#include "Navigation/RTSPathFollowingComponent.h"

#include "AIController.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSHullClearance.h"
#include "Units/RTSUnit.h"

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
}
