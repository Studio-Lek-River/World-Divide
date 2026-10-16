#include "Navigation/RTSPathFollowingComponent.h"

#include "AIController.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSHullClearance.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"

void URTSPathFollowingComponent::OnPathUpdated()
{
	Super::OnPathUpdated();

	const AAIController* Controller = Cast<AAIController>(GetOwner());
	const ARTSUnit* Unit = Controller ? Controller->GetPawn<ARTSUnit>() : nullptr;
	const ARecastNavMesh* NavMesh = Path.IsValid() ? Cast<ARecastNavMesh>(Path->GetNavigationDataUsed()) : nullptr;
	if (Unit && NavMesh && Path->IsValid() && !Path->GetPathPoints().IsEmpty())
	{
		// A move order into a wall or a building paths to the edge of the navmesh (ARTSUnit::RequestMove); the hull may go on
		// toward the spot clicked.
		const TOptional<FVector> Goal = Unit->GetMoveGoal();
		const TOptional<FVector> Target = Unit->GetMoveTarget();
		const bool bShortOfTarget = Goal && Target && FVector::Dist2D(*Goal, Path->GetPathPoints().Last().Location) < 1.f
			&& FVector::Dist2D(*Goal, *Target) > 1.f;
		RTSHullClearance::AdjustPath(*NavMesh, Unit->GetShape(), Path->GetPathPoints(), bShortOfTarget ? &Target.GetValue() : nullptr);
	}
	if (URTSUnitMovement* Locomotion = GetLocomotion(); Locomotion && Path.IsValid())
	{
		Locomotion->SetPath(Path->GetPathPoints());
	}
}

URTSUnitMovement* URTSPathFollowingComponent::GetLocomotion() const
{
	const AAIController* Controller = Cast<AAIController>(GetOwner());
	const ARTSUnit* Unit = Controller ? Controller->GetPawn<ARTSUnit>() : nullptr;
	URTSUnitMovement* Movement = Unit ? Unit->GetUnitMovement() : nullptr;
	return Movement && Movement->UsesLocomotion() ? Movement : nullptr;
}

void URTSPathFollowingComponent::FollowPathSegment(float DeltaTime)
{
	// The gear reads the path itself.
	if (!GetLocomotion())
	{
		Super::FollowPathSegment(DeltaTime);
	}
}

bool URTSPathFollowingComponent::HasReachedCurrentTarget(const FVector& CurrentLocation) const
{
	if (const URTSUnitMovement* Locomotion = GetLocomotion())
	{
		return Locomotion->GetGuidanceTarget() > MoveSegmentEndIndex;
	}
	return Super::HasReachedCurrentTarget(CurrentLocation);
}

bool URTSPathFollowingComponent::UpdateBlockDetection()
{
	// A pivot stands still for seconds; ARTSUnit::UpdateBlockedMove knows a stuck gear from a turning one.
	return GetLocomotion() ? false : Super::UpdateBlockDetection();
}
