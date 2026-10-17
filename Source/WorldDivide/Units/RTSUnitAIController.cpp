#include "Units/RTSUnitAIController.h"

#include "Navigation/RTSPathFollowingComponent.h"
#include "Navigation/RTSPathSearch.h"
#include "NavigationSystem.h"
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

void ARTSUnitAIController::FindPathForMoveRequest(const FAIMoveRequest& MoveRequest, FPathFindingQuery& Query, FNavPathSharedPtr& OutPath) const
{
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSystem)
	{
		return;
	}
	const FPathFindingResult Result = RTSPathSearch::FindFullPath(*NavSystem, Query);
	if (Result.IsSuccessful() && Result.Path.IsValid())
	{
		if (MoveRequest.IsMoveToActorRequest())
		{
			Result.Path->SetGoalActorObservation(*MoveRequest.GetGoalActor(), 100.0f);
		}
		Result.Path->EnableRecalculationOnInvalidation(true);
		RTSPathSearch::KeepLimitOnRepath(*Result.Path);
		OutPath = Result.Path;
	}
}
