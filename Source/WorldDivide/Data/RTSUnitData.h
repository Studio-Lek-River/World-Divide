#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSUnitData.generated.h"

/** What a vehicle part rides on: it follows that part's turn, and the gun's elevation for Gun. */
UENUM(BlueprintType)
enum class ERTSVehiclePartMount : uint8
{
	Hull,
	Turret,
	Gun,
};

/** A part beyond the hull, turret and gun: an add-on such as a weapon station, or a road wheel. */
USTRUCT(BlueprintType)
struct FRTSVehiclePart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	ERTSVehiclePartMount MountedOn = ERTSVehiclePartMount::Hull;

	/** Where the part's pivot sits in the space of what it is mounted on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector Offset = FVector::ZeroVector;

	/** The part's turn on its mount, such as a right-hand wheel that is the left one turned round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FRotator Rotation = FRotator::ZeroRotator;

	/**
	 * A hull wheel that rolls with the ground under it as the unit drives and turns, about its own Y axis; its radius is half
	 * the mesh's height. Wheels sharing a mesh are drawn as one instanced component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	bool bWheel = false;
};

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

	/**
	 * A gun that elevates on its own, carried by the turret: it pitches toward its target's height. With one, shots start at
	 * GunMuzzleOffset on the gun instead of MuzzleOffset on the turret.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> GunMesh;

	/** Where the gun's trunnion sits on the turret, in the turret's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector GunOffset = FVector::ZeroVector;

	/** The barrel tip in the gun's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FVector GunMuzzleOffset = FVector::ZeroVector;

	/** Depression and elevation limits in degrees, up positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float GunMinPitch = -8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float GunMaxPitch = 20.f;

	/** Degrees per second the gun elevates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float GunElevationRate = 30.f;

	/** Add-ons and running gear, placed on the hull, the turret or the gun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TArray<FRTSVehiclePart> Parts;
};
