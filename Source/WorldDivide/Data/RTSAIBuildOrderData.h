#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSAIBuildOrderData.generated.h"

class URTSEntityData;
class URTSUnitData;

/** How hard an AI opponent plays: it scales the AI's attack waves. */
UENUM(BlueprintType)
enum class ERTSAIDifficulty : uint8
{
	Easy,
	Normal,
	Hard,
};

/** One entry of a build order: own at least Count of Entry before going on. */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSBuildOrderStep
{
	GENERATED_BODY()

	/** A building for a builder to place, or a unit or upgrade to produce at a building that offers it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TObjectPtr<URTSEntityData> Entry;

	/** Buildings count with their construction sites, units with those queued, an upgrade once queued or researched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "1"))
	int32 Count = 1;
};

/** What a difficulty multiplies a build order's waves by. */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSAIWaveScale
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0.1"))
	float IntervalScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "0.1"))
	float SizeScale = 1.f;
};

/** When an AI opponent sends its army at the enemy. */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSAIWaveSettings
{
	GENERATED_BODY()

	/** Seconds after the last wave before the next goes with whatever army is ready, if it is at least MinSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "1"))
	float Interval = 90.f;

	/** An army this large goes at once, whatever the time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "1"))
	int32 Size = 8;

	/** The smallest army the interval sends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI", meta = (ClampMin = "1"))
	int32 MinSize = 4;
};

/**
 * How an AI opponent plays a faction: the base it builds, in order, the army it keeps producing once the base stands, and
 * when it attacks. URTSFactionData::AIBuildOrder names the one an AI of that faction follows.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSAIBuildOrderData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Worked through top to bottom; a step lost again, such as a destroyed building, comes first once more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TArray<FRTSBuildOrderStep> Steps;

	/** Produced in turn once every step is met, to fill the attack waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TArray<TObjectPtr<URTSUnitData>> ArmyUnits;

	/** The waves at Normal difficulty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	FRTSAIWaveSettings Waves;

	/** What each difficulty multiplies Waves by; a difficulty left out plays as Normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TMap<ERTSAIDifficulty, FRTSAIWaveScale> DifficultyScales;
};
