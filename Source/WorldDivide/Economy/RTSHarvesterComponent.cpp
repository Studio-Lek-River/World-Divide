#include "Economy/RTSHarvesterComponent.h"

#include "AIController.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSHarvesterData.h"
#include "Economy/RTSSupplySource.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	/** How close to a footprint edge, beyond the harvester's own radius, counts as docked. */
	constexpr float DockReach = 150.f;

	/** The approach aims this far, beyond the harvester's own radius, outside the edge: clear of the navmesh hole. */
	constexpr float DockOffset = 60.f;

	constexpr float ApproachAcceptanceRadius = 25.f;

	/** Seconds between attempts when an approach failed or no depot exists yet. */
	constexpr float RetrySeconds = 1.f;
}

URTSHarvesterComponent::URTSHarvesterComponent()
{
	// The unit's Harvest order drives the run from the unit's tick.
	PrimaryComponentTick.bCanEverTick = false;
}

int32 URTSHarvesterComponent::GetTeamId() const
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	return Unit ? Unit->GetTeamId() : INDEX_NONE;
}

bool URTSHarvesterComponent::StartHarvest(ARTSSupplySource* InSource)
{
	if (!HarvesterData)
	{
		return false;
	}
	Depot.Reset();
	RetryCooldown = 0.f;

	if (InSource && !InSource->IsDepleted())
	{
		Source = InSource;
		State = ERTSHarvestState::ToSource;
	}
	else if (!PickNextSource())
	{
		// Nothing to load, but a load on board is still worth delivering.
		State = Carried > 0 ? ERTSHarvestState::ToDepot : ERTSHarvestState::Idle;
	}
	// Whatever the owner was doing, the run picks its own path.
	StopOwnerMovement();
	return State != ERTSHarvestState::Idle;
}

void URTSHarvesterComponent::StopHarvest()
{
	State = ERTSHarvestState::Idle;
	Source.Reset();
	Depot.Reset();
}

bool URTSHarvesterComponent::PickNextSource()
{
	Source = ARTSSupplySource::FindNearest(*GetWorld(), GetOwner()->GetActorLocation(), HarvesterData->SearchRadius);
	if (!Source.IsValid())
	{
		return false;
	}
	State = ERTSHarvestState::ToSource;
	return true;
}

bool URTSHarvesterComponent::ContinueAfterSourceEmpty()
{
	if (Carried > 0)
	{
		State = ERTSHarvestState::ToDepot;
		return true;
	}
	if (PickNextSource())
	{
		return true;
	}
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s found no supply source in reach and stops"), *GetOwner()->GetName());
	StopHarvest();
	return false;
}

bool URTSHarvesterComponent::UpdateHarvest(float DeltaSeconds)
{
	if (!HarvesterData || State == ERTSHarvestState::Idle)
	{
		return false;
	}
	RetryCooldown = FMath::Max(RetryCooldown - DeltaSeconds, 0.f);
	const FVector Location = GetOwner()->GetActorLocation();

	if (State == ERTSHarvestState::ToSource || State == ERTSHarvestState::Gathering)
	{
		// Another harvester may have emptied it.
		ARTSSupplySource* Target = Source.Get();
		if (!Target || Target->IsDepleted())
		{
			return ContinueAfterSourceEmpty();
		}

		if (State == ERTSHarvestState::ToSource)
		{
			const FVector Edge = Target->GetClosestFootprintPoint(Location);
			if (!IsDocked(Edge))
			{
				ApproachDock(Edge);
				return true;
			}
			StopOwnerMovement();
			State = ERTSHarvestState::Gathering;
			GatherTimeLeft = HarvesterData->GatherSeconds;
		}

		GatherTimeLeft -= DeltaSeconds;
		if (GatherTimeLeft <= 0.f)
		{
			Carried += Target->Take(HarvesterData->CarryAmount - Carried);
			State = ERTSHarvestState::ToDepot;
			Depot.Reset();
		}
		return true;
	}

	// ToDepot. Without a depot the harvester waits, loaded, and looks again every so often.
	if (!Depot.IsValid())
	{
		if (RetryCooldown > 0.f)
		{
			return true;
		}
		Depot = FindNearestDepot(*GetWorld(), GetTeamId(), Location);
		if (!Depot.IsValid())
		{
			RetryCooldown = RetrySeconds;
			return true;
		}
	}
	const FVector Edge = Depot->GetClosestFootprintPoint(Location);
	if (!IsDocked(Edge))
	{
		ApproachDock(Edge);
		return true;
	}

	StopOwnerMovement();
	Deposit();
	if (Source.IsValid() && !Source->IsDepleted())
	{
		State = ERTSHarvestState::ToSource;
		return true;
	}
	return ContinueAfterSourceEmpty();
}

void URTSHarvesterComponent::Deposit()
{
	if (ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(*GetWorld(), GetTeamId()))
	{
		Player->AddCredits(Carried);
	}
	Carried = 0;
}

void URTSHarvesterComponent::StopOwnerMovement() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AAIController* AI = Pawn ? Pawn->GetController<AAIController>() : nullptr)
	{
		AI->StopMovement();
	}
}

void URTSHarvesterComponent::ApproachDock(const FVector& EdgePoint)
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	AAIController* AI = Unit ? Unit->GetController<AAIController>() : nullptr;
	if (!AI || AI->GetMoveStatus() != EPathFollowingStatus::Idle || RetryCooldown > 0.f)
	{
		return;
	}

	// The dock's centre lies in its own navmesh hole; aim just outside the edge nearest the harvester.
	const FVector Outward = (Unit->GetActorLocation() - EdgePoint).GetSafeNormal2D();
	const FVector Goal = EdgePoint + Outward * (Unit->GetSelectionRadius() + DockOffset);
	AI->MoveToLocation(Goal, ApproachAcceptanceRadius,
		/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
	// Whether the request failed or will end short of the dock, the next attempt waits instead of coming every tick.
	RetryCooldown = RetrySeconds;
}

bool URTSHarvesterComponent::IsDocked(const FVector& EdgePoint) const
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	return Unit && FVector::Dist2D(Unit->GetActorLocation(), EdgePoint) <= Unit->GetSelectionRadius() + DockReach;
}

ARTSBuilding* URTSHarvesterComponent::FindNearestDepot(const UWorld& World, int32 TeamId, const FVector& From)
{
	ARTSBuilding* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
	{
		const URTSBuildingData* Data = It->GetBuildingData();
		if (It->GetTeamId() != TeamId || !Data || !Data->bAcceptsSupply || !It->IsConstructed())
		{
			continue;
		}
		const float Distance = FVector::Dist2D(From, It->GetClosestFootprintPoint(From));
		if (Distance < NearestDistance)
		{
			Nearest = *It;
			NearestDistance = Distance;
		}
	}
	return Nearest;
}
