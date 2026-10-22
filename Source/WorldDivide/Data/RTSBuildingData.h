#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSBuildingData.generated.h"

class UCurveTable;
class UStaticMesh;
class URTSFlagTypes;
class URTSUnitData;
class URTSUpgradeData;

/**
 * How a building part turns on its own while the building stands (#351): the HQ's fans spin, its radar turns and its wing
 * dish sweeps, about the part's pivot in its own frame. URTSBuildingMotionSubsystem drives it on every machine that draws;
 * ARTSBuilding::SetEquipmentMotionScale slows or stops it.
 */
USTRUCT(BlueprintType)
struct FRTSPartMotion
{
	GENERATED_BODY()

	/** Degrees a second the part turns about each of its own axes, on and on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FRotator SpinRate = FRotator::ZeroRotator;

	/** Degrees the part swings either way of its rest about each of its own axes, a sine over SweepPeriod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FRotator SweepAmplitude = FRotator::ZeroRotator;

	/** Seconds of one full swing of SweepAmplitude, there and back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0"))
	float SweepPeriod = 0.f;

	/** Where the part turns about, in cm in its own frame; zero at its origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FVector PivotOffset = FVector::ZeroVector;

	/** A part this one rides on, turning with it about that part's pivot as well as its own (the dish on its azimuth). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FName FollowPart;

	/**
	 * Each building runs the part up to this share faster or slower than its rates, picked from where it stands, and starts it
	 * at its own phase, so neighbouring buildings never turn in step.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0", ClampMax = "1"))
	float RateJitter = 0.f;

	bool IsAnimated() const
	{
		return !SpinRate.IsNearlyZero() || (!SweepAmplitude.IsNearlyZero() && SweepPeriod > 0.f);
	}
};

/**
 * One more mesh of a building made of several (the USA HQ, #191): drawn with the building's Mesh, rising with it during
 * construction and coloured with it, without collision of its own.
 */
USTRUCT(BlueprintType)
struct FRTSBuildingPart
{
	GENERATED_BODY()

	/** The part's name in the building's layout, such as DoorSection00 or Radar_Azimuth, for the parts that move. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TObjectPtr<UStaticMesh> Mesh;

	/** Where the part stands in the building's own frame, at rest, before MeshScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FTransform Transform;

	/** The part is animated (door, radar, fans); the others never move in the building. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	bool bMovable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	bool bCastShadow = true;

	/** Grows the part's render bounds, for one whose material moves it beyond its mesh (the flag's wind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "1"))
	float BoundsScale = 1.f;

	/** The part stops drawing farther than this from the camera, in cm; 0 to always draw it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "0"))
	float MaxDrawDistance = 0.f;

	/** Its material stops moving it (world position offset, the flag's wind) farther than this from the camera, in cm; 0 never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part", meta = (ClampMin = "0"))
	float WPODisableDistance = 0.f;

	/** How the part turns on its own (#351); none for a part that stands still or that the building poses (the door). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FRTSPartMotion Motion;
};

/** A box that blocks units and cuts the navmesh, in the building's own frame, before MeshScale. */
USTRUCT(BlueprintType)
struct FRTSBuildingCollisionBox
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FVector Min = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	FVector Max = FVector::ZeroVector;

	/**
	 * The garage bay a produced vehicle drives out of (URTSGarageComponent, #192). It stays solid and cuts the navmesh like
	 * the others: the vehicle leaving it drives a scripted lane, not a path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bGarage = false;
};

/** A named point of a building's layout in its own frame, such as the garage's spawn and exit. */
USTRUCT(BlueprintType)
struct FRTSBuildingSocket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	FVector Location = FVector::ZeroVector;
};

/**
 * A structure placed on the 100 cm build grid. An armed building (a tower) fires once construction completes; a building
 * listing Units or Upgrades (a barracks) produces or researches them.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSBuildingData : public URTSEntityData
{
	GENERATED_BODY()

public:
	/** Size in 100 cm grid cells along X and Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "1"))
	FIntPoint Footprint = FIntPoint(16, 16);

	/** Meshes drawn with Mesh, for a building modelled in several (#191). Empty for a building of one mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TArray<FRTSBuildingPart> Parts;

	/**
	 * The boxes that block units and cut the navmesh, around the building's blocks. Empty for one box filling the footprint
	 * up to the mesh's height.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	TArray<FRTSBuildingCollisionBox> CollisionBoxes;

	/** Named points of the building's layout, such as GarageSpawn and GarageExit (#192). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	TArray<FRTSBuildingSocket> Sockets;

	/** The flags there are (DA_FlagTypes), where FlagType is looked up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<URTSFlagTypes> FlagTypes;

	/**
	 * The flag the building flies, a key of FlagTypes such as USA (#351): its material goes on the part FlagPart, and that
	 * part's width becomes its height times the flag's aspect. None for a building without a flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FName FlagType;

	/** The part of Parts that is the flag's cloth, at the pole's hoist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FName FlagPart;

	/** The location of the socket Name, or null when the data has none of that name. */
	const FVector* FindSocket(FName Name) const
	{
		const FRTSBuildingSocket* Socket = Sockets.FindByPredicate([Name](const FRTSBuildingSocket& S) { return S.Name == Name; });
		return Socket ? &Socket->Location : nullptr;
	}

	/** Where a produced vehicle appears in the garage bay, facing out of the door (#192). */
	static inline const FName GarageSpawnSocket = TEXT("GarageSpawn");

	/** The garage door's opening on the floor, on the same lane as GarageSpawnSocket. */
	static inline const FName GarageExitSocket = TEXT("GarageExit");

	/** Above the door, where its warning lights hang. */
	static inline const FName GarageDoorPivotSocket = TEXT("GarageDoorPivot");

	/** A collision box is the garage bay and both garage sockets are there: produced vehicles leave through the garage. */
	bool HasGarage() const
	{
		return FindSocket(GarageSpawnSocket) && FindSocket(GarageExitSocket)
			&& CollisionBoxes.ContainsByPredicate([](const FRTSBuildingCollisionBox& Box) { return Box.bGarage; });
	}

	/**
	 * The garage door's motion as delivered (the HQ's CT_HQ20_DoorMotion): rows S00..S15 _X, _Z and _P for the door
	 * sections' place along the building's X and Z and their pitch, Cable_P and Cable_L for the lift cables' pitch and length
	 * scale, Drive_P for the drive shaft's turn, in cm and degrees over the cycle's seconds. Null for a door that does not move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garage")
	TObjectPtr<UCurveTable> GarageDoorMotion;

	/** The second of GarageDoorMotion at which the door starts to open, closed before it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garage", meta = (ClampMin = "0"))
	float GarageDoorOpenStart = 2.f;

	/** The second of GarageDoorMotion at which the door is open; closing plays the opening backward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garage", meta = (ClampMin = "0"))
	float GarageDoorOpenEnd = 10.f;

	/** Seconds the door takes in the game to open, and again to close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garage", meta = (ClampMin = "0.1"))
	float GarageDoorSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0"))
	int32 PowerOutput = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0"))
	int32 PowerDraw = 0;

	/** The weapon holds fire while the owner is low on power, as base defences do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bRequiresPower = false;

	/** Spawned once for the owner when construction completes, such as the harvester that comes with a supply depot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TObjectPtr<URTSUnitData> SpawnOnComplete;

	/** What the building produces once constructed, in command card order. Empty for a building without production. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TArray<TObjectPtr<URTSUnitData>> Units;

	/** What the building researches once constructed, in command card order after Units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	TArray<TObjectPtr<URTSUpgradeData>> Upgrades;

	/**
	 * How many of this building a team may own at once, construction sites included, such as one headquarters. 0 for no
	 * limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production", meta = (ClampMin = "0"))
	int32 MaxCount = 0;

	/**
	 * Pads for aircraft, side by side across the roof: each aircraft the building produces is bound to one and returns there
	 * to rearm, so it produces aircraft only while a pad is free. 0 for a building that is not an airfield.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production", meta = (ClampMin = "0"))
	int32 AirfieldPads = 0;

	/** Harvesters deliver supplies here once it is constructed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	bool bAcceptsSupply = false;

	/**
	 * A defensive structure, such as a tower. Defences do not keep a team in the match: a team is eliminated once every
	 * other building it has, construction sites included, is gone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Match")
	bool bIsDefense = false;

	/**
	 * Seconds a superweapon takes to charge once constructed, and again after each launch; the charge holds while the owner
	 * is low on power. 0 for a building that is not a superweapon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Superweapon", meta = (ClampMin = "0"))
	float SuperweaponChargeTime = 0.f;

	/** Seconds from launch until the strike lands at the target point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Superweapon", meta = (ClampMin = "0"))
	float SuperweaponDelay = 3.f;

	/** Ground distance from the target point, to a unit or to a building's footprint edge, within which the strike deals damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Superweapon", meta = (ClampMin = "0"))
	float SuperweaponRadius = 4000.f;

	/** Damage at the target point; it falls off linearly to SuperweaponEdgeDamage of it at the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Superweapon", meta = (ClampMin = "0"))
	float SuperweaponDamage = 1000.f;

	/** The share of SuperweaponDamage dealt right at the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Superweapon", meta = (ClampMin = "0", ClampMax = "1"))
	float SuperweaponEdgeDamage = 0.25f;
};
