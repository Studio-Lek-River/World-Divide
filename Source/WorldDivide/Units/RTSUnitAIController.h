#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RTSUnitAIController.generated.h"

/** Server-side pathing for an ARTSUnit. Orders and targeting live on the unit and its weapon component. */
UCLASS()
class WORLDDIVIDE_API ARTSUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
};
