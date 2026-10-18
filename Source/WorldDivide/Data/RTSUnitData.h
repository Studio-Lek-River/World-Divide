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

class UMaterialInterface;

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

	/**
	 * A wheel's rolling radius in cm, before the unit's MeshScale: where its rim meets the ground, or for a sprocket or an
	 * idler the track's centre line wrapped round it. Zero takes half the mesh's height.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "0", EditCondition = "bWheel"))
	float RollRadius = 0.f;

	/**
	 * Non-zero makes a hull part a track: the length in cm of one of its links, before the unit's MeshScale. Its material
	 * (M_Vehicle with TrackAnimation, #151) moves the links round their loop by the travel of the track's side, from the
	 * part's Offset.Y, which the unit gives it as a phase through one link in custom primitive data.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "0"))
	float TrackPitch = 0.f;

	/**
	 * A track's material without the link animation, for every slot: drawn instead while rts.Vehicle.TrackAnimation is 0,
	 * and the one a track's far LODs take once it has them (#149). None keeps the mesh's own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UMaterialInterface> RigidMaterial;
};

/** What moves a ground unit: legs walk anywhere they face, tracks and wheels drive the hull (RTSLocomotion). */
UENUM(BlueprintType)
enum class ERTSLocomotionType : uint8
{
	/** Infantry: walks the way its path goes and turns to face it. Aircraft fly by their own rules and ignore it. */
	Legs,
	/** Left and right tracks at their own speeds: drives, turns and, when allowed, pivots on the spot. */
	Tracked,
	/** Steered front wheels: turns only while rolling, no tighter than its turning circle. */
	Wheeled,
};

/**
 * How a vehicle's tracks or wheels move its hull (RTSLocomotion, URTSUnitMovement). URTSUnitData::MoveSpeed is the top
 * speed forward and URTSUnitData::TurnRate the fastest it turns. Distances in cm, angles in degrees, times in seconds.
 */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSLocomotionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion")
	ERTSLocomotionType Type = ERTSLocomotionType::Legs;

	/** Top speed backwards in cm/s; 0 never reverses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "0"))
	float ReverseSpeed = 0.f;

	/** cm/s each second gained toward a faster speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1"))
	float Acceleration = 400.f;

	/** cm/s each second shed toward a slower speed or a standstill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1"))
	float Braking = 1000.f;

	/** Tracked: the distance between the middles of the two tracks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Tracked"))
	float TrackGauge = 280.f;

	/** Tracked: turns on the spot, one track forward and one back, when its way lies far to the side or behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (EditCondition = "Type == ERTSLocomotionType::Tracked"))
	bool bAllowPivot = true;

	/** Tracked: turns on the spot first once its way lies more than this many degrees off its heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", ClampMax = "179", EditCondition = "Type == ERTSLocomotionType::Tracked"))
	float PivotAngle = 60.f;

	/** Wheeled: the distance between the front and rear axles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float Wheelbase = 400.f;

	/** Wheeled: the distance between the left and right wheels, for how far each side rolls in a turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float AxleTrack = 220.f;

	/** Wheeled: the furthest the front wheels steer either way, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", ClampMax = "80", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float MaxSteerAngle = 35.f;

	/** Wheeled: degrees per second the front wheels steer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float SteerRate = 60.f;

	/** Wheeled: the tightest turn in cm, at the rear axle; 0 leaves it to the steering, Wheelbase / tan(MaxSteerAngle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "0", EditCondition = "Type == ERTSLocomotionType::Wheeled"))
	float MinTurnRadius = 0.f;

	/** cm/s² of sideways pull a turn may take before the vehicle slows for it: its grip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion", meta = (ClampMin = "1"))
	float MaxLateralAcceleration = 400.f;

	/** True for tracks and wheels. */
	bool IsVehicle() const { return Type != ERTSLocomotionType::Legs; }

	/** Wheeled: the tightest turn the profile allows, from MinTurnRadius and the steering. */
	float GetMinTurnRadius() const;

	/** Whether every value the type uses is finite and in range; the first problem in OutError. Legs is always valid. */
	bool Validate(FString* OutError = nullptr) const;
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

	/** Tracks or wheels, or legs (the default); aircraft ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FRTSLocomotionProfile Locomotion;

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

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
