#pragma once

#include "CoreMinimal.h"
#include "Data/RTSEntityData.h"
#include "RTSBuildingData.generated.h"

class UCurveTable;
class UStaticMesh;
class UTextureLightProfile;
class URTSFlagTypes;
class URTSKitPieceData;
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

UENUM(BlueprintType)
enum class ERTSBuildingLightType : uint8
{
	Point,
	Spot,
	/** A lit rectangle, SourceWidth by SourceHeight, facing along its X: a strip lamp or a lit window band. */
	Rect,
};

/** How a light of the shared building kit shines (docs/art/BUILDING_KIT.md, States). */
UENUM(BlueprintType)
enum class ERTSKitLightBehaviour : uint8
{
	Steady,
	Flash,
	Rotate,
};

/** A building's size class: how many real lights it may have (RTSKit::GetLightBudget, BUILDING_KIT.md, Light budgets). */
UENUM(BlueprintType)
enum class ERTSBuildingSize : uint8
{
	Small,
	Medium,
	Large,
};

/** What damage does to a kit piece's light (the state table of BUILDING_KIT.md). */
UENUM(BlueprintType)
enum class ERTSKitFaultResponse : uint8
{
	None,
	Flicker,
	Off,
};

/**
 * A real light of a building (#195), at one of its lamps: it only ever shines from a lamp or window the model has, never
 * from a point in the air. Non-shadow-casting and cosmetic; URTSBuildingLightSubsystem shows the steady ones in the dark,
 * URTSGarageComponent flashes the GarageBeacons.
 */
USTRUCT(BlueprintType)
struct FRTSBuildingLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FName Name;

	/**
	 * The lighting group it belongs to (Windows, Fixtures, Accents, GarageLamps, GarageBeacons), which switches it with the
	 * material glow of the same name, if any (ARTSBuilding::SetLightGroupOn). The light components carry it as the tag Light.<Group>.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FName Group;

	/** The lamp or window of the model it shines from (Art/USA/buildings/usa_hq/issue195/fixtures.json for the HQ). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FName Fixture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	ERTSBuildingLightType Type = ERTSBuildingLightType::Point;

	/** Where it stands and which way it shines (its X), in the building's own frame, before MeshScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float Candelas = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float Radius = 1000.f;

	/** Spot: the cone's half angles in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", ClampMax = "80"))
	float InnerConeAngle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", ClampMax = "80"))
	float OuterConeAngle = 44.f;

	/** Rect: the lit rectangle across its Y and Z, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float SourceWidth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float SourceHeight = 0.f;

	/**
	 * Rect: barn doors along its edges, their angle out of square in degrees (0 square to the rectangle, 88 open) and length
	 * in cm, so a lamp against a wall does not light through it: with no shadows, only they keep the light to its side.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", ClampMax = "88"))
	float BarnDoorAngle = 88.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float BarnDoorLength = 20.f;

	/** Its light distribution profile (IES, #440): a hot centre and a soft edge across its cone; none lights it evenly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	TObjectPtr<UTextureLightProfile> Profile;

	/** Point and Spot: the radius of the glowing source in cm, which softens its pool's edge (#440); 0 a point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float SourceRadius = 0.f;

	/** Point and Spot: the glowing source's length in cm along its Z, a tube (#456); 0 a ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0"))
	float SourceLength = 0.f;

	/** Steady for the building's own lights; a kit piece's light takes its piece's (RTSKit::Expand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	ERTSKitLightBehaviour Behaviour = ERTSKitLightBehaviour::Steady;
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

/** A mount's yes or no that falls back to its piece's default when it does not say. */
UENUM(BlueprintType)
enum class ERTSKitOverride : uint8
{
	Default,
	On,
	Off,
};

/**
 * A piece of the shared building kit mounted on a building (docs/art/BUILDING_KIT.md, Mounts): RTSKit::Expand turns it into
 * a part, its light and its lens's per-instance data. Setup fills it from the building recipe's "mounts".
 */
USTRUCT(BlueprintType)
struct FRTSKitMount
{
	GENERATED_BODY()

	/** The part's name in the building, as Parts names theirs (FlagPart, FindPart); also its light's Fixture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	TObjectPtr<URTSKitPieceData> Piece;

	/** A socket of the building (Sockets) the piece stands at; none to place it by Transform alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FName Socket;

	/** In the socket's frame, or the building's own without one, before MeshScale; its scale scales the piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FTransform Transform;

	/** The piece's mount point (URTSKitPieceData::MountPoints) that meets the building; none for its first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FName MountPoint;

	/** The faction variant of the piece's materials; none for the building's faction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FName Variant;

	/** Battery-backed: stays on, dimmed, on low power. Default takes the piece's bEmergencyDefault. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	ERTSKitOverride Emergency = ERTSKitOverride::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	bool bOverrideFaultResponse = false;

	/** What damage does to it, when bOverrideFaultResponse; else the piece's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (EditCondition = "bOverrideFaultResponse"))
	ERTSKitFaultResponse FaultResponse = ERTSKitFaultResponse::None;

	/** The lighting group of its light and lens; none for the piece's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FName Group;

	/** The colour of an indicator's glow (09, 14); white for every other piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount")
	FLinearColor IndicatorColor = FLinearColor::White;

	/** Scales its piece's brightness on this building alone (#453): its light, lens glow, glare and beam together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "0"))
	float Brightness = 1.f;
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

	/** The real lights at the building's lamps (#195). Empty for a building that only glows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TArray<FRTSBuildingLight> Lights;

	/** The shared kit's pieces on the building (RTSKit::Expand makes their parts and lights). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TArray<FRTSKitMount> Mounts;

	/**
	 * Beyond this many cm from the nearest camera the whole building draws its LOD1, shell, parts and kit pieces on the same
	 * frame (BUILDING_KIT.md, option C2, URTSBuildingLightSubsystem); 0 leaves each mesh's LOD to the renderer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual", meta = (ClampMin = "0"))
	float LODSwitchDistance = 0.f;

	/** Its size class, which sets how many real lights it may have (RTSKit::GetLightBudget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	ERTSBuildingSize Size = ERTSBuildingSize::Medium;

	/** The group of Lights the garage flashes while its door is not closed (URTSGarageComponent). */
	static inline const FName GarageBeaconsGroup = TEXT("GarageBeacons");

	/**
	 * The group of Lights inside the garage bay, on only while its door is not closed (URTSGarageComponent): a light casts no
	 * shadow, so behind the closed door it would light the apron through it.
	 */
	static inline const FName GarageLampsGroup = TEXT("GarageLamps");

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

	/** At or below this share of its health the building shows Damaged (URTSBuildingStateComponent, #196). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation", meta = (ClampMin = "0", ClampMax = "1"))
	float DamagedHealthFraction = 0.5f;

	/** At or below this share of its health the building shows Critical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation", meta = (ClampMin = "0", ClampMax = "1"))
	float CriticalHealthFraction = 0.25f;

	/**
	 * The rubble a destroyed building leaves (ARTSBuildingRuin), at the building's mesh placement. None draws the building's
	 * own meshes collapsed flat and charred in its place.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	TObjectPtr<UStaticMesh> RuinMesh;

	/** Seconds the building takes to collapse into rubble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation", meta = (ClampMin = "0.1"))
	float RuinCollapseSeconds = 3.f;

	/** Seconds the rubble lies before it sinks away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation", meta = (ClampMin = "0"))
	float RuinSeconds = 60.f;

	/** Seconds the rubble takes to sink into the ground and go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation", meta = (ClampMin = "0.1"))
	float RuinFadeSeconds = 5.f;

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

	/** The door's hinge line, above the opening. */
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
