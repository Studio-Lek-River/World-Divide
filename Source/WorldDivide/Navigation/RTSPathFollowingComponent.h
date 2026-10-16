#pragma once

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"
#include "RTSPathFollowingComponent.generated.h"

class URTSUnitMovement;

/**
 * Path following for an ARTSUnit: every path it is given, and every path the navigation system finds again after the
 * navmesh or the goal changed, is fitted to the unit's hull first (RTSHullClearance::AdjustPath), once per path.
 * A vehicle's gear follows the path on its own (URTSUnitMovement): it gets each path, path following asks it nothing and
 * counts the points its guidance passed as reached, and its own blocked handling replaces the engine's.
 */
UCLASS()
class WORLDDIVIDE_API URTSPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

protected:
	virtual void OnPathUpdated() override;
	virtual void FollowPathSegment(float DeltaTime) override;
	virtual bool HasReachedCurrentTarget(const FVector& CurrentLocation) const override;
	virtual bool UpdateBlockDetection() override;

private:
	/** The unit's movement when its gear drives it; null for units on legs. */
	URTSUnitMovement* GetLocomotion() const;
};
