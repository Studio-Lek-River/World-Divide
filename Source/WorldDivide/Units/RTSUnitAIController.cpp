#include "Units/RTSUnitAIController.h"

#include "Units/RTSUnit.h"

void ARTSUnitAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (ARTSUnit* Unit = GetPawn<ARTSUnit>())
	{
		Unit->NotifyMoveFinished(Result);
	}
}
