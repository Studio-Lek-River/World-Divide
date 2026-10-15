#include "Units/RTSUnitAIController.h"

#include "Navigation/RTSPathFollowingComponent.h"
#include "Units/RTSUnit.h"

ARTSUnitAIController::ARTSUnitAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URTSPathFollowingComponent>(TEXT("PathFollowingComponent")))
{
	// The unit turns itself and never reads a control rotation or focus, so the controller has nothing to tick for. Path
	// following ticks as its own component and still reports OnMoveCompleted.
	PrimaryActorTick.bCanEverTick = false;
}

void ARTSUnitAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (ARTSUnit* Unit = GetPawn<ARTSUnit>())
	{
		Unit->NotifyMoveFinished(Result);
	}
}
