#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSBuildingData.generated.h"

class UStaticMesh;
class URTSUnitData;
class URTSUpgradeData;

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

	/** The garage bay, which the garage sequence opens for a unit leaving it (#192). Solid for now. */
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

	/** The location of the socket Name, or null when the data has none of that name. */
	const FVector* FindSocket(FName Name) const
	{
		const FRTSBuildingSocket* Socket = Sockets.FindByPredicate([Name](const FRTSBuildingSocket& S) { return S.Name == Name; });
		return Socket ? &Socket->Location : nullptr;
	}

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
