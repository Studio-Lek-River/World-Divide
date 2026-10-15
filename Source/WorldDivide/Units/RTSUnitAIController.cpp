#include "Units/RTSUnitAIController.h"

#include "Navigation/RTSPathFollowingComponent.h"
#include "Units/RTSUnit.h"

ARTSUnitAIController::ARTSUnitAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URTSPathFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void ARTSUnitAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (ARTSUnit* Unit = GetPawn<ARTSUnit>())
	{
		Unit->NotifyMoveFinished(Result);
	}
}
