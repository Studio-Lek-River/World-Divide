#include "Units/RTSUnit.h"

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSHarvesterData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSMoveDestination.h"
#include "Navigation/RTSMoveRequestSubsystem.h"
#include "Navigation/RTSNavAgents.h"
#include "Net/UnrealNetwork.h"
#include "Player/RTSPlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSApproach.h"
#include "Units/RTSBarrelClearance.h"
#include "Units/RTSDustSubsystem.h"
#include "Units/RTSExhaustSubsystem.h"
#include "Units/RTSGround.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSRunningGearSubsystem.h"
#include "Units/RTSVehicleLightSubsystem.h"
#include "Units/RTSUnitAIController.h"
#include "Units/RTSUnitMovement.h"
#include "WorldDivide.h"

namespace
{
	constexpr float MoveAcceptanceRadius = 25.f;

	/** Attackers stop this far inside their range so a slowly retreating target does not slip straight out again. */
	constexpr float AttackApproachFraction = 0.8f;

	/** Attackers head for a spot this fraction of their range outside a building's edge, well clear of its navmesh hole. */
	constexpr float BuildingApproachFraction = 0.5f;

	/** Units that cannot harvest or build stop this far outside the edge of a supply source or construction site they were sent to. */
	constexpr float DockStandOff = 200.f;

	/**
	 * A move target within this of the navmesh is on it; any further off, in a wall or a building, RTSMoveDestination picks
	 * the reachable spot nearest it. Narrower than a vehicle's query extent, so a click into a wall never lands on its far side.
	 */
	const FVector MoveProjectExtent(50.f, 50.f, 250.f);

	/**
	 * A unit holds short of a new building once this much of its travel time lies between it and the footprint, or a vehicle
	 * its braking distance and this margin, in cm.
	 */
	constexpr float HoldLookaheadSeconds = 0.2f;
	constexpr float HoldBrakingMargin = 100.f;

	/**
	 * A vehicle stuck against something this many seconds backs off a little and paths again; stuck as long again within the
	 * second span, it gives the move up.
	 */
	constexpr float BlockedReplanSeconds = 0.6f;
	/** The first time it is stuck it backs off for this many seconds before it follows its new path. */
	constexpr float BlockedBackOffSeconds = 0.4f;
	constexpr double BlockedGiveUpSeconds = 3.0;

	/** A vehicle slower than this (cm/s) stands, and may pivot toward its target. */
	constexpr float StandingSpeed = 10.f;

	/** A fire kick jolts the mesh back this many cm per unit of effect scale and eases it home over this long. */
	constexpr float FireKickPerScale = 8.f;
	constexpr float FireKickSeconds = 0.15f;
	/**
	 * The ground is looked for from this far above a unit's feet, enough for the steepest ramp it climbs in a frame, down to
	 * this far below them, enough for a unit spawned above the ground.
	 */
	constexpr float GroundTraceUp = 100.f;
	constexpr float GroundTraceDown = 2000.f;

	/** A unit that moved no further than this (cm) since its last ground trace stands on the same ground and skips it. */
	constexpr float GroundTraceSkipDistance = 1.f;

	/** A tilting vehicle eases its mesh toward the slope at this rate, so it rolls over a crest instead of snapping. */
	constexpr float TiltInterpSpeed = 10.f;

	/** A turret fires once it points this close to its target, in degrees. */
	constexpr float TurretAimTolerance = 5.f;

	/**
	 * A driving unit looks for obstacles in its barrel's way as far along its path as it drives in the time its turret takes
	 * to traverse this many degrees, between these many seconds.
	 */
	constexpr float BarrelLookAheadTurn = 90.f;
	constexpr float BarrelLookAheadMinSeconds = 0.5f;
	constexpr float BarrelLookAheadMaxSeconds = 2.f;

	/**
	 * A barrel whose reach was found clear of walls is looked at again after this many seconds, in which a building may have
	 * gone up; one near a wall is swept at most this often, or as soon as the aim it wants moves by this many degrees.
	 */
	TAutoConsoleVariable<bool> CVarBarrelClearance(
		TEXT("rts.BarrelClearance"), true,
		TEXT("Turns turrets and raises guns so barrels stay out of walls and buildings (RTSBarrelClearance), on the server."));

	constexpr double BarrelReachCheckSeconds = 1.0;
	constexpr double BarrelAimCheckSeconds = 0.2;
	constexpr float BarrelAimRecheckAngle = 2.5f;

	/**
	 * The overlap that finds what the navmesh leaves out looks at least this many cm past the barrel's reach, so a unit
	 * standing or creeping may move as far before it looks again.
	 */
	constexpr float BarrelReachMargin = 100.f;

	/** A turret that must turn the long way round heads for a point this many degrees on, so it never turns back the short way. */
	constexpr float BarrelLongWayStep = 170.f;

	/** Below this much driving (cm) and turning (degrees) since the last frame the wheels stand still. */
	constexpr float GearMinDistance = 0.01f;
	constexpr float GearMinTurn = 0.001f;

	/**
	 * A vehicle's gun recoils and its hull rocks (PlayGunRecoil) only within this many cm of a camera on this machine (#219).
	 * Past the M20's LOD2 switch at 110 m the rock is under a pixel and the barrel's stroke a pixel or two, while moving the
	 * hull and its parts cost 240 firing M20s about 1.8 ms of render thread and GPU a frame.
	 */
	TAutoConsoleVariable<float> CVarRecoilDistance(
		TEXT("rts.Vehicle.RecoilDistance"), 11000.f,
		TEXT("cm from this machine's camera past which a vehicle's gun does not recoil nor its hull rock (#219); 0 everywhere."));

	/** Scales every vehicle's body motion (FRTSBodyMotion, #297) for tuning it live: 0 keeps the bodies still, 2 doubles it. */
	TAutoConsoleVariable<float> CVarBodyMotion(
		TEXT("rts.Vehicle.BodyMotion"), 1.f,
		TEXT("Scales how far vehicles' hulls squat, dive and lean with their drive and their turrets trail a traverse (#297); 0 off."));

	/**
	 * Every part of a unit is movable, and the renderer gives a movable primitive its own hardware occlusion query in every
	 * frame it is on screen: about 1,500 at 240 M20s (#214). Lit as a group under the collision box, the parts take the
	 * occlusion that static scenery has: a part seen last frame is queried again in about one frame in ten, and parts hidden
	 * last frame share batched queries, so units behind buildings are still culled. Read when a unit is made.
	 */
	TAutoConsoleVariable<bool> CVarGroupPartOcclusion(
		TEXT("rts.Render.GroupPartOcclusion"), true,
		TEXT("Gives the parts of units made from now on the approximate occlusion queries of static scenery (#214)."));

	/** A cosmetic part on Parent, like Mesh and Turret: the hull box alone collides and is picked. */
	template <typename T>
	T* MakePartComponent(AActor& Owner, USceneComponent& Parent, UStaticMesh* PartMesh, bool bWorldPlaced = false)
	{
		T* Part = NewObject<T>(&Owner);
		Part->SetStaticMesh(PartMesh);
		// A world-placed part is attached for its visibility only and stands at the world's origin; its maker places it.
		Part->SetUsingAbsoluteLocation(bWorldPlaced);
		Part->SetUsingAbsoluteRotation(bWorldPlaced);
		Part->SetUsingAbsoluteScale(bWorldPlaced);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetupAttachment(&Parent);
		Owner.AddInstanceComponent(Part);
		Part->RegisterComponent();
		return Part;
	}
}

ARTSUnit::ARTSUnit()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->InitBoxExtent(FVector(60.f));
	// Units steer by the navmesh, which already routes around obstacles, so they don't sweep against the world:
	// a box resting on the ground counts as penetrating it and would block every move.
	// They overlap each other, URTSAvoidanceSubsystem pushes them apart, and they block mouse traces for picking.
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// Nothing listens for overlap events, and every move and avoidance push would pay for them.
	Collision->SetGenerateOverlapEvents(false);
	// Otherwise the navmesh grows on top of units and paths start on their roofs.
	Collision->SetCanEverAffectNavigation(false);
	// The box draws nothing; it only groups the parts' occlusion (see CVarGroupPartOcclusion).
	Collision->bLightAttachmentsAsGroup = CVarGroupPartOcclusion.GetValueOnAnyThread();
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	RunningGear = CreateDefaultSubobject<USceneComponent>(TEXT("RunningGear"));
	RunningGear->SetupAttachment(Collision);

	Turret = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Turret"));
	Turret->SetupAttachment(Mesh);
	Turret->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Turret->SetCanEverAffectNavigation(false);
	Turret->SetVisibility(false);

	Movement = CreateDefaultSubobject<URTSUnitMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	// No plane constraint: the path climbs and descends with the navmesh, and SnapToGround stands the unit on the ground.
	// Path following feeds acceleration input, so MaxSpeed and braking behave like player input would.
	Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;
	// ApplyUnitData picks the nav agent from the hull; the engine would size it to the box's diagonal instead.
	Movement->GetNavMovementProperties()->bUpdateNavAgentWithOwnersCollision = false;

	HealthComponent = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));
	WeaponComponent = CreateDefaultSubobject<URTSWeaponComponent>(TEXT("Weapon"));
	HarvesterComponent = CreateDefaultSubobject<URTSHarvesterComponent>(TEXT("Harvester"));
	BuilderComponent = CreateDefaultSubobject<URTSBuilderComponent>(TEXT("Builder"));
	AircraftComponent = CreateDefaultSubobject<URTSAircraftComponent>(TEXT("Aircraft"));

	AIControllerClass = ARTSUnitAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;

	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(30.f);
	// Every player sees every unit for now; fog of war will decide relevancy later.
	bAlwaysRelevant = true;
}

void ARTSUnit::InitFromData(URTSUnitData* InUnitData, int32 InTeamId)
{
	check(HasAuthority());
	UnitData = InUnitData;
	TeamId = InTeamId;
	HealthComponent->InitHealth(UnitData ? UnitData->MaxHealth : 1.f);
	WeaponComponent->SetTeamId(TeamId);
	WeaponComponent->SetWeaponData(UnitData ? UnitData->Weapon : nullptr);
	ApplyUpgrades();
	HarvesterComponent->SetHarvesterData(Cast<URTSHarvesterData>(UnitData));
	BuilderComponent->SetBuilderData(Cast<URTSBuilderData>(UnitData));
	AircraftComponent->SetAircraftData(Cast<URTSAircraftData>(UnitData));
	ApplyUnitData();
}

void ARTSUnit::ApplyUpgrades()
{
	check(HasAuthority());
	if (!UnitData)
	{
		return;
	}
	const UWorld& World = *GetWorld();
	HealthComponent->SetMaxHealth(UnitData->MaxHealth * ARTSPlayerState::GetStatMultiplierForTeam(World, TeamId, *UnitData, ERTSUpgradeStat::MaxHealth));
	WeaponComponent->SetDamageMultiplier(ARTSPlayerState::GetStatMultiplierForTeam(World, TeamId, *UnitData, ERTSUpgradeStat::WeaponDamage));
}

void ARTSUnit::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	HealthComponent->OnDamaged.AddDynamic(this, &ThisClass::HandleDamaged);
	HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
	// SnapToGround corrects this frame's move rather than the last one.
	AddTickPrerequisiteComponent(Movement);
}

void ARTSUnit::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	ARTSPlayerController::NotifyTeamAttacked(*this, TeamId, DamageInstigator, ERTSAttackAlert::Units);
	ARTSAIPlayer::NotifyUnitDamaged(*this, DamageInstigator);
}

void ARTSUnit::HandleDeath(AActor* Killer)
{
	// Selections hold weak pointers, so they drop the unit on their own.
	Destroy();
}

void ARTSUnit::Destroyed()
{
	// Buildings this unit was ordered to build but never placed go back to the player.
	if (HasAuthority())
	{
		ClearCommands();
		BuilderComponent->StopBuild();
		AircraftComponent->ReleasePad();
	}
	Super::Destroyed();
}

void ARTSUnit::OnRep_UnitData()
{
	ApplyUnitData();
}

void ARTSUnit::OnRep_TeamId()
{
	ApplyTeamColor();
}

void ARTSUnit::ApplyUnitData()
{
	if (!UnitData)
	{
		return;
	}

	Mesh->SetStaticMesh(UnitData->Mesh);
	Mesh->SetRelativeScale3D(UnitData->MeshScale);
	RunningGear->SetRelativeScale3D(UnitData->MeshScale);
	// The box is the hull, and the mesh, or the running gear below it, stands on its bottom.
	Shape = RTSUnitShape::FromData(*UnitData);
	Collision->SetBoxExtent(Shape.Hull.GetExtent());
	// A new ground offset: stand the unit on the ground again.
	GroundTraceLocation.Reset();
	MeshRestLocation = Shape.MeshOffset;
	UpdateMeshTransform();

	Turret->SetStaticMesh(UnitData->TurretMesh);
	Turret->SetVisibility(UnitData->TurretMesh != nullptr);
	Turret->SetRelativeLocationAndRotation(UnitData->TurretOffset, FRotator(0.f, TurretYaw, 0.f));
	CreateParts();

	Movement->MaxSpeed = UnitData->MoveSpeed;
	// The narrowest agent class wide enough for the hull: its navmesh keeps every path wide enough for the unit.
	Movement->NavAgentProps = RTSNavAgents::SelectAgent(Shape);
	// Tracks or wheels drive a vehicle (#138); infantry walks.
	Movement->ApplyUnitData(*UnitData, Shape.Hull.GetSize().X);
	HullClearanceCache = FRTSHullClearanceCache();
	BarrelReachCache = FRTSHullClearanceCache();
	bBarrelNearObstacle = false;
	bClearAimFound = false;

	ApplyTeamColor();
}

void ARTSUnit::CreateParts()
{
	for (UStaticMeshComponent* Part : PartComponents)
	{
		Part->DestroyComponent();
	}
	PartComponents.Reset();
	PartSlots.Reset();
	Wheels.Reset();
	WheelSets.Reset();
	Tracks.Reset();
	RecoilParts.Reset();
	GunRecoilStroke = 0.f;
	GunRecoilTime.Reset();
	GunRecoilOffset = 0.f;
	bGunRecoilHeld = false;
	GunJump.Reset();
	HullLift.Reset();
	HullSide.Reset();
	HullSquat.Reset();
	HullLean.Reset();
	TurretLag.Reset();
	BodySpeed.Reset();
	BodyAcceleration = 0.f;
	BodyTurretYaw.Reset();
	LastGearPose.Reset();
	GearHull.Reset();
	GearTime.Reset();
	bWheelsSettled = true;
	const FRTSRealizedMotion& Motion = Movement->GetRealizedMotion();
	GearDriven = Motion.DrivenDistance;
	GearTurned = Motion.TurnedRadians;
	GearLeft = Motion.LeftTravel;
	GearRight = Motion.RightTravel;
	if (Gun)
	{
		Gun->DestroyComponent();
		Gun = nullptr;
	}

	if (HasGun())
	{
		Gun = MakePartComponent<UStaticMeshComponent>(*this, *Turret, UnitData->GunMesh);
		Gun->SetRelativeLocationAndRotation(UnitData->GunOffset, FRotator(GunPitch, 0.f, 0.f));
	}

	const FTransform Hull = RunningGear->GetComponentTransform();
	TMap<UStaticMesh*, int32> SetOfMesh;
	for (const FRTSVehiclePart& Part : UnitData->Parts)
	{
		FPartSlot& Slot = PartSlots.AddDefaulted_GetRef();
		// The tracks and wheels stay on the ground while the hull above them sways (#333).
		const bool bRunningGear = Part.MountedOn == ERTSVehiclePartMount::Hull && (Part.bWheel || Part.TrackPitch > 0.f);
		USceneComponent* Parent = Part.MountedOn == ERTSVehiclePartMount::Gun ? Gun.Get()
			: Part.MountedOn == ERTSVehiclePartMount::Turret ? (HasTurret() ? Turret.Get() : nullptr)
			: bRunningGear ? RunningGear.Get() : Mesh.Get();
		if (!Part.Mesh || !Parent)
		{
			continue;
		}
		const FTransform Rest(Part.Rotation, Part.Offset);
		if (!Part.bWheel || Part.MountedOn != ERTSVehiclePartMount::Hull)
		{
			Slot.Component = MakePartComponent<UStaticMeshComponent>(*this, *Parent, Part.Mesh);
			Slot.Component->SetRelativeTransform(Rest);
			PartComponents.Add(Slot.Component);
			if (Part.bRecoils && Part.MountedOn == ERTSVehiclePartMount::Gun)
			{
				RecoilParts.Add({ Slot.Component, Part.Offset });
				const FBox Box = Part.Mesh->GetBoundingBox();
				GunRecoilStroke = FMath::Max(GunRecoilStroke, UnitData->GunRecoil.GetStroke(Box.Max.X - Box.Min.X));
			}
			if (Part.TrackPitch > 0.f && Part.MountedOn == ERTSVehiclePartMount::Hull)
			{
				AddTrack(Slot, Part);
			}
			continue;
		}

		// One draw for every wheel of a kind: each wheel is an instance. The set stays attached to the running gear, which it is
		// shown and hidden with, but it does not move with it: UpdateRunningGear writes every instance in world space each frame
		// the running gear moves or the wheels roll. Carried by the gear's move as well, the set would be updated twice (#165).
		int32* SetIndex = SetOfMesh.Find(Part.Mesh.Get());
		if (!SetIndex)
		{
			FWheelSet& NewSet = WheelSets.AddDefaulted_GetRef();
			NewSet.Component = MakePartComponent<UInstancedStaticMeshComponent>(*this, *Parent, Part.Mesh, /*bWorldPlaced*/ true);
			// The set itself never moves, so without each instance's last place the renderer sees the wheels stand still while
			// they drive and roll, and the temporal upscaler smears them (#243). Set before the instances, which it then keeps.
			NewSet.Component->SetHasPerInstancePrevTransforms(true);
			PartComponents.Add(NewSet.Component);
			SetIndex = &SetOfMesh.Add(Part.Mesh.Get(), WheelSets.Num() - 1);
		}
		FWheelSet& Set = WheelSets[*SetIndex];
		Slot.Component = Set.Component;
		Slot.Instance = Set.Component->AddInstance(Rest * Hull, /*bWorldSpace*/ false);
		Slot.Wheel = Wheels.Num();
		Set.Transforms.Add(Rest * Hull);
		Set.PrevTransforms.Add(Rest * Hull);

		FWheel& Wheel = Wheels.AddDefaulted_GetRef();
		Wheel.Set = *SetIndex;
		Wheel.Instance = Slot.Instance;
		Wheel.Rest = Rest;
		const float Radius = Part.RollRadius > 0.f ? Part.RollRadius : Part.Mesh->GetBoundingBox().GetExtent().Z;
		Wheel.Radius = FMath::Max(Radius * UnitData->MeshScale.Z, 1.f);
		Wheel.Side = Part.Offset.Y * UnitData->MeshScale.Y;
	}

	if (URTSRunningGearSubsystem* Gear = GetWorld() ? GetWorld()->GetSubsystem<URTSRunningGearSubsystem>() : nullptr)
	{
		if (Wheels.IsEmpty() && Tracks.IsEmpty())
		{
			Gear->Unregister(*this);
		}
		else
		{
			Gear->Register(*this);
		}
	}
	// The head- and rear lights (#223), on every machine that draws.
	if (URTSVehicleLightSubsystem* Lights = GetWorld() ? GetWorld()->GetSubsystem<URTSVehicleLightSubsystem>() : nullptr)
	{
		if (UnitData->Lights.HasLights())
		{
			Lights->Register(*this);
		}
		else
		{
			Lights->Unregister(*this);
		}
	}
	// The engine's exhaust (#222), on every machine that draws.
	if (URTSExhaustSubsystem* Exhaust = GetWorld() ? GetWorld()->GetSubsystem<URTSExhaustSubsystem>() : nullptr)
	{
		if (UnitData->Exhaust.HasExhaust())
		{
			Exhaust->Register(*this);
		}
		else
		{
			Exhaust->Unregister(*this);
		}
	}
	// The dust behind the tracks or wheels (#224), on every machine that draws.
	if (URTSDustSubsystem* Dust = GetWorld() ? GetWorld()->GetSubsystem<URTSDustSubsystem>() : nullptr)
	{
		if (UnitData->Dust.HasDust())
		{
			Dust->Register(*this);
		}
		else
		{
			Dust->Unregister(*this);
		}
	}
}

void ARTSUnit::AddTrack(FPartSlot& Slot, const FRTSVehiclePart& Part)
{
	UStaticMeshComponent& Component = *Slot.Component;
	if (!URTSRunningGearSubsystem::AnimatesTracks())
	{
		// Rigid: the same mesh in the material without the link shift, and no phase to keep.
		if (Part.RigidMaterial)
		{
			for (int32 Index = 0; Index < Component.GetNumMaterials(); ++Index)
			{
				Component.SetMaterial(Index, Part.RigidMaterial);
			}
		}
		return;
	}
	// The links' shift is too fine to see from afar: the renderer leaves it out past this distance, while UpdateRunningGear
	// keeps the phase counting.
	Component.SetWorldPositionOffsetDisableDistance(FMath::RoundToInt(URTSRunningGearSubsystem::GetTrackAnimationDistance()));
	Slot.Track = Tracks.Num();
	FTrack& Track = Tracks.AddDefaulted_GetRef();
	Track.Component = &Component;
	Track.Pitch = FMath::Max(Part.TrackPitch * UnitData->MeshScale.X, 0.1f);
	Track.Side = Part.Offset.Y * UnitData->MeshScale.Y;
}

bool ARTSUnit::GetTrackPhase(int32 PartIndex, float& OutPhase) const
{
	if (!PartSlots.IsValidIndex(PartIndex) || PartSlots[PartIndex].Track == INDEX_NONE)
	{
		return false;
	}
	OutPhase = Tracks[PartSlots[PartIndex].Track].Phase;
	return true;
}

bool ARTSUnit::GetTrackMaterialPhase(int32 PartIndex, float& OutPhase) const
{
	if (!PartSlots.IsValidIndex(PartIndex) || PartSlots[PartIndex].Track == INDEX_NONE)
	{
		return false;
	}
	// From the component's data itself, so it shows what reached the material.
	const TArray<float>& Data = Tracks[PartSlots[PartIndex].Track].Component->GetCustomPrimitiveData().Data;
	auto Read = [&Data](int32 Index) { return Data.IsValidIndex(Index) ? Data[Index] : 0.f; };
	FTrack Sent;
	Sent.SentPhase = Read(RTSPrimitiveData::TrackPhase);
	Sent.SentRate = Read(RTSPrimitiveData::TrackRate);
	Sent.SentStart = Read(RTSPrimitiveData::TrackStart);
	Sent.SentAcceleration = Read(RTSPrimitiveData::TrackAcceleration);
	OutPhase = FMath::Frac(GetSentPhase(Sent, static_cast<float>(GetWorld()->GetTimeSeconds())));
	return true;
}

float ARTSUnit::GetSentPhase(const FTrack& Track, float Now)
{
	// As vehicle_art.py's TRACK_PHASE_HLSL, in floats as the shader has them.
	const float Since = Now - Track.SentStart;
	return Track.SentPhase + Since * (Track.SentRate + 0.5f * Track.SentAcceleration * Since);
}

void ARTSUnit::GetTrackComponents(TArray<const UStaticMeshComponent*, TInlineAllocator<2>>& OutComponents) const
{
	OutComponents.Reset();
	for (int32 Index = 0; UnitData && Index < PartSlots.Num() && Index < UnitData->Parts.Num(); ++Index)
	{
		const FRTSVehiclePart& Part = UnitData->Parts[Index];
		if (Part.TrackPitch > 0.f && Part.MountedOn == ERTSVehiclePartMount::Hull && PartSlots[Index].Component)
		{
			OutComponents.Add(PartSlots[Index].Component);
		}
	}
}

bool ARTSUnit::GetWheelAngle(int32 PartIndex, float& OutRadians) const
{
	if (!PartSlots.IsValidIndex(PartIndex) || PartSlots[PartIndex].Wheel == INDEX_NONE)
	{
		return false;
	}
	OutRadians = Wheels[PartSlots[PartIndex].Wheel].Angle;
	return true;
}

FTransform ARTSUnit::GetWheelTransform(const FWheel& Wheel) const
{
	// Rolling forward brings the top of the wheel forward: a nose-down pitch about the hull's Y axis, after the wheel's own
	// turn on the hull.
	const FQuat Roll = FRotator(-FMath::RadiansToDegrees(Wheel.Angle), 0.f, 0.f).Quaternion();
	return FTransform(Roll * Wheel.Rest.GetRotation(), Wheel.Rest.GetLocation()) * RunningGear->GetComponentTransform();
}

bool ARTSUnit::GetPartTransform(int32 PartIndex, FTransform& OutTransform) const
{
	if (!PartSlots.IsValidIndex(PartIndex) || !PartSlots[PartIndex].Component)
	{
		return false;
	}
	const FPartSlot& Slot = PartSlots[PartIndex];
	// A wheel from the hull as it stands now: its instance follows only once UpdateRunningGear has run this frame.
	OutTransform = Slot.Wheel == INDEX_NONE ? Slot.Component->GetComponentTransform() : GetWheelTransform(Wheels[Slot.Wheel]);
	return true;
}

int32 ARTSUnit::UpdateRunningGear(const FRTSTrackViews* Views)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ARTSUnit::UpdateRunningGear);

	if (Wheels.IsEmpty() && Tracks.IsEmpty())
	{
		return 0;
	}
	float Driven = 0.f;
	float TurnedRadians = 0.f;
	// A tracked vehicle's wheels and tracks go by the realized travel of their side, the same numbers for both, so the
	// links, the road wheels on them and the sprocket and idler they wrap move as one.
	TOptional<FVector2f> SideTravel;
	if (Movement->UsesLocomotion())
	{
		// A vehicle's wheels roll with what its gear drove, never with a push, a correction or a teleport.
		const FRTSRealizedMotion& Motion = Movement->GetRealizedMotion();
		Driven = static_cast<float>(Motion.DrivenDistance - GearDriven);
		TurnedRadians = static_cast<float>(Motion.TurnedRadians - GearTurned);
		if (Movement->GetLimits().IsTracked())
		{
			SideTravel = FVector2f(static_cast<float>(Motion.LeftTravel - GearLeft), static_cast<float>(Motion.RightTravel - GearRight));
		}
		GearDriven = Motion.DrivenDistance;
		GearTurned = Motion.TurnedRadians;
		GearLeft = Motion.LeftTravel;
		GearRight = Motion.RightTravel;
	}
	else
	{
		// The first call only notes where the unit stands.
		const FTransform Pose = GetActorTransform();
		if (LastGearPose.IsSet())
		{
			Driven = FVector::DotProduct(Pose.GetLocation() - LastGearPose->GetLocation(), Pose.GetUnitAxis(EAxis::X));
			TurnedRadians = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LastGearPose->Rotator().Yaw, Pose.Rotator().Yaw));
		}
		LastGearPose = Pose;
	}
	const bool bRolls = FMath::Abs(Driven) >= GearMinDistance || FMath::Abs(FMath::RadiansToDegrees(TurnedRadians)) >= GearMinTurn;
	// Turning right (positive yaw) drives the left side (negative Y) forward and the right side back.
	auto TravelOf = [&SideTravel, Driven, TurnedRadians](float Side)
	{
		return SideTravel ? (Side < 0.f ? SideTravel->X : SideTravel->Y) : Driven - Side * TurnedRadians;
	};

	// The material counts the phase on from a link rate and a start time by the scene's time, which is the world's (#216).
	const double Now = GetWorld()->GetTimeSeconds();
	const float Seconds = GearTime ? static_cast<float>(Now - *GearTime) : 0.f;
	GearTime = Now;
	int32 Sent = 0;
	const float NowSeconds = static_cast<float>(Now);
	for (FTrack& Track : Tracks)
	{
		const float LastRate = Track.Rate;
		Track.Rate = 0.f;
		Track.Acceleration = 0.f;
		if (bRolls)
		{
			// Links of travel along the ground. The phase counts through one link only, and goes down driving forward.
			const float Links = TravelOf(Track.Side) / Track.Pitch;
			Track.Phase = FMath::Frac(Track.Phase - FMath::Frac(Links));
			if (Seconds > 0.f)
			{
				Track.Rate = -Links / Seconds;
				Track.Acceleration = (Track.Rate - LastRate) / Seconds;
			}
		}
		// The material's phase against the travel's, wrapped to [-0.5, 0.5) of a link, and its rate over the last frame against
		// the travel's. Standing, the material has the phase exactly and no motion, so its links stand without smearing;
		// rolling, it may drift by TrackDriftLimit before the phase is sent again, and its rate by TrackRateLimit a frame.
		const float Since = NowSeconds - Track.SentStart;
		const float Drift = FMath::Frac(Track.Phase - GetSentPhase(Track, NowSeconds) + 0.5f) - 0.5f;
		const float SentRate = Track.SentRate + Track.SentAcceleration * (Since - 0.5f * Seconds);
		const bool bStale = Track.Rate == 0.f
			? Track.SentRate != 0.f || Track.SentAcceleration != 0.f || Drift != 0.f
			: FMath::Abs(Drift) > TrackDriftLimit || FMath::Abs(Track.Rate - SentRate) * FMath::Max(Seconds, 1.f / 60.f) > TrackRateLimit;
		if (bStale && IsTrackDrawn(Track, Views))
		{
			// From the phase as it is: the last frame's rate is the one half a frame ago, so the rate now is half a frame of
			// acceleration on. Custom primitive data reaches the renderer as an update of the primitive's data: no render state
			// rebuild and no move (#165).
			static_assert(RTSPrimitiveData::TrackRate == RTSPrimitiveData::TrackPhase + 1 && RTSPrimitiveData::TrackStart == RTSPrimitiveData::TrackPhase + 2
				&& RTSPrimitiveData::TrackAcceleration == RTSPrimitiveData::TrackPhase + 3);
			Track.SentPhase = Track.Phase;
			Track.SentRate = Track.Rate + 0.5f * Track.Acceleration * Seconds;
			Track.SentStart = NowSeconds;
			Track.SentAcceleration = Track.Acceleration;
			Track.Component->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TrackPhase,
				FVector4(Track.SentPhase, Track.SentRate, Track.SentStart, Track.SentAcceleration));
			++Sent;
		}
	}

	// The wheels are placed in world space, so they follow every move of the running gear, rolled or not: a push, a tilt, a
	// kick, a correction from the server. The hull's rock and sway leave them on the ground (#333).
	const FTransform& Hull = RunningGear->GetComponentTransform();
	const bool bMoved = bRolls || !GearHull.IsSet() || !GearHull->Equals(Hull);
	if (Wheels.IsEmpty() || (!bMoved && bWheelsSettled))
	{
		return Sent;
	}
	// Last frame's places, for the motion vectors. Stopped, once more with the places as they are, so the wheels do not smear
	// as if still running.
	for (FWheelSet& Set : WheelSets)
	{
		Set.PrevTransforms = Set.Transforms;
	}
	bWheelsSettled = !bMoved;

	if (bMoved)
	{
		GearHull = Hull;
		for (FWheel& Wheel : Wheels)
		{
			if (bRolls)
			{
				Wheel.Angle = FMath::Fmod(Wheel.Angle + TravelOf(Wheel.Side) / Wheel.Radius, UE_TWO_PI);
				if (Wheel.Angle < 0.f)
				{
					Wheel.Angle += UE_TWO_PI;
				}
			}
			WheelSets[Wheel.Set].Transforms[Wheel.Instance] = GetWheelTransform(Wheel);
		}
	}
	for (const FWheelSet& Set : WheelSets)
	{
		// Without a render state rebuild: the set's instance data goes to the renderer as a change of transforms, where
		// rebuilding the proxy of every set of every vehicle each frame cost more than the move itself (#165).
		Set.Component->BatchUpdateInstancesTransforms(0, Set.Transforms, Set.PrevTransforms, /*bWorldSpace*/ false,
			/*bMarkRenderStateDirty*/ false);
		Set.Component->UpdateBounds();
		Set.Component->MarkRenderTransformDirty();
	}
	return Sent;
}

bool ARTSUnit::IsTrackDrawn(const FTrack& Track, const FRTSTrackViews* Views) const
{
	if (!Views)
	{
		return true;
	}
	if (!Views->bDraws)
	{
		return false;
	}
	if (Views->Locations.IsEmpty())
	{
		return true;
	}
	const FVector Location = Track.Component->GetComponentLocation();
	const FBoxSphereBounds& Bounds = Track.Component->Bounds;
	for (int32 Index = 0; Index < Views->Locations.Num(); ++Index)
	{
		const FVector& View = Views->Locations[Index];
		if (Views->Range > 0.f && FVector::DistSquared(View, Location) > FMath::Square(Views->Range))
		{
			continue;
		}
		if (!Views->Directions.IsValidIndex(Index) || !Views->HalfAngles.IsValidIndex(Index))
		{
			return true;
		}
		// Within the view's cone, widened by the bounds and the margin: a track off screen keeps its phase to itself (#212).
		const FVector ToTrack = Bounds.Origin - View;
		const double Distance = ToTrack.Size();
		const double Radius = Bounds.SphereRadius + Views->Margin;
		if (Distance <= Radius)
		{
			return true;
		}
		const double Angle = FMath::Acos(FMath::Clamp(ToTrack.Dot(Views->Directions[Index]) / Distance, -1.0, 1.0));
		if (Angle <= Views->HalfAngles[Index] + FMath::Asin(Radius / Distance))
		{
			return true;
		}
	}
	return false;
}

void ARTSUnit::ApplyTeamColor()
{
	// As custom primitive data, not a dynamic material instance per slot: every unit keeps the meshes' shared material
	// instances, so the same part of every unit draws together whatever its team (#150). The track phase (0, 1) is left
	// as it is.
	const FVector4 Color(GetTeamColor(TeamId));
	TArray<UStaticMeshComponent*, TInlineAllocator<8>> Parts = { Mesh.Get(), Turret.Get(), Gun.Get() };
	Parts.Append(PartComponents);
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part && Part->GetStaticMesh())
		{
			Part->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, Color);
		}
	}
}

void ARTSUnit::SetLampsOn(bool bOn)
{
	// As the team colour: custom primitive data, so every unit keeps the lamps' shared material and draws with the others.
	TArray<UStaticMeshComponent*, TInlineAllocator<8>> Parts = { Mesh.Get(), Turret.Get(), Gun.Get() };
	Parts.Append(PartComponents);
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part && Part->GetStaticMesh())
		{
			Part->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LampOn, bOn ? 1.f : 0.f);
		}
	}
}

bool ARTSUnit::HasTurret() const
{
	return UnitData && UnitData->TurretMesh;
}

bool ARTSUnit::HasGun() const
{
	return HasTurret() && UnitData->GunMesh;
}

void ARTSUnit::SetAimOverride(TOptional<FVector2D> YawPitch)
{
	check(HasAuthority());
	AimOverride = YawPitch;
}

float ARTSUnit::GetTurretYawToward(const AActor& Target) const
{
	return GetTurretYawToward(Target.GetActorLocation());
}

float ARTSUnit::GetTurretYawToward(const FVector& Point) const
{
	const FVector ToTarget = Point - GetActorLocation();
	return FRotator::NormalizeAxis(ToTarget.Rotation().Yaw - GetActorRotation().Yaw);
}

void ARTSUnit::UpdateTurret(float DeltaSeconds)
{
	if (!HasTurret())
	{
		return;
	}
	const TOptional<FVector> AimPoint = WeaponComponent->GetAimPoint();
	float Goal = AimOverride ? AimOverride->X : AimPoint ? GetTurretYawToward(*AimPoint) : 0.f;
	// Up toward the target's middle, or the ordered point on the ground, from the trunnion; the hull's tilt is left out.
	float PitchGoal = 0.f;
	if (AimOverride)
	{
		PitchGoal = AimOverride->Y;
	}
	else if (AimPoint && Gun)
	{
		const FVector ToTarget = *AimPoint - Gun->GetComponentLocation();
		PitchGoal = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Z, ToTarget.Size2D()));
	}
	PitchGoal = HasGun() ? FMath::Clamp(PitchGoal, UnitData->GunMinPitch, UnitData->GunMaxPitch) : 0.f;
	// An inspection aim is held as given.
	if (!AimOverride)
	{
		KeepBarrelClear(Goal, PitchGoal, AimPoint.IsSet());
	}

	const float Yaw = FMath::FixedTurn(TurretYaw, Goal, UnitData->TurretTurnRate * DeltaSeconds);
	if (Yaw != TurretYaw)
	{
		TurretYaw = FRotator::NormalizeAxis(Yaw);
		OnRep_TurretYaw();
	}

	if (!HasGun())
	{
		return;
	}
	const float Pitch = FMath::FixedTurn(GunPitch, PitchGoal, UnitData->GunElevationRate * DeltaSeconds);
	if (Pitch != GunPitch)
	{
		// FixedTurn gives [0, 360): a lowered gun is stored below zero, as its limits and KeepBarrelClear compare it.
		GunPitch = FRotator::NormalizeAxis(Pitch);
		OnRep_GunPitch();
	}
}

void ARTSUnit::KeepBarrelClear(float& Yaw, float& Pitch, bool bHasTarget)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ARTSUnit::KeepBarrelClear);

	bAimBlocked = false;
	if (!Shape.Barrel || !CVarBarrelClearance.GetValueOnGameThread())
	{
		bBarrelNearObstacle = false;
		return;
	}
	const FRTSBarrel& Barrel = *Shape.Barrel;
	const UWorld& World = *GetWorld();
	const double Now = World.GetTimeSeconds();
	// Far enough ahead to traverse a quarter turn before the unit gets there.
	const float LookAheadSeconds = FMath::Clamp(BarrelLookAheadTurn / UnitData->TurretTurnRate, BarrelLookAheadMinSeconds, BarrelLookAheadMaxSeconds);
	const float LookAhead = Movement->Velocity.Size2D() * LookAheadSeconds;

	// The ground the barrel can sweep at any yaw, a disc round the unit: only a wall of the navmesh near it, or something
	// solid the navmesh leaves out, calls for sweeps.
	FRTSFootprint Reach;
	Reach.Center = GetActorLocation();
	Reach.Radius = Barrel.GetReach();
	if (BarrelReachCache.IsClear(Reach, Now))
	{
		bBarrelNearObstacle = false;
	}
	else if (Now >= NextBarrelReachCheck)
	{
		// Without a navmesh to tell where the walls are, the sweeps alone decide.
		const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(World, GetNavAgentPropertiesRef());
		const float Clearance = NavMesh ? RTSHullClearance::GetClearance(*NavMesh, Reach, nullptr, LookAhead) : -1.f;
		bBarrelNearObstacle = Clearance <= LookAhead;
		float Slack = Clearance - LookAhead;
		if (!bBarrelNearObstacle)
		{
			// A prop or wall left out of navigation stands on the navmesh without cutting it: one overlap at the barrel's height,
			// wide enough for what the unit drives before the check expires, so a moving unit looks no more often than one standing.
			const float Margin = FMath::Max(BarrelReachMargin, Movement->Velocity.Size2D() * static_cast<float>(BarrelReachCheckSeconds));
			bBarrelNearObstacle = RTSBarrelClearance::IsAnythingInReach(World, Barrel, GetActorTransform(),
				HasGun() ? UnitData->GunMinPitch : 0.f, HasGun() ? UnitData->GunMaxPitch : 0.f, Margin);
			Slack = FMath::Min(Slack, Margin);
		}
		BarrelReachCache.Center = FVector2D(Reach.Center);
		BarrelReachCache.Slack = bBarrelNearObstacle ? -1.f : Slack;
		// Spread over a quarter second either way per unit, so units spawned together do not all look on the same frame.
		BarrelReachCache.ExpireTime = Now + BarrelReachCheckSeconds * (0.75 + 0.5 * (GetUniqueID() % 16) / 16.0);
		NextBarrelReachCheck = Now + BarrelAimCheckSeconds;
	}
	if (!bBarrelNearObstacle)
	{
		bClearAimFound = false;
		return;
	}

	const FVector2D Wanted(Yaw, Pitch);
	if (Now >= NextBarrelAimCheck || FMath::Abs(FMath::FindDeltaAngleDegrees(CheckedAim.X, Wanted.X)) > BarrelAimRecheckAngle
		|| FMath::Abs(CheckedAim.Y - Wanted.Y) > BarrelAimRecheckAngle)
	{
		// Where the unit stands, and where its path takes it before the turret could turn: the turret is round in time.
		TArray<FTransform, TInlineAllocator<2>> Poses = { GetActorTransform() };
		if (LookAhead > 1.f)
		{
			Poses.Add(GetPoseAlongPath(LookAhead));
		}
		const RTSBarrelClearance::FAim Aim = RTSBarrelClearance::FindClearAim(World, Barrel, Poses, Yaw, Pitch, TurretYaw,
			HasGun() ? UnitData->GunMaxPitch : 0.f);
		CheckedAim = Wanted;
		ClearAim = FVector2D(Aim.Yaw, Aim.Pitch);
		bClearAimFound = Aim.bFound && RouteToClearAim(ClearAim);
		NextBarrelAimCheck = Now + BarrelAimCheckSeconds;
	}
	if (bClearAimFound)
	{
		// The aim wanted at the check was clear: follow the aim wanted now, which may have moved a little since.
		if (!ClearAim.Equals(CheckedAim, UE_KINDA_SMALL_NUMBER))
		{
			Yaw = ClearAim.X;
			Pitch = ClearAim.Y;
		}
	}
	else
	{
		// Nowhere clear: hold still rather than sweep through the obstacle.
		Yaw = TurretYaw;
		Pitch = GunPitch;
	}
	bAimBlocked = bHasTarget && (!bClearAimFound || FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, Wanted.X)) > TurretAimTolerance);
}

bool ARTSUnit::RouteToClearAim(FVector2D& Aim) const
{
	const UWorld& World = *GetWorld();
	const FRTSBarrel& Barrel = *Shape.Barrel;
	const FTransform Pose = GetActorTransform();
	// A barrel already in an obstacle (a building went up around it) takes the short way out.
	if (!RTSBarrelClearance::IsClearAt(World, Barrel, Pose, TurretYaw, GunPitch))
	{
		return true;
	}
	// FMath::FixedTurn takes the short way; the long way is round the back.
	const float Delta = FMath::FindDeltaAngleDegrees(TurretYaw, Aim.X);
	const float Short = Delta >= 0.f ? 1.f : -1.f;
	for (const float Direction : { Short, -Short })
	{
		if (!RTSBarrelClearance::IsSwingClear(World, Barrel, Pose, TurretYaw, Aim.X, Direction, Aim.Y))
		{
			continue;
		}
		// The gun moves while the turret turns: clear at both ends of its way, or raised (or lowered) first.
		if (!FMath::IsNearlyEqual(GunPitch, Aim.Y, 0.1f)
			&& !RTSBarrelClearance::IsSwingClear(World, Barrel, Pose, TurretYaw, Aim.X, Direction, GunPitch))
		{
			Aim.X = TurretYaw;
		}
		else if (Direction != Short)
		{
			// The long way is half a turn or more: a point short of half a turn on is still the short way to FixedTurn.
			Aim.X = FRotator::NormalizeAxis(TurretYaw - Short * BarrelLongWayStep);
		}
		return true;
	}
	return false;
}

FTransform ARTSUnit::GetPoseAlongPath(float Distance) const
{
	FVector Location = GetActorLocation();
	FVector Heading = GetActorForwardVector().GetSafeNormal2D();
	const AAIController* AI = GetController<AAIController>();
	const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = Follow && Follow->GetStatus() == EPathFollowingStatus::Moving ? Follow->GetPath() : nullptr;
	if (Path.IsValid() && Path->IsValid())
	{
		const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
		for (int32 Index = Follow->GetNextPathIndex(); Index < Points.Num() && Distance > 0.f; ++Index)
		{
			// The path lies on the ground; the unit's origin stays at its own height above it.
			const FVector Point(Points[Index].Location.X, Points[Index].Location.Y, Location.Z);
			const float Leg = FVector::Dist2D(Location, Point);
			if (Leg > UE_KINDA_SMALL_NUMBER)
			{
				Heading = (Point - Location).GetSafeNormal2D();
			}
			const float Step = FMath::Min(Leg, Distance);
			Location += Heading * Step;
			Distance -= Step;
		}
	}
	return FTransform(Heading.Rotation(), Location);
}

bool ARTSUnit::IsBarrelClearAt(const FTransform& Pose) const
{
	return !Shape.Barrel || RTSBarrelClearance::IsClearAt(*GetWorld(), *Shape.Barrel, Pose, TurretYaw, GunPitch);
}

bool ARTSUnit::GetBarrelSegment(FVector& OutStart, FVector& OutEnd) const
{
	if (!HasTurret())
	{
		return false;
	}
	OutStart = Gun ? Gun->GetComponentLocation()
		: Turret->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, UnitData->MuzzleOffset.Z));
	OutEnd = GetMuzzleLocation();
	return true;
}

void ARTSUnit::OnRep_TurretYaw()
{
	PlaceTurret();
}

void ARTSUnit::PlaceTurret()
{
	Turret->SetRelativeRotation(FRotator(0.f, TurretYaw + TurretLag.GetAngle(), 0.f));
}

void ARTSUnit::OnRep_GunPitch()
{
	PlaceGun();
}

bool ARTSUnit::IsAimedAt(const AActor& Target) const
{
	return IsAimedAt(Target.GetActorLocation());
}

bool ARTSUnit::IsAimedAt(const FVector& Point) const
{
	// A barrel that cannot point there without reaching into an obstacle waits, even where it passes the target on its way.
	return !HasTurret() || (!bAimBlocked && FMath::Abs(FMath::FindDeltaAngleDegrees(TurretYaw, GetTurretYawToward(Point))) <= TurretAimTolerance);
}

const FName ARTSUnit::MuzzleSocketName(TEXT("Muzzle"));

FVector ARTSUnit::GetMuzzleLocation() const
{
	return GetMuzzleTransform().GetLocation();
}

FTransform ARTSUnit::GetMuzzleTransform() const
{
	if (Gun)
	{
		if (Gun->DoesSocketExist(MuzzleSocketName))
		{
			FTransform Socket = Gun->GetSocketTransform(MuzzleSocketName);
			Socket.SetScale3D(FVector::OneVector);
			return Socket;
		}
		const FTransform& GunTransform = Gun->GetComponentTransform();
		return FTransform(GunTransform.GetRotation(), GunTransform.TransformPosition(UnitData->GunMuzzleOffset));
	}
	const FTransform& TurretTransform = Turret->GetComponentTransform();
	return FTransform(TurretTransform.GetRotation(),
		TurretTransform.TransformPosition(UnitData ? UnitData->MuzzleOffset : FVector::ZeroVector));
}

FLinearColor ARTSUnit::GetTeamColor(int32 InTeamId)
{
	static const FLinearColor Colors[] = {
		FLinearColor(0.05f, 0.25f, 0.9f),
		FLinearColor(0.85f, 0.08f, 0.05f),
		FLinearColor(0.1f, 0.7f, 0.15f),
		FLinearColor(0.9f, 0.75f, 0.05f),
		FLinearColor(0.95f, 0.4f, 0.02f),
		FLinearColor(0.5f, 0.1f, 0.8f),
	};
	return Colors[FMath::Abs(InTeamId) % UE_ARRAY_COUNT(Colors)];
}

const FRTSRealizedMotion& ARTSUnit::GetRealizedMotion() const
{
	return Movement->GetRealizedMotion();
}

void ARTSUnit::SetLocomotionNet(const FRTSLocomotionNet& Net)
{
	if (!(LocomotionNet == Net))
	{
		LocomotionNet = Net;
	}
}

float ARTSUnit::GetSelectionRadius() const
{
	return UnitData ? UnitData->SelectionRadius : Collision->GetUnscaledBoxExtent().X;
}

float ARTSUnit::GetStandOffRadius() const
{
	const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*GetWorld(), GetNavAgentPropertiesRef());
	return FMath::Max(GetSelectionRadius(), NavMesh ? RTSHullClearance::GetErosion(*NavMesh) : 0.f);
}

FVector ARTSUnit::GetSelectionCenter() const
{
	return GetActorTransform().TransformPosition(Shape.Selection.GetCenter());
}

FRTSFootprint ARTSUnit::GetFootprint() const
{
	return RTSUnitShape::MakeFootprint(Shape, GetActorTransform());
}

bool ARTSUnit::IsAirborne() const
{
	return AircraftComponent->IsAirborne();
}

FVector ARTSUnit::GetClosestFootprintPoint(const FVector& From) const
{
	return GetFootprint().GetClosestEdgePoint(From);
}

float ARTSUnit::GetGroundOffset() const
{
	return Collision->GetScaledBoxExtent().Z;
}

FVector ARTSUnit::GetNavAgentLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, GetGroundOffset());
}

void ARTSUnit::PlayFireKick(float EffectScale)
{
	// A hull that rocks back on its suspension (PlayGunRecoil) does not also slide back.
	if (UnitData && UnitData->GunRecoil.HullRockDegrees > 0.f)
	{
		return;
	}
	FireKickDistance = FireKickPerScale * EffectScale;
	FireKickTimeLeft = FireKickSeconds;
	UpdateMeshTransform();
}

bool ARTSUnit::UpdateFireKick(float DeltaSeconds)
{
	if (FireKickTimeLeft <= 0.f)
	{
		return false;
	}
	FireKickTimeLeft = FMath::Max(FireKickTimeLeft - DeltaSeconds, 0.f);
	return true;
}

void ARTSUnit::PlayGunRecoil()
{
	if (!UnitData || bGunRecoilHeld)
	{
		return;
	}
	// Nobody sees a tank off screen or far away recoil, and moving its hull and parts for a second is most of what the recoil
	// costs. A machine that never renders (tests under -nullrhi) has no frames or views to go by and plays it all.
	if (FApp::CanEverRender() && (!Mesh->WasRecentlyRendered() || !IsRecoilSeen(GetLocalViewLocations())))
	{
		return;
	}
	const FRTSGunRecoil& Recoil = UnitData->GunRecoil;
	if (GunRecoilStroke > 0.f)
	{
		GunRecoilFrom = GunRecoilOffset;
		GunRecoilTime = 0.f;
	}
	if (Gun && Recoil.JumpDegrees > 0.f)
	{
		GunJump.Kick(Recoil.JumpDegrees, Recoil.JumpSeconds, Recoil.JumpDamping);
	}
	if (Recoil.HullRockDegrees > 0.f)
	{
		// The shot pushes back along the bore above the suspension: the hull lifts on the side the gun points to.
		float Side = 0.f;
		float Ahead = 1.f;
		FMath::SinCos(&Side, &Ahead, FMath::DegreesToRadians(TurretYaw));
		HullLift.Kick(Recoil.HullRockDegrees * Ahead, Recoil.HullRockSeconds, Recoil.HullRockDamping);
		HullSide.Kick(Recoil.HullRockDegrees * Side, Recoil.HullRockSeconds, Recoil.HullRockDamping);
	}
	UpdateGunRecoil(0.f);
}

bool ARTSUnit::IsRecoilSeen(TConstArrayView<FVector> ViewLocations) const
{
	const float Distance = CVarRecoilDistance.GetValueOnGameThread();
	if (Distance <= 0.f)
	{
		return true;
	}
	const FVector Location = GetActorLocation();
	return ViewLocations.ContainsByPredicate([&Location, Distance](const FVector& View)
	{
		return FVector::DistSquared(View, Location) <= FMath::Square(Distance);
	});
}

TArray<FVector, TInlineAllocator<2>> ARTSUnit::GetLocalViewLocations() const
{
	TArray<FVector, TInlineAllocator<2>> Locations;
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Player = It->Get();
			if (Player && Player->IsLocalController() && Player->PlayerCameraManager)
			{
				Locations.Add(Player->PlayerCameraManager->GetCameraLocation());
			}
		}
	}
	return Locations;
}

bool ARTSUnit::ShowsBodyMotion(ENetMode NetMode, TConstArrayView<FVector> ViewLocations) const
{
	return NetMode != NM_DedicatedServer && IsRecoilSeen(ViewLocations);
}

bool ARTSUnit::UpdateBodyMotion(float DeltaSeconds)
{
	if (!UnitData || !UnitData->BodyMotion.IsEnabled() || !Movement->UsesLocomotion())
	{
		return false;
	}
	const FRTSRealizedMotion& Motion = Movement->GetRealizedMotion();
	// Standing with the body at rest and the turret still, nothing moves: no need to ask after the cameras.
	const bool bStill = !IsBodyMoving() && Motion.ForwardSpeed == 0.f && Motion.YawRate == 0.f && BodyAcceleration == 0.f
		&& BodyTurretYaw && *BodyTurretYaw == TurretYaw;
	const bool bShown = !bStill && FApp::CanEverRender() && Mesh->WasRecentlyRendered()
		&& ShowsBodyMotion(GetNetMode(), GetLocalViewLocations());
	return StepBodyMotion(DeltaSeconds, Motion.ForwardSpeed, Motion.YawRate, bShown);
}

bool ARTSUnit::StepBodyMotion(float DeltaSeconds, float ForwardSpeed, float YawRate, bool bShown)
{
	if (!UnitData)
	{
		return false;
	}
	const FRTSBodyMotion& Body = UnitData->BodyMotion;
	// The acceleration is followed even unseen, so a vehicle that comes into view mid-drive does not jolt from a stale one.
	if (BodySpeed && DeltaSeconds > 0.f)
	{
		const float Acceleration = (ForwardSpeed - *BodySpeed) / DeltaSeconds;
		BodyAcceleration += (Acceleration - BodyAcceleration) * (1.f - FMath::Exp(-DeltaSeconds / Body.AccelerationSmoothing));
		if (FMath::Abs(BodyAcceleration) < UE_KINDA_SMALL_NUMBER)
		{
			BodyAcceleration = 0.f;
		}
	}
	BodySpeed = ForwardSpeed;
	const float Traverse = BodyTurretYaw ? FMath::FindDeltaAngleDegrees(*BodyTurretYaw, TurretYaw) : 0.f;
	BodyTurretYaw = TurretYaw;

	const float Scale = FMath::Max(CVarBodyMotion.GetValueOnGameThread(), 0.f);
	if (!bShown || Scale <= 0.f)
	{
		if (!IsBodyMoving())
		{
			return false;
		}
		const bool bHullMoved = HullSquat.IsActive() || HullLean.IsActive();
		HullSquat.Reset();
		HullLean.Reset();
		if (TurretLag.IsActive())
		{
			TurretLag.Reset();
			PlaceTurret();
		}
		return bHullMoved;
	}

	bool bHullMoved = false;
	if (Body.HasHullMotion())
	{
		// Pulling away the drive pushes the hull's bottom ahead of its body, which sits back on its rear and lifts its nose; a
		// turn pulls the bottom in toward its middle, so the body leans out and its inner side lifts: right in a right turn.
		// Reversing, the turn's pull changes sides along with the speed.
		const float Max = Body.MaxDegrees * Scale;
		const float Sideways = ForwardSpeed * FMath::DegreesToRadians(YawRate);
		const float Squat = FMath::Clamp(Body.SquatPerAcceleration * Scale * BodyAcceleration / 100.f, -Max, Max);
		const float Lean = FMath::Clamp(Body.LeanPerAcceleration * Scale * Sideways / 100.f, -Max, Max);
		bHullMoved |= HullSquat.Advance(Squat, DeltaSeconds, Body.SettleSeconds, Body.Damping);
		bHullMoved |= HullLean.Advance(Lean, DeltaSeconds, Body.SettleSeconds, Body.Damping);
	}
	if (Body.TurretLagDegrees > 0.f && HasTurret())
	{
		// The drawn turret stays where it was as the aim moves on, then the spring brings it after: behind a traverse it
		// trails, and once the traverse stops it swings past the aim by a little and settles on it.
		TurretLag.Shift(-Traverse, Body.TurretLagDegrees * Scale);
		if (TurretLag.Advance(0.f, DeltaSeconds, Body.TurretLagSeconds, Body.TurretLagDamping) || Traverse != 0.f)
		{
			PlaceTurret();
		}
	}
	return bHullMoved;
}

void ARTSUnit::SetGunRecoilHold(TOptional<float> Time)
{
	bGunRecoilHeld = false;
	GunRecoilTime.Reset();
	GunRecoilOffset = 0.f;
	GunJump.Reset();
	HullLift.Reset();
	HullSide.Reset();
	PlaceRecoilParts();
	PlaceGun();
	UpdateMeshTransform();
	if (Time)
	{
		GunRecoilFrom = 0.f;
		PlayGunRecoil();
		UpdateGunRecoil(*Time);
		bGunRecoilHeld = IsRecoiling();
	}
}

void ARTSUnit::UpdateGunRecoil(float DeltaSeconds)
{
	if (!UnitData)
	{
		return;
	}
	const float Elapsed = bGunRecoilHeld ? 0.f : DeltaSeconds;
	if (GunRecoilTime)
	{
		*GunRecoilTime += Elapsed;
		if (*GunRecoilTime >= UnitData->GunRecoil.GetSeconds())
		{
			GunRecoilTime.Reset();
			GunRecoilOffset = 0.f;
		}
		else
		{
			GunRecoilOffset = UnitData->GunRecoil.OffsetAt(*GunRecoilTime, GunRecoilFrom, GunRecoilStroke);
		}
		PlaceRecoilParts();
	}
	// Placed once more on the frame the spring comes to rest, so it ends at exactly zero.
	if (GunJump.IsActive())
	{
		GunJump.Advance(Elapsed);
		PlaceGun();
	}
	if (HullLift.IsActive() || HullSide.IsActive())
	{
		HullLift.Advance(Elapsed);
		HullSide.Advance(Elapsed);
		UpdateMeshTransform();
	}
}

void ARTSUnit::PlaceGun()
{
	if (Gun)
	{
		Gun->SetRelativeRotation(FRotator(GunPitch + GunJump.GetAngle(), 0.f, 0.f));
	}
}

void ARTSUnit::PlaceRecoilParts()
{
	// The parts ride on the gun, whose X is the bore, so the gun's elevation and the turret's traverse carry them as before.
	for (const FRecoilPart& Part : RecoilParts)
	{
		Part.Component->SetRelativeLocation(Part.Rest - FVector(GunRecoilOffset, 0.f, 0.f));
	}
}

bool ARTSUnit::UpdateTilt(float DeltaSeconds)
{
	FRotator Target = FRotator::ZeroRotator;
	if (UnitData && UnitData->bTiltsToGround && !AircraftComponent->CanFly())
	{
		FVector Normal = FVector::UpVector;
		if (HasAuthority())
		{
			Normal = GroundNormal.Get(FVector::UpVector);
		}
		else
		{
			// Clients trace their own ground, but only once the unit has moved; turning in place keeps the same normal.
			const FVector Location = GetActorLocation();
			if (!GroundTraceLocation || FVector::DistSquared(*GroundTraceLocation, Location) > FMath::Square(GroundTraceSkipDistance))
			{
				float GroundZ = 0.f;
				ClientGroundNormal = FVector::UpVector;
				RTSGround::Trace(*GetWorld(), GetNavAgentLocation(), GroundTraceUp, GroundTraceDown, GroundZ, &ClientGroundNormal);
				GroundTraceLocation = Location;
			}
			Normal = ClientGroundNormal;
		}
		// The slope in the unit's own frame: the mesh's up follows the normal, its nose stays on the unit's heading.
		Target = FRotationMatrix::MakeFromZX(GetActorRotation().UnrotateVector(Normal), FVector::XAxisVector).Rotator();
		Target.Yaw = 0.f;
	}
	if (MeshTilt.Equals(Target, 0.01f))
	{
		if (MeshTilt == Target)
		{
			return false;
		}
		MeshTilt = Target;
		return true;
	}
	MeshTilt = FMath::RInterpTo(MeshTilt, Target, DeltaSeconds, TiltInterpSpeed);
	return true;
}

void ARTSUnit::UpdateMeshTransform()
{
	// The unit faces its target while it fires, so back is -X.
	const float KickAlpha = FMath::Square(FireKickTimeLeft / FireKickSeconds);
	const FVector Kick(-FireKickDistance * KickAlpha, 0.f, 0.f);
	// Leaning around the bottom of the hull box keeps the tracks on the slope.
	const FVector Extent = Collision->GetUnscaledBoxExtent();
	const FVector Pivot(0.f, 0.f, -Extent.Z);
	const FVector Rest = MeshRestLocation + Kick;
	FVector Hull = Rest;
	FQuat Rotation = FQuat::Identity;
	// The rock from a shot (PlayGunRecoil) and the drive's sway (StepBodyMotion) lift one side, hinged on the bottom edge
	// across from it, so the hull rises on its suspension but never sinks into the ground or onto its tracks; the running
	// gear stays down (#333).
	const FVector2D Rock = GetHullRock() + GetHullSway();
	const float RockDegrees = Rock.Size();
	if (RockDegrees > UE_KINDA_SMALL_NUMBER)
	{
		const FVector Lifted(Rock / RockDegrees, 0.f);
		const float Reach = FMath::Min(Extent.X / FMath::Max(FMath::Abs(Lifted.X), UE_KINDA_SMALL_NUMBER),
			Extent.Y / FMath::Max(FMath::Abs(Lifted.Y), UE_KINDA_SMALL_NUMBER));
		const FVector Hinge(-Lifted.X * Reach, -Lifted.Y * Reach, -Extent.Z);
		Rotation = FQuat((Lifted ^ FVector::UpVector).GetSafeNormal(), FMath::DegreesToRadians(RockDegrees));
		Hull = Hinge + Rotation.RotateVector(Hull - Hinge);
	}
	const FQuat Tilt = MeshTilt.Quaternion();
	RunningGear->SetRelativeLocationAndRotation(Pivot + Tilt.RotateVector(Rest - Pivot), Tilt);
	Mesh->SetRelativeLocationAndRotation(Pivot + Tilt.RotateVector(Hull - Pivot), Tilt * Rotation);
}

void ARTSUnit::SnapToGround()
{
	check(HasAuthority());
	if (AircraftComponent->CanFly())
	{
		GroundTraceLocation.Reset();
		return;
	}
	// Standing still, the unit is already on the ground it was last stood on.
	FVector Location = GetActorLocation();
	if (GroundTraceLocation && FVector::DistSquared(*GroundTraceLocation, Location) <= FMath::Square(GroundTraceSkipDistance))
	{
		return;
	}
	float GroundZ = 0.f;
	FVector Normal = FVector::UpVector;
	if (!RTSGround::Trace(*GetWorld(), GetNavAgentLocation(), GroundTraceUp, GroundTraceDown, GroundZ, &Normal))
	{
		return;
	}
	GroundNormal = Normal;
	const float StandZ = GroundZ + GetGroundOffset();
	if (!FMath::IsNearlyEqual(Location.Z, StandZ, 0.1f))
	{
		// Look again next tick: a unit sunk deeper than GroundTraceUp climbs that much per trace.
		Location.Z = StandZ;
		SetActorLocation(Location);
		GroundTraceLocation.Reset();
		return;
	}
	GroundTraceLocation = Location;
}

void ARTSUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() && UnitData)
	{
		TickOrders(DeltaSeconds);
	}
	// Cosmetic, on every machine, after the server moved and grounded the unit this frame.
	const bool bTilted = UpdateTilt(DeltaSeconds);
	const bool bKicked = UpdateFireKick(DeltaSeconds);
	if (UpdateBodyMotion(DeltaSeconds) || bKicked || bTilted)
	{
		UpdateMeshTransform();
	}
	UpdateGunRecoil(DeltaSeconds);
	// The wheels follow later in the frame, once avoidance has pushed the hull too: URTSRunningGearSubsystem.
}

void ARTSUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URTSRunningGearSubsystem* Gear = GetWorld()->GetSubsystem<URTSRunningGearSubsystem>())
	{
		Gear->Unregister(*this);
	}
	if (URTSExhaustSubsystem* Exhaust = GetWorld()->GetSubsystem<URTSExhaustSubsystem>())
	{
		Exhaust->Unregister(*this);
	}
	if (URTSDustSubsystem* Dust = GetWorld()->GetSubsystem<URTSDustSubsystem>())
	{
		Dust->Unregister(*this);
	}
	if (URTSVehicleLightSubsystem* Lights = GetWorld()->GetSubsystem<URTSVehicleLightSubsystem>())
	{
		Lights->Unregister(*this);
	}
	Super::EndPlay(EndPlayReason);
}

void ARTSUnit::TickOrders(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ARTSUnit::TickOrders);

	// Once the match is over the unit stands where it is; its orders stay queued but never run again.
	if (ARTSGameState::IsMatchOver(*GetWorld()))
	{
		AAIController* AI = GetController<AAIController>();
		if (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			AI->StopMovement();
		}
		// Stopping the path leaves the speed, which the movement would brake off over several frames, coasting on.
		Movement->StopMovementImmediately();
		Movement->HaltImmediately();
		return;
	}

	// Aircraft fly by their own rules, turning and banking included.
	if (AircraftComponent->CanFly())
	{
		AircraftComponent->UpdateAircraft(DeltaSeconds);
		return;
	}

	UpdateAttackOrder();
	UpdateAttackMoveOrder();
	UpdateHarvestOrder(DeltaSeconds);
	UpdateBuildOrder(DeltaSeconds);
	SnapToGround();

	// A vehicle's gear turns its hull (URTSUnitMovement): standing, a tracked one pivots toward its target unless a turret does
	// the aiming or its barrel cannot point there without reaching into an obstacle; wheels cannot turn standing.
	if (Movement->UsesLocomotion())
	{
		TOptional<float> Facing;
		const TOptional<FVector> AimPoint = WeaponComponent->GetAimPoint();
		if (AimPoint && (!HasTurret() || bAimBlocked) && Movement->Velocity.SizeSquared2D() < FMath::Square(StandingSpeed))
		{
			const FVector ToTarget = *AimPoint - GetActorLocation();
			if (!ToTarget.IsNearlyZero())
			{
				Facing = ToTarget.Rotation().Yaw;
			}
		}
		Movement->SetFacingRequest(Facing);
		UpdateBlockedMove();
	}
	// Infantry turns toward the direction of travel, or toward the target while standing, unless a turret does the aiming or
	// its barrel cannot point there without reaching into an obstacle. Rotation replicates with movement.
	else if (Movement->Velocity.SizeSquared2D() > FMath::Square(10.f))
	{
		FaceTowardYaw(Movement->Velocity.Rotation().Yaw, DeltaSeconds);
	}
	else if (const TOptional<FVector> AimPoint = WeaponComponent->GetAimPoint(); AimPoint && (!HasTurret() || bAimBlocked))
	{
		const FVector ToTarget = *AimPoint - GetActorLocation();

		if (!ToTarget.IsNearlyZero())
		{
			const FRotator Before = GetActorRotation();
			FaceTowardYaw(ToTarget.Rotation().Yaw, DeltaSeconds);
			// The hull swings the turret with it: not into the obstacle it is turning away from.
			if (bAimBlocked && !IsBarrelClearAt(GetActorTransform()))
			{
				SetActorRotation(Before);
			}
		}
	}
	UpdateTurret(DeltaSeconds);
}

void ARTSUnit::UpdateBlockedMove()
{
	AAIController* AI = GetController<AAIController>();
	if (!AI || AI->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		return;
	}
	const bool bOutOfReach = Movement->HasGuidanceFailed();
	if (!bOutOfReach && Movement->GetBlockedSeconds() < BlockedReplanSeconds)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bStuckAgain = Now - BlockedReplanTime < BlockedGiveUpSeconds;
	const bool bMoveOrder = !CommandQueue.IsEmpty()
		&& (CommandQueue[0].Type == ERTSCommandType::Move || CommandQueue[0].Type == ERTSCommandType::AttackMove);
	if (!bOutOfReach && !bStuckAgain)
	{
		// A fresh path from where it stands, with whatever moved into its way since. Attacks, harvests and builds back off
		// too and start their own moves again once this one stops: a wheeled builder nosed into the building it just put up
		// would otherwise drive into its wall again and again (#357).
		UE_LOG(LogWorldDivide, Verbose, TEXT("%s is stuck, backs off and paths again"), *GetName());
		BlockedReplanTime = Now;
		Movement->BackOff(BlockedBackOffSeconds);
		if (bMoveOrder)
		{
			StartNextCommand();
		}
		else
		{
			AI->StopMovement();
		}
		return;
	}
	// Stuck again, or a goal its steering cannot reach: the move ends where the unit stands. Attacks, harvests and builds
	// start their own moves again. A given-up move is a refused order: its issuer hears of it, before the stop pops it. The
	// next move, of this order or the one after it, may back off once of its own.
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s gives up its move: %s"), *GetName(), bOutOfReach ? TEXT("out of reach") : TEXT("stuck"));
	BlockedReplanTime = -1.0e9;
	if (bMoveOrder)
	{
		ARTSPlayerController::NotifyOrderRefused(CommandQueue[0].Issuer, GetActorLocation());
	}
	AI->StopMovement();
}

void ARTSUnit::FaceTowardYaw(float Yaw, float DeltaSeconds)
{
	const FRotator Current = GetActorRotation();
	SetActorRotation(FMath::RInterpConstantTo(Current, FRotator(0.f, Yaw, 0.f), DeltaSeconds, UnitData->TurnRate));
}

void ARTSUnit::ExecuteCommand(const FRTSCommand& InCommand)
{
	check(HasAuthority());

	// Clients pick the target, so the server checks it; a unit that cannot fire at it keeps what it was doing.
	if (InCommand.Type == ERTSCommandType::ForceFire && !CanForceFire(InCommand))
	{
		return;
	}

	if (AircraftComponent->CanFly())
	{
		AircraftComponent->ExecuteCommand(InCommand);
		return;
	}

	FRTSCommand Command = InCommand;
	// Only ChargeOrder puts credits on an order.
	Command.PaidCost = 0;
	if (Command.Type == ERTSCommandType::Harvest && !HarvesterComponent->CanHarvest())
	{
		const ARTSSupplySource* Source = Cast<ARTSSupplySource>(Command.TargetActor);
		if (!Source)
		{
			return;
		}
		// Only harvesters load; anyone else sent to a source just walks up to it.
		const FVector Edge = Source->GetClosestFootprintPoint(GetActorLocation());
		Command.Type = ERTSCommandType::Move;
		Command.TargetActor = nullptr;
		Command.TargetLocation = Edge + (GetActorLocation() - Edge).GetSafeNormal2D() * (GetStandOffRadius() + DockStandOff);
	}
	else if (Command.Type == ERTSCommandType::Build && !BuilderComponent->CanBuild())
	{
		// Only builders construct; anyone else sent to a site just walks up to it. A new building for them is dropped.
		const ARTSBuilding* Site = Cast<ARTSBuilding>(Command.TargetActor);
		if (!Site)
		{
			return;
		}
		const FVector Edge = Site->GetClosestFootprintPoint(GetActorLocation());
		Command.Type = ERTSCommandType::Move;
		Command.TargetActor = nullptr;
		Command.TargetLocation = Edge + (GetActorLocation() - Edge).GetSafeNormal2D() * (GetStandOffRadius() + DockStandOff);
	}

	if (Command.Type == ERTSCommandType::Stop || !Command.bQueue)
	{
		HarvesterComponent->StopHarvest();
		BuilderComponent->StopBuild();
	}

	if (Command.Type == ERTSCommandType::Stop)
	{
		ClearCommands();
		if (AAIController* AI = GetController<AAIController>())
		{
			AI->StopMovement();
		}
		ApplyWeaponMode();
		return;
	}

	const bool bWasIdle = CommandQueue.IsEmpty();
	if (!Command.bQueue)
	{
		ClearCommands();
	}
	// A new building is paid as soon as it is ordered, after the replaced orders gave their payments back; one the player
	// cannot afford is dropped.
	if (Command.Type != ERTSCommandType::Build || BuilderComponent->ChargeOrder(Command))
	{
		CommandQueue.Add(Command);
		BuilderComponent->UpdatePlannedSites(CommandQueue);
	}

	if (!Command.bQueue || bWasIdle)
	{
		StartNextCommand();
	}
}

bool ARTSUnit::CanForceFire(const FRTSCommand& Command) const
{
	if (AircraftComponent->CanFly())
	{
		return AircraftComponent->CanForceFire(Command);
	}
	return WeaponComponent->CanForceFireAt(Command.TargetActor, Command.TargetLocation);
}

void ARTSUnit::StepAside(const FVector& Location)
{
	check(HasAuthority());
	// Aircraft park on pads and fly over sites.
	if (AircraftComponent->CanFly())
	{
		return;
	}

	// A Build restarts from its command afterwards, which the builder updates: a placed site to resume, or its payment.
	if (!CommandQueue.IsEmpty() && CommandQueue[0].Type == ERTSCommandType::Build)
	{
		BuilderComponent->SuspendBuild(CommandQueue[0]);
	}
	HarvesterComponent->StopHarvest();
	BuilderComponent->StopBuild();

	FRTSCommand Move;
	Move.Type = ERTSCommandType::Move;
	Move.TargetLocation = Location;
	CommandQueue.Insert(Move, 0);
	BuilderComponent->UpdatePlannedSites(CommandQueue);
	StartNextCommand();
}

bool ARTSUnit::HoldShortOf(const FRTSBuildingFootprint& Footprint)
{
	check(HasAuthority());
	if (AircraftComponent->CanFly())
	{
		return false;
	}

	AAIController* AI = GetController<AAIController>();
	const UPathFollowingComponent* PathFollowing = AI ? AI->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = PathFollowing ? PathFollowing->GetPath() : nullptr;
	if (!Path.IsValid() || PathFollowing->GetStatus() != EPathFollowingStatus::Moving)
	{
		return false;
	}

	// Footprint and path compared flat, in the building's own frame: the unit's whole circle has to stay out.
	const FBox Flat(FVector(-Footprint.Extent.X, -Footprint.Extent.Y, -WORLD_MAX), FVector(Footprint.Extent.X, Footprint.Extent.Y, WORLD_MAX));
	const FBox Area = Flat.ExpandBy(FVector(GetSelectionRadius(), GetSelectionRadius(), 0.f));
	auto ToLocal = [&Footprint](const FVector& Location) { return FVector(Footprint.ToLocal(Location), 0.f); };

	const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
	// A vehicle looks as far ahead as it needs to brake.
	float Remaining = Movement->GetMaxSpeed() * HoldLookaheadSeconds;
	if (Movement->UsesLocomotion())
	{
		Remaining = FMath::Max(Remaining, Movement->GetStoppingDistance() + HoldBrakingMargin);
	}
	FVector Start = GetActorLocation();
	bool bEnters = false;
	for (int32 Index = PathFollowing->GetNextPathIndex(); !bEnters && Remaining > 0.f && Points.IsValidIndex(Index); ++Index)
	{
		FVector End = Points[Index].Location;
		End.Z = Start.Z;
		const float Length = FVector::Dist2D(Start, End);
		if (Length > Remaining)
		{
			End = Start + (End - Start) * (Remaining / Length);
		}
		// A unit brushing the footprint, such as one still stepping off the site, may leave it.
		const FVector LocalStart = ToLocal(Start);
		const FVector LocalEnd = ToLocal(End);
		const bool bLeaving = Area.IsInsideXY(LocalStart)
			&& Flat.ComputeSquaredDistanceToPoint(LocalEnd) >= Flat.ComputeSquaredDistanceToPoint(LocalStart);
		bEnters = Length > UE_KINDA_SMALL_NUMBER && !bLeaving
			&& FMath::LineBoxIntersection(Area, LocalStart, LocalEnd, LocalEnd - LocalStart);
		Remaining -= Length;
		Start = End;
	}

	return bEnters && AI->PauseMove(AI->GetCurrentMoveRequestID());
}

bool ARTSUnit::IsMoveHeld() const
{
	// Nothing else pauses a move.
	const AAIController* AI = GetController<AAIController>();
	return AI && AI->GetMoveStatus() == EPathFollowingStatus::Paused;
}

void ARTSUnit::ReleaseHold()
{
	check(HasAuthority());

	if (!IsMoveHeld())
	{
		return;
	}
	if (!CommandQueue.IsEmpty()
		&& (CommandQueue[0].Type == ERTSCommandType::Move || CommandQueue[0].Type == ERTSCommandType::AttackMove))
	{
		// A fresh request replaces the paused one, so its path takes the new hole into account.
		StartNextCommand();
	}
	else if (AAIController* AI = GetController<AAIController>())
	{
		// Attacks, harvests and builds start a new move of their own once the unit stands idle.
		AI->StopMovement();
	}
}

void ARTSUnit::StartNextCommand()
{
	// Nothing starts after the match, such as a move released by a building's finished navmesh hole.
	if (ARTSGameState::IsMatchOver(*GetWorld()))
	{
		return;
	}
	AAIController* AI = GetController<AAIController>();
	if (!AI)
	{
		ClearCommands();
	}

	while (!CommandQueue.IsEmpty() && !StartCommand(*AI, CommandQueue[0]))
	{
		PopCommand();
	}
	ApplyWeaponMode();
}

void ARTSUnit::ClearCommands()
{
	CancelMoveRequest();
	for (FRTSCommand& Command : CommandQueue)
	{
		BuilderComponent->RefundOrder(Command);
	}
	CommandQueue.Reset();
	BuilderComponent->UpdatePlannedSites(CommandQueue);
}

void ARTSUnit::PopCommand()
{
	CancelMoveRequest();
	BuilderComponent->RefundOrder(CommandQueue[0]);
	CommandQueue.RemoveAt(0);
	BuilderComponent->UpdatePlannedSites(CommandQueue);
}

ERTSMoveRequest ARTSUnit::RequestMove(AAIController& AI, const FVector& Location)
{
	// Inside a wall or a building there is no navmesh to stand on, so the unit heads for the reachable spot nearest the
	// target instead, however deep the target lies and wherever the unit comes from.
	FVector Destination = Location;
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* NavData = NavSystem ? NavSystem->GetNavDataForProps(GetNavAgentPropertiesRef(), GetNavAgentLocation()) : nullptr;
	FNavLocation NavPoint;
	if (NavData && !NavSystem->ProjectPointToNavigation(Destination, NavPoint, MoveProjectExtent, NavData)
		&& !RTSMoveDestination::FindReachable(*this, Location, Destination))
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("%s finds no reachable spot near %s"), *GetName(), *Location.ToString());
		return ERTSMoveRequest::Refused;
	}

	MoveGoal = Destination;
	MoveTarget = Location;

	// The AI controller may report AlreadyAtGoal or a failure back through NotifyMoveFinished
	// before MoveToLocation returns; that is handled here instead of re-entering the queue.
	// Only a full path counts: a partial one would end on the near side of whatever cuts the goal off and call that done.
	bRequestingMove = true;
	bMoveFinishedDuringRequest = false;
	const EPathFollowingRequestResult::Type Result = AI.MoveToLocation(
		Destination, MoveAcceptanceRadius,
		/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ false);
	bRequestingMove = false;

	if (Result == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("%s could not path to %s"), *GetName(), *Location.ToString());
		MoveGoal.Reset();
		MoveTarget.Reset();
		return ERTSMoveRequest::Refused;
	}
	return Result == EPathFollowingRequestResult::RequestSuccessful && !bMoveFinishedDuringRequest
		? ERTSMoveRequest::Moving : ERTSMoveRequest::AtGoal;
}

bool ARTSUnit::StartMoveRequest(AAIController& AI, bool bTellRefusal)
{
	URTSMoveRequestSubsystem* Requests = GetWorld()->GetSubsystem<URTSMoveRequestSubsystem>();
	const FRTSCommand& Command = CommandQueue[0];
	if (!Requests)
	{
		const ERTSMoveRequest Request = RequestMove(AI, Command.TargetLocation);
		if (Request == ERTSMoveRequest::Refused && bTellRefusal)
		{
			ARTSPlayerController::NotifyOrderRefused(Command.Issuer, Command.TargetLocation, Command.OrderId);
		}
		return Request == ERTSMoveRequest::Moving;
	}

	// Copied before the request runs: it may drop this order and start the next.
	const int32 OrderId = Command.OrderId;
	const ARTSPlayerState* Issuer = Command.Issuer;
	MoveGoal.Reset();
	MoveTarget.Reset();
	MoveRequestTicket = MoveRequestTicket == MAX_uint32 ? 1 : MoveRequestTicket + 1;
	const uint32 Ticket = MoveRequestTicket;
	bMoveRequestPending = true;
	bMoveRequestTellsRefusal = bTellRefusal;
	Requests->Request(*this, Ticket, OrderId, Issuer);
	if (IsMoveRequestCurrent(Ticket) && AI.GetMoveStatus() != EPathFollowingStatus::Idle)
	{
		// Waiting for its turn: the replaced move ends here, and NotifyMoveFinished leaves the waiting order alone.
		AI.StopMovement();
	}
	return true;
}

void ARTSUnit::CancelMoveRequest()
{
	if (bMoveRequestPending)
	{
		bMoveRequestPending = false;
		MoveRequestTicket = MoveRequestTicket == MAX_uint32 ? 1 : MoveRequestTicket + 1;
	}
}

void ARTSUnit::RunMoveRequest()
{
	check(HasAuthority());

	bMoveRequestPending = false;
	AAIController* AI = GetController<AAIController>();
	// After the match nothing starts; the order stays, as every other does.
	if (!AI || CommandQueue.IsEmpty() || ARTSGameState::IsMatchOver(*GetWorld()))
	{
		return;
	}
	const FRTSCommand& Command = CommandQueue[0];
	if (Command.Type != ERTSCommandType::Move && Command.Type != ERTSCommandType::AttackMove)
	{
		return;
	}
	const FVector Target = Command.TargetLocation;
	const ERTSMoveRequest Request = RequestMove(*AI, Target);
	if (Request == ERTSMoveRequest::Refused && bMoveRequestTellsRefusal)
	{
		ARTSPlayerController::NotifyOrderRefused(CommandQueue[0].Issuer, Target, CommandQueue[0].OrderId);
	}
	if (Request != ERTSMoveRequest::Moving)
	{
		// Refused, or already standing at its goal.
		PopCommand();
		StartNextCommand();
	}
}

bool ARTSUnit::StartCommand(AAIController& AI, FRTSCommand& Command)
{
	if (Command.Type == ERTSCommandType::Move || Command.Type == ERTSCommandType::AttackMove)
	{
		return StartMoveRequest(AI, /*bTellRefusal*/ true);
	}
	CancelMoveRequest();
	MoveGoal.Reset();
	MoveTarget.Reset();

	if (Command.Type == ERTSCommandType::Attack)
	{
		// Clients pick the target, so the server checks it: armed, alive, and not on our side.
		if (!WeaponComponent->IsArmed() || !WeaponComponent->CanTarget(Command.TargetActor))
		{
			return false;
		}
		// Drop whatever move was running; UpdateAttackOrder starts the approach on the next tick.
		AI.StopMovement();
		return true;
	}

	if (Command.Type == ERTSCommandType::ForceFire)
	{
		// Checked again for a queued order: its target may have died or taken off since.
		if (!CanForceFire(Command))
		{
			return false;
		}
		GroundApproachStart.Reset();
		AI.StopMovement();
		return true;
	}

	if (Command.Type == ERTSCommandType::Harvest)
	{
		// Clients name the source, so anything else in TargetActor counts as none: the nearest source then.
		return HarvesterComponent->StartHarvest(Cast<ARTSSupplySource>(Command.TargetActor));
	}

	if (Command.Type == ERTSCommandType::Build)
	{
		return BuilderComponent->StartBuild(Command);
	}

	return false;
}

void ARTSUnit::UpdateHarvestOrder(float DeltaSeconds)
{
	if (CommandQueue.IsEmpty() || CommandQueue[0].Type != ERTSCommandType::Harvest)
	{
		return;
	}
	if (!HarvesterComponent->UpdateHarvest(DeltaSeconds))
	{
		PopCommand();
		StartNextCommand();
	}
}

void ARTSUnit::UpdateBuildOrder(float DeltaSeconds)
{
	if (CommandQueue.IsEmpty() || CommandQueue[0].Type != ERTSCommandType::Build)
	{
		return;
	}
	if (!BuilderComponent->UpdateBuild(DeltaSeconds))
	{
		PopCommand();
		StartNextCommand();
	}
	else if (BuilderComponent->GetSite() && !BuilderComponent->GetPlannedSites().IsEmpty())
	{
		// The front site may just have been placed; it is a building now.
		BuilderComponent->UpdatePlannedSites(CommandQueue);
	}
}

void ARTSUnit::UpdateAttackOrder()
{
	if (CommandQueue.IsEmpty()
		|| (CommandQueue[0].Type != ERTSCommandType::Attack && CommandQueue[0].Type != ERTSCommandType::ForceFire))
	{
		return;
	}
	AAIController* AI = GetController<AAIController>();
	if (!AI)
	{
		return;
	}

	AActor* Target = CommandQueue[0].TargetActor;
	const bool bForced = CommandQueue[0].Type == ERTSCommandType::ForceFire;
	if (bForced && !Target)
	{
		UpdateGroundFireOrder(*AI);
		return;
	}
	if (bForced ? !WeaponComponent->CanForceTarget(Target) : !WeaponComponent->CanTarget(Target))
	{
		AI->StopMovement();
		PopCommand();
		StartNextCommand();
		return;
	}

	const bool bMoving = AI->GetMoveStatus() != EPathFollowingStatus::Idle;
	if (WeaponComponent->IsInRange(Target))
	{
		if (bMoving)
		{
			AI->StopMovement();
		}
	}
	else if (!bMoving)
	{
		const float Range = WeaponComponent->GetRange();
		if (Target->IsA<ARTSUnit>())
		{
			// Following the actor keeps the path updated while the target moves.
			AI->MoveToActor(Target, Range * AttackApproachFraction,
				/*bStopOnOverlap*/ true, /*bUsePathfinding*/ true, /*bCanStrafe*/ true, /*FilterClass*/ nullptr,
				/*bAllowPartialPath*/ true);
		}
		else if (const ARTSBuilding* Building = Cast<ARTSBuilding>(Target))
		{
			// A building's centre lies in its own navmesh hole, out of reach of a path; stand outside its footprint,
			// facing side first. With no reachable spot in range, or a move that cannot start, the order is over.
			FVector Spot;
			const bool bFound = Building->GetBuildingData()
				&& RTSApproach::FindReachableSpot(*this, Building->GetGroundFootprint(),
					Range * BuildingApproachFraction, Range - MoveAcceptanceRadius, Spot);
			const EPathFollowingRequestResult::Type Result = bFound
				? AI->MoveToLocation(Spot, MoveAcceptanceRadius,
					/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
					/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true)
				: EPathFollowingRequestResult::Failed;
			if (Result != EPathFollowingRequestResult::RequestSuccessful)
			{
				UE_LOG(LogWorldDivide, Verbose, TEXT("%s cannot reach %s and drops the attack"), *GetName(), *Target->GetName());
				AI->StopMovement();
				PopCommand();
				StartNextCommand();
			}
		}
	}
}

void ARTSUnit::UpdateGroundFireOrder(AAIController& AI)
{
	const FVector Spot = CommandQueue[0].TargetLocation;
	const bool bMoving = AI.GetMoveStatus() != EPathFollowingStatus::Idle;
	if (WeaponComponent->IsInRange(Spot))
	{
		if (bMoving)
		{
			AI.StopMovement();
		}
		GroundApproachStart.Reset();
		return;
	}
	if (bMoving)
	{
		return;
	}

	// The last approach ended out of range where it started, such as a partial path to a spot across water: out of reach.
	const bool bStuck = GroundApproachStart
		&& FVector::DistSquared2D(*GroundApproachStart, GetActorLocation()) < FMath::Square(MoveAcceptanceRadius);
	GroundApproachStart = GetActorLocation();
	const EPathFollowingRequestResult::Type Result = bStuck ? EPathFollowingRequestResult::Failed
		: AI.MoveToLocation(Spot, WeaponComponent->GetRange() * AttackApproachFraction,
			/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
			/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("%s cannot get %s in range and drops the force fire"), *GetName(), *Spot.ToString());
		AI.StopMovement();
		PopCommand();
		StartNextCommand();
	}
}

void ARTSUnit::UpdateAttackMoveOrder()
{
	if (CommandQueue.IsEmpty() || CommandQueue[0].Type != ERTSCommandType::AttackMove)
	{
		return;
	}
	AAIController* AI = GetController<AAIController>();
	// A move held short of a new building waits for ReleaseHold, which paths it again; a waiting one for its search.
	if (!AI || IsMoveHeld() || bMoveRequestPending)
	{
		return;
	}

	// The weapon picks its own target, units before buildings; the order only decides whether to stand or walk.
	const AActor* Target = WeaponComponent->GetTarget();
	const bool bEngaged = WeaponComponent->CanTarget(Target) && WeaponComponent->IsInRange(Target);
	const bool bMoving = AI->GetMoveStatus() != EPathFollowingStatus::Idle;
	if (bEngaged)
	{
		if (bMoving)
		{
			// NotifyMoveFinished keeps the order through this stop.
			bStoppingToFight = true;
			AI->StopMovement();
			bStoppingToFight = false;
		}
	}
	else if (!bMoving && !StartMoveRequest(*AI, /*bTellRefusal*/ false))
	{
		// Already at the target, or no way left there; a request that waits ends the order the same way when it runs.
		PopCommand();
		StartNextCommand();
	}
}

void ARTSUnit::ApplyWeaponMode()
{
	const FRTSCommand* Current = CommandQueue.IsEmpty() ? nullptr : &CommandQueue[0];
	if (Current && Current->Type == ERTSCommandType::Attack)
	{
		WeaponComponent->SetOrderedTarget(Current->TargetActor);
	}
	else if (Current && Current->Type == ERTSCommandType::ForceFire && Current->TargetActor)
	{
		WeaponComponent->SetOrderedTarget(Current->TargetActor, /*bForce*/ true);
	}
	else if (Current && Current->Type == ERTSCommandType::ForceFire)
	{
		WeaponComponent->SetOrderedLocation(Current->TargetLocation);
	}
	else
	{
		WeaponComponent->ClearOrder();
	}
	// Units on a plain move ignore enemies; idle and attack-moving units engage whatever comes into range.
	WeaponComponent->SetAutoAcquire(Current == nullptr || Current->Type == ERTSCommandType::AttackMove);
}

void ARTSUnit::NotifyMoveFinished(const FPathFollowingResult& Result)
{
	// A new order replaced this move; the queue already reflects that.
	if (Result.HasFlag(FPathFollowingResultFlags::NewRequest))
	{
		return;
	}
	if (bRequestingMove)
	{
		bMoveFinishedDuringRequest = true;
		return;
	}
	// The move a waiting request replaced ended; the order waits for its own path (StartMoveRequest).
	if (bMoveRequestPending)
	{
		return;
	}
	// Stopped by the end of the match: the order is neither finished nor dropped, so nothing is popped or refunded.
	if (ARTSGameState::IsMatchOver(*GetWorld()))
	{
		return;
	}
	// UpdateAttackOrder, the harvester and the builder start and stop their own moves and end their orders themselves.
	if (!CommandQueue.IsEmpty()
		&& (CommandQueue[0].Type == ERTSCommandType::Attack || CommandQueue[0].Type == ERTSCommandType::ForceFire
			|| CommandQueue[0].Type == ERTSCommandType::Harvest || CommandQueue[0].Type == ERTSCommandType::Build))
	{
		return;
	}
	// An attack-move stopped to fight; UpdateAttackMoveOrder walks on once no enemy is left in range.
	if (bStoppingToFight)
	{
		return;
	}

	if (!CommandQueue.IsEmpty())
	{
		PopCommand();
	}
	StartNextCommand();
}

void ARTSUnit::ArriveInCrowd()
{
	check(HasAuthority());

	const bool bMoveOrder = !CommandQueue.IsEmpty()
		&& (CommandQueue[0].Type == ERTSCommandType::Move || CommandQueue[0].Type == ERTSCommandType::AttackMove);
	AAIController* AI = GetController<AAIController>();
	if (bMoveOrder && AI && AI->GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		// NotifyMoveFinished ends the order and starts the next one.
		AI->StopMovement();
	}
}

void ARTSUnit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ARTSUnit, UnitData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSUnit, TeamId, COND_InitialOnly);
	DOREPLIFETIME(ARTSUnit, TurretYaw);
	DOREPLIFETIME(ARTSUnit, GunPitch);
	DOREPLIFETIME(ARTSUnit, LocomotionNet);
}
