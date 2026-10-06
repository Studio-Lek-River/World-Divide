#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RTSUnitAIController.generated.h"

/** Server-side brain of an ARTSUnit. For now it only walks; targeting and combat come later. */
UCLASS()
class WORLDDIVIDE_API ARTSUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
};
