#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RTSUnitAIController.generated.h"

/**
 * Server-side pathing for an ARTSUnit, with paths fitted to its hull (URTSPathFollowingComponent). Orders and targeting live
 * on the unit and its weapon component.
 */
UCLASS()
class WORLDDIVIDE_API ARTSUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	ARTSUnitAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
};
