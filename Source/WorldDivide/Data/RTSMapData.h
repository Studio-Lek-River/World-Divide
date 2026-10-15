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

	/** A top-down image of the level the lobby shows: world +X is up, world +Y is right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	FSoftObjectPath Preview;

	/** The world XY rectangle the preview covers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	FBox2D PreviewArea = FBox2D(FVector2D(-30000.0), FVector2D(30000.0));

	/** Where the PlayerStart tagged "Team<N>" is on the preview, at index N: 0..1 from the left and from the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	TArray<FVector2D> StartSpotPositions;

	/** Where WorldLocation is on the preview, as in StartSpotPositions. */
	FVector2D ToPreviewPosition(const FVector& WorldLocation) const
	{
		const FVector2D Size = PreviewArea.GetSize();
		return FVector2D(
			Size.Y > 0.0 ? (WorldLocation.Y - PreviewArea.Min.Y) / Size.Y : 0.5,
			Size.X > 0.0 ? (PreviewArea.Max.X - WorldLocation.X) / Size.X : 0.5);
	}
};
