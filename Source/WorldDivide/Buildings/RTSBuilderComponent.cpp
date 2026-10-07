#include "Buildings/RTSBuilderComponent.h"

#include "AIController.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Navigation/PathFollowingComponent.h"
#include "Units/RTSApproach.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	/** How close to the footprint edge, beyond the builder's own radius, counts as standing at the site. */
	constexpr float SiteReach = 150.f;

	/** The approach aims this far, beyond the builder's own radius, outside the edge: clear of the footprint. */
	constexpr float SiteOffset = 60.f;

	constexpr float SiteAcceptanceRadius = 25.f;

	/** Seconds between attempts when an approach ended short of the site. */
	constexpr float SiteRetrySeconds = 1.f;

	/** Own units stepping off a site aim this far, beyond their own radius, outside the edge. */
	constexpr float ClearOffset = 60.f;

	/** How long a builder waits for own units to clear its site before it gives up. */
	constexpr float ClearSiteTimeout = 5.f;
}

URTSBuilderComponent::URTSBuilderComponent()
{
	// The unit's Build order drives the work from the unit's tick.
	PrimaryComponentTick.bCanEverTick = false;
}

bool URTSBuilderComponent::CanBuild(const URTSBuildingData* Data) const
{
	return BuilderData && Data
		&& BuilderData->Buildings.ContainsByPredicate([Data](const TObjectPtr<URTSBuildingData>& Entry) { return Entry.Get() == Data; });
}

bool URTSBuilderComponent::StartBuild(const FRTSCommand& Command)
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	if (!BuilderData || !Unit)
	{
		return false;
	}
	StopBuild();

	if (ARTSBuilding* Target = Cast<ARTSBuilding>(Command.TargetActor))
	{
		// Clients name the site, so the server checks it: own, standing, unfinished, and something this builder can build.
		if (Target->GetTeamId() != Unit->GetTeamId() || Target->IsConstructed() || Target->GetHealthComponent()->IsDead()
			|| !CanBuild(Target->GetBuildingData()))
		{
			return false;
		}
		Site = Target;
		bHasSite = true;
	}
	else
	{
		URTSBuildingData* Data = Cast<URTSBuildingData>(Command.EntityData);
		if (!CanBuild(Data))
		{
			return false;
		}
		// The spot itself is checked on arrival, when whatever stands there now may have moved on.
		PlannedData = Data;
		PlannedLocation = RTSBuildPlacement::SnapToGrid(Command.TargetLocation, Data->Footprint);
	}

	State = ERTSBuildState::ToSite;
	// Whatever the owner was doing, the order picks its own path.
	StopOwnerMovement();
	return true;
}

void URTSBuilderComponent::StopBuild()
{
	State = ERTSBuildState::Idle;
	PlannedData = nullptr;
	Site.Reset();
	bHasSite = false;
	RetryCooldown = 0.f;
	ClearingSeconds = 0.f;
	ClearedUnits.Reset();
}

bool URTSBuilderComponent::UpdateBuild(float DeltaSeconds)
{
	if (!BuilderData || State == ERTSBuildState::Idle)
	{
		return false;
	}
	RetryCooldown = FMath::Max(RetryCooldown - DeltaSeconds, 0.f);

	// Another builder may have finished the site, or its owner cancelled it, or the enemy destroyed it.
	ARTSBuilding* Building = Site.Get();
	if (bHasSite && (!Building || Building->IsConstructed() || Building->GetHealthComponent()->IsDead()))
	{
		StopOwnerMovement();
		StopBuild();
		return false;
	}

	const URTSBuildingData* Data = Building ? Building->GetBuildingData() : PlannedData.Get();
	if (!Data)
	{
		return EndBuild(TEXT("has nothing to build"));
	}
	const FVector Center = Building ? Building->GetActorLocation() : PlannedLocation;
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Data->Footprint);
	const FVector Location = GetOwner()->GetActorLocation();
	const FVector Edge(
		FMath::Clamp(Location.X, Center.X - Extent.X, Center.X + Extent.X),
		FMath::Clamp(Location.Y, Center.Y - Extent.Y, Center.Y + Extent.Y),
		Center.Z);
	if (!IsDocked(Edge))
	{
		State = ERTSBuildState::ToSite;
		return ApproachSite(Center, Data->Footprint) || EndBuild(TEXT("cannot reach its site"));
	}

	if (State == ERTSBuildState::ToSite)
	{
		StopOwnerMovement();
		State = ERTSBuildState::Constructing;
	}
	if (!Building)
	{
		if (ClearSite(DeltaSeconds))
		{
			return true;
		}
		if (!PlaceSite())
		{
			return EndBuild(TEXT("could not place its building"));
		}
		Building = Site.Get();
	}

	Building->UpdateConstruction(DeltaSeconds);
	if (Building->IsConstructed())
	{
		StopBuild();
		return false;
	}
	return true;
}

bool URTSBuilderComponent::PlaceSite()
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	ARTSPlayerState* Player = Unit ? ARTSPlayerState::FindForTeam(*GetWorld(), Unit->GetTeamId()) : nullptr;
	if (!Player)
	{
		return false;
	}

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Build;
	Command.EntityData = PlannedData;
	Command.TargetLocation = PlannedLocation;
	ARTSBuilding* Building = RTSCommands::PurchaseBuilding(*GetWorld(), *Player, Command);
	if (!Building)
	{
		return false;
	}
	// From here on only builders advance it.
	Building->bAutoConstruct = false;
	Site = Building;
	bHasSite = true;
	PlannedData = nullptr;
	ClearingSeconds = 0.f;
	ClearedUnits.Reset();
	return true;
}

bool URTSBuilderComponent::ClearSite(float DeltaSeconds)
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	if (!Unit || !PlannedData)
	{
		return false;
	}
	FVector Center;
	TArray<AActor*> Blockers;
	if (RTSBuildPlacement::CanPlace(*GetWorld(), *PlannedData, PlannedLocation, Center, &Blockers) || Blockers.IsEmpty())
	{
		return false;
	}

	// Only own units step aside; enemy units, buildings and obstacles leave the spot to be refused.
	TArray<ARTSUnit*, TInlineAllocator<8>> OwnUnits;
	for (AActor* Blocker : Blockers)
	{
		ARTSUnit* Other = Cast<ARTSUnit>(Blocker);
		if (!Other || Other == Unit || Other->GetTeamId() != Unit->GetTeamId())
		{
			return false;
		}
		OwnUnits.Add(Other);
	}
	ClearingSeconds += DeltaSeconds;
	if (ClearingSeconds > ClearSiteTimeout)
	{
		return false;
	}

	// Each unit is sent once; one that finds no spot or wanders back runs out the timeout.
	for (ARTSUnit* Other : OwnUnits)
	{
		if (ClearedUnits.Contains(Other))
		{
			continue;
		}
		ClearedUnits.Add(Other);
		const float Radius = Other->GetSelectionRadius();
		FVector Spot;
		if (RTSApproach::FindReachableSpot(*Other, Center, PlannedData->Footprint, Radius + ClearOffset, Radius + SiteReach, Spot))
		{
			Other->StepAside(Spot);
		}
	}
	return true;
}

bool URTSBuilderComponent::ApproachSite(const FVector& Center, const FIntPoint& Footprint)
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	AAIController* AI = Unit ? Unit->GetController<AAIController>() : nullptr;
	if (!AI || AI->GetMoveStatus() != EPathFollowingStatus::Idle || RetryCooldown > 0.f)
	{
		return true;
	}

	// Next to the footprint, facing side first: inside it the builder would block its own building.
	const float Radius = Unit->GetSelectionRadius();
	FVector Goal;
	if (!RTSApproach::FindReachableSpot(*Unit, Center, Footprint, Radius + SiteOffset,
		Radius + SiteReach - SiteAcceptanceRadius, Goal))
	{
		return false;
	}
	AI->MoveToLocation(Goal, SiteAcceptanceRadius,
		/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
	// Should the move end short of the site, the next attempt waits instead of coming every tick.
	RetryCooldown = SiteRetrySeconds;
	return true;
}

bool URTSBuilderComponent::EndBuild(const TCHAR* Reason)
{
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s %s and stops"), *GetOwner()->GetName(), Reason);
	StopOwnerMovement();
	StopBuild();
	return false;
}

bool URTSBuilderComponent::IsDocked(const FVector& EdgePoint) const
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	if (!Unit)
	{
		return false;
	}
	const float Distance = FVector::Dist2D(Unit->GetActorLocation(), EdgePoint);
	return Distance > Unit->GetSelectionRadius() && Distance <= Unit->GetSelectionRadius() + SiteReach;
}

void URTSBuilderComponent::StopOwnerMovement() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AAIController* AI = Pawn ? Pawn->GetController<AAIController>() : nullptr)
	{
		AI->StopMovement();
	}
}
