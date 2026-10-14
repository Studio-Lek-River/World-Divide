#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSUnitData.generated.h"

UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSUnitData : public URTSEntityData
{
	GENERATED_BODY()

public:
	/** Top speed in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "0"))
	float MoveSpeed = 400.f;

	/** Degrees per second the unit turns toward its heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float TurnRate = 360.f;

	/** Vehicles tilt their mesh to the slope they stand on; infantry stays upright. Aircraft ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	bool bTiltsToGround = false;

	/**
	 * A turret that traverses on its own, carried by Mesh: the hull then only turns to drive and the turret aims. Without
	 * one the whole unit turns toward its target.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> TurretMesh;

	/** Where the turret's pivot sits on Mesh, in Mesh's own space (MeshScale applies to both). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector TurretOffset = FVector::ZeroVector;

	/** The barrel tip in the turret's own space, where shots start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector MuzzleOffset = FVector::ZeroVector;

	/** Degrees per second the turret traverses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float TurretTurnRate = 120.f;
};
