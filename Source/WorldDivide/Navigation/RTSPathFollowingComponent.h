#pragma once

#include "CoreMinimal.h"
#include "Navigation/PathFollowingComponent.h"
#include "RTSPathFollowingComponent.generated.h"

/**
 * Path following for an ARTSUnit: every path it is given, and every path the navigation system finds again after the
 * navmesh or the goal changed, is fitted to the unit's hull first (RTSHullClearance::AdjustPath), once per path.
 */
UCLASS()
class WORLDDIVIDE_API URTSPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

protected:
	virtual void OnPathUpdated() override;
};
