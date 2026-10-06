#pragma once

#include "CoreMinimal.h"
#include "RTSCommand.generated.h"

UENUM(BlueprintType)
enum class ERTSCommandType : uint8
{
	None,
	Move,
	Stop,
};

/**
 * Every player order is one of these. Orders travel client -> server through ARTSPlayerController::Server_IssueCommand
 * and only the server executes them, so singleplayer and multiplayer share one code path.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	ERTSCommandType Type = ERTSCommandType::None;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	TObjectPtr<AActor> TargetActor = nullptr;

	/** Append to the unit's order queue instead of replacing it (Shift). */
	UPROPERTY(BlueprintReadWrite, Category = "Command")
	bool bQueue = false;
};
