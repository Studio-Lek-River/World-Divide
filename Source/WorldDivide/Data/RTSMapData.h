#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "Engine/DataAsset.h"
#include "RTSMapData.generated.h"

/** A map the lobby offers for a skirmish. URTSSettings::SkirmishMaps lists them in the order the lobby shows them. */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSMapData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FText DisplayName;

	/** The level the match is played on, with a skirmish game mode and a PlayerStart tagged "Team<N>" for each start spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath Map;

	/** The PlayerStarts tagged Team0..Team<StartSpots - 1>; the lobby has one seat per start spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map", meta = (ClampMin = "1"))
	int32 StartSpots = 2;

	/** The seats the lobby starts with, one per start spot: closed seats are open for joining players, missing ones too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seats")
	TArray<FRTSSeat> DefaultSeats;
};
