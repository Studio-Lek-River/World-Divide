#include "Economy/RTSHarvesterComponent.h"

#include "AIController.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSHarvesterData.h"
#include "Economy/RTSSupplySource.h"
#include "EngineUtils.h"
#include "Navigation/PathFollowingComponent.h"
#include "Units/RTSApproach.h"
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
	SkippedSources.Reset();
	SkippedDepots.Reset();
	RetryCooldown = 0.f;

	if (InSource && !InSource->IsDepleted())
	{
		Source = InSource;
		bSourceOrdered = true;
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
	SkippedSources.Reset();
	SkippedDepots.Reset();
}

bool URTSHarvesterComponent::PickNextSource()
{
	Source = ARTSSupplySource::FindNearest(*GetWorld(), GetOwner()->GetActorLocation(), HarvesterData->SearchRadius, SkippedSources);
	if (!Source.IsValid())
	{
		return false;
	}
	bSourceOrdered = false;
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
			if (!IsDocked(Target->GetClosestFootprintPoint(Location)))
			{
				return ApproachDock(Target->GetActorLocation(), Target->Footprint)
					|| (bSourceOrdered ? EndUnreachable(*Target) : SkipUnreachableSource());
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
		Depot = FindNearestDepot(*GetWorld(), GetTeamId(), Location, SkippedDepots);
		if (!Depot.IsValid())
		{
			// Depots stand, but none can be reached: waiting would not change that.
			for (const TWeakObjectPtr<ARTSBuilding>& Skipped : SkippedDepots)
			{
				if (Skipped.IsValid())
				{
					return EndUnreachable(*Skipped);
				}
			}
			RetryCooldown = RetrySeconds;
			return true;
		}
	}
	if (!IsDocked(Depot->GetClosestFootprintPoint(Location)))
	{
		return ApproachDock(Depot->GetActorLocation(), Depot->GetFootprint()) || SkipUnreachableDepot();
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
	// Whatever blocked a depot on this delivery may be gone by the next one.
	SkippedDepots.Reset();
}

void URTSHarvesterComponent::StopOwnerMovement() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AAIController* AI = Pawn ? Pawn->GetController<AAIController>() : nullptr)
	{
		AI->StopMovement();
	}
}

bool URTSHarvesterComponent::ApproachDock(const FVector& Center, const FIntPoint& Footprint)
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	AAIController* AI = Unit ? Unit->GetController<AAIController>() : nullptr;
	if (!AI || AI->GetMoveStatus() != EPathFollowingStatus::Idle || RetryCooldown > 0.f)
	{
		return true;
	}

	// The dock's centre lies in its own navmesh hole; stand just outside the footprint, facing side first.
	const float Radius = Unit->GetStandOffRadius();
	FVector Goal;
	if (!RTSApproach::FindReachableSpot(*Unit, Center, Footprint, Radius + DockOffset,
		Radius + DockReach - ApproachAcceptanceRadius, Goal))
	{
		return false;
	}
	AI->MoveToLocation(Goal, ApproachAcceptanceRadius,
		/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
	// Should the move end short of the dock, the next attempt waits instead of coming every tick.
	RetryCooldown = RetrySeconds;
	return true;
}

bool URTSHarvesterComponent::EndUnreachable(const AActor& Dock)
{
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s cannot reach %s and stops"), *GetOwner()->GetName(), *Dock.GetName());
	StopOwnerMovement();
	StopHarvest();
	return false;
}

bool URTSHarvesterComponent::SkipUnreachableSource()
{
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s cannot reach %s and looks for another source"), *GetOwner()->GetName(), *Source->GetName());
	SkippedSources.Add(Source);
	Source.Reset();
	return ContinueAfterSourceEmpty();
}

bool URTSHarvesterComponent::SkipUnreachableDepot()
{
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s cannot reach %s and looks for another depot"), *GetOwner()->GetName(), *Depot->GetName());
	SkippedDepots.Add(Depot);
	Depot.Reset();
	return true;
}

bool URTSHarvesterComponent::IsDocked(const FVector& EdgePoint) const
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	return Unit && FVector::Dist2D(Unit->GetActorLocation(), EdgePoint) <= Unit->GetStandOffRadius() + DockReach;
}

ARTSBuilding* URTSHarvesterComponent::FindNearestDepot(const UWorld& World, int32 TeamId, const FVector& From,
	TConstArrayView<TWeakObjectPtr<ARTSBuilding>> Exclude)
{
	ARTSBuilding* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
	{
		const URTSBuildingData* Data = It->GetBuildingData();
		if (It->GetTeamId() != TeamId || !Data || !Data->bAcceptsSupply || !It->IsConstructed() || Exclude.Contains(*It))
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
