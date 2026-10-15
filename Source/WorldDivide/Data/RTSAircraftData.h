#pragma once

#include "CoreMinimal.h"
#include "Data/RTSUnitData.h"
#include "RTSAircraftData.generated.h"

/**
 * A unit that flies: it parks on a pad of the airfield that produced it, takes off for each order, flies at altitude over
 * the navmesh and banks into its turns, and fires a limited load per sortie before it returns to its pad to rearm.
 * MoveSpeed is its airspeed, never zero in flight, and TurnRate how fast it turns at that speed.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSAircraftData : public URTSUnitData
{
	GENERATED_BODY()

public:
	/** Height above the ground it flies at, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0"))
	float CruiseAltitude = 3000.f;

	/** How fast it climbs off its pad and descends, in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "1"))
	float ClimbRate = 1200.f;

	/** How far it rolls into its tightest turn, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flight", meta = (ClampMin = "0", ClampMax = "89"))
	float MaxBankAngle = 45.f;

	/** Shots it fires per sortie. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sortie", meta = (ClampMin = "1"))
	int32 AmmoPerSortie = 4;

	/** Seconds on its pad to rearm from empty to full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sortie", meta = (ClampMin = "0"))
	float RearmSeconds = 8.f;
};
