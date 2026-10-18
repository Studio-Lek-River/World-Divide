#include "Units/RTSUnit.h"

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
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
#include "Materials/MaterialInstanceDynamic.h"
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
#include "Units/RTSGround.h"
#include "Units/RTSRunningGearSubsystem.h"
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

/** Engine basic shapes expose this parameter; real unit and building materials should too. */
const FName ARTSUnit::TeamColorParameter(TEXT("Color"));

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
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

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
	LastGearPose.Reset();
	GearHull.Reset();
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

	const FTransform Hull = Mesh->GetComponentTransform();
	TMap<UStaticMesh*, int32> SetOfMesh;
	for (const FRTSVehiclePart& Part : UnitData->Parts)
	{
		FPartSlot& Slot = PartSlots.AddDefaulted_GetRef();
		USceneComponent* Parent = Part.MountedOn == ERTSVehiclePartMount::Gun ? Gun.Get()
			: Part.MountedOn == ERTSVehiclePartMount::Turret ? (HasTurret() ? Turret.Get() : nullptr) : Mesh.Get();
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
			if (Part.TrackPitch > 0.f && Part.MountedOn == ERTSVehiclePartMount::Hull)
			{
				AddTrack(Slot, Part);
			}
			continue;
		}

		// One draw for every wheel of a kind: each wheel is an instance. The set stays attached to the hull, which it is shown,
		// hidden and coloured with, but it does not move with it: UpdateRunningGear writes every instance in world space each
		// frame the hull moves or the wheels roll. Carried by the hull's move as well, the set would be updated twice (#165).
		int32* SetIndex = SetOfMesh.Find(Part.Mesh.Get());
		if (!SetIndex)
		{
			FWheelSet& NewSet = WheelSets.AddDefaulted_GetRef();
			NewSet.Component = MakePartComponent<UInstancedStaticMeshComponent>(*this, *Parent, Part.Mesh, /*bWorldPlaced*/ true);
			PartComponents.Add(NewSet.Component);
			SetIndex = &SetOfMesh.Add(Part.Mesh.Get(), WheelSets.Num() - 1);
		}
		FWheelSet& Set = WheelSets[*SetIndex];
		Slot.Component = Set.Component;
		Slot.Instance = Set.Component->AddInstance(Rest * Hull, /*bWorldSpace*/ false);
		Slot.Wheel = Wheels.Num();
		Set.Transforms.Add(Rest * Hull);

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
	return FTransform(Roll * Wheel.Rest.GetRotation(), Wheel.Rest.GetLocation()) * Mesh->GetComponentTransform();
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

void ARTSUnit::UpdateRunningGear(const FRTSTrackViews* Views)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ARTSUnit::UpdateRunningGear);

	if (Wheels.IsEmpty() && Tracks.IsEmpty())
	{
		return;
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

	for (FTrack& Track : Tracks)
	{
		if (bRolls)
		{
			// Links of travel along the ground. The phase counts through one link only, and goes down driving forward.
			const float Links = TravelOf(Track.Side) / Track.Pitch;
			Track.Phase = FMath::Frac(Track.Phase - FMath::Frac(Links));
			Track.PreviousPhase = Track.Phase + Links;
			Track.bSent = false;
		}
		else if (Track.PreviousPhase != Track.Phase)
		{
			// Stopped: once more without motion, so the links do not smear as if still running.
			Track.PreviousPhase = Track.Phase;
			Track.bSent = false;
		}
		if (!Track.bSent && IsTrackDrawn(Track, Views))
		{
			// Custom primitive data reaches the renderer as an update of the primitive's data: no render state rebuild and
			// no move (#165).
			Track.Component->SetCustomPrimitiveDataVector2(0, FVector2D(Track.Phase, Track.PreviousPhase));
			Track.bSent = true;
		}
	}

	// The wheels are placed in world space, so they follow every move of the hull, rolled or not: a push, a tilt, a kick, a
	// correction from the server.
	const FTransform& Hull = Mesh->GetComponentTransform();
	if (Wheels.IsEmpty() || (!bRolls && GearHull.IsSet() && GearHull->Equals(Hull)))
	{
		return;
	}
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
	for (const FWheelSet& Set : WheelSets)
	{
		// Without a render state rebuild: the set's instance data goes to the renderer as a change of transforms, where
		// rebuilding the proxy of every set of every vehicle each frame cost more than the move itself (#165).
		Set.Component->BatchUpdateInstancesTransforms(0, Set.Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ false);
		Set.Component->UpdateBounds();
		Set.Component->MarkRenderTransformDirty();
	}
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
	if (Views->Range <= 0.f || Views->Locations.IsEmpty())
	{
		return true;
	}
	const FVector Location = Track.Component->GetComponentLocation();
	for (const FVector& View : Views->Locations)
	{
		if (FVector::DistSquared(View, Location) <= FMath::Square(Views->Range))
		{
			return true;
		}
	}
	return false;
}

void ARTSUnit::ApplyTeamColor()
{
	TArray<UStaticMeshComponent*, TInlineAllocator<8>> Parts = { Mesh.Get(), Turret.Get(), Gun.Get() };
	Parts.Append(PartComponents);
	for (UStaticMeshComponent* Part : Parts)
	{
		if (!Part || !Part->GetStaticMesh())
		{
			continue;
		}
		// Every slot, since a model can carry several materials.
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* Material = Part->CreateDynamicMaterialInstance(Slot))
			{
				Material->SetVectorParameterValue(TeamColorParameter, GetTeamColor(TeamId));
			}
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
	const FVector ToTarget = Target.GetActorLocation() - GetActorLocation();
	return FRotator::NormalizeAxis(ToTarget.Rotation().Yaw - GetActorRotation().Yaw);
}

void ARTSUnit::UpdateTurret(float DeltaSeconds)
{
	if (!HasTurret())
	{
		return;
	}
	const AActor* Target = WeaponComponent->GetTarget();
	float Goal = AimOverride ? AimOverride->X : Target ? GetTurretYawToward(*Target) : 0.f;
	// Up toward the target's middle from the trunnion; the hull's tilt is left out.
	float PitchGoal = 0.f;
	if (AimOverride)
	{
		PitchGoal = AimOverride->Y;
	}
	else if (Target && Gun)
	{
		const FVector ToTarget = Target->GetActorLocation() - Gun->GetComponentLocation();
		PitchGoal = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Z, ToTarget.Size2D()));
	}
	PitchGoal = HasGun() ? FMath::Clamp(PitchGoal, UnitData->GunMinPitch, UnitData->GunMaxPitch) : 0.f;
	// An inspection aim is held as given.
	if (!AimOverride)
	{
		KeepBarrelClear(Goal, PitchGoal, Target != nullptr);
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
	Turret->SetRelativeRotation(FRotator(0.f, TurretYaw, 0.f));
}

void ARTSUnit::OnRep_GunPitch()
{
	if (Gun)
	{
		Gun->SetRelativeRotation(FRotator(GunPitch, 0.f, 0.f));
	}
}

bool ARTSUnit::IsAimedAt(const AActor& Target) const
{
	// A barrel that cannot point there without reaching into an obstacle waits, even where it passes the target on its way.
	return !HasTurret() || (!bAimBlocked && FMath::Abs(FMath::FindDeltaAngleDegrees(TurretYaw, GetTurretYawToward(Target))) <= TurretAimTolerance);
}

FVector ARTSUnit::GetMuzzleLocation() const
{
	if (Gun)
	{
		return Gun->GetComponentTransform().TransformPosition(UnitData->GunMuzzleOffset);
	}
	return Turret->GetComponentTransform().TransformPosition(UnitData ? UnitData->MuzzleOffset : FVector::ZeroVector);
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
	const FVector Pivot(0.f, 0.f, -Collision->GetUnscaledBoxExtent().Z);
	const FQuat Tilt = MeshTilt.Quaternion();
	Mesh->SetRelativeLocationAndRotation(Pivot + Tilt.RotateVector(MeshRestLocation + Kick - Pivot), Tilt);
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
	if (UpdateFireKick(DeltaSeconds) || bTilted)
	{
		UpdateMeshTransform();
	}
	// The wheels follow later in the frame, once avoidance has pushed the hull too: URTSRunningGearSubsystem.
}

void ARTSUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URTSRunningGearSubsystem* Gear = GetWorld()->GetSubsystem<URTSRunningGearSubsystem>())
	{
		Gear->Unregister(*this);
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
		const AActor* Target = WeaponComponent->GetTarget();
		if (Target && (!HasTurret() || bAimBlocked) && Movement->Velocity.SizeSquared2D() < FMath::Square(StandingSpeed))
		{
			const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
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
	else if (const AActor* Target = WeaponComponent->GetTarget(); Target && (!HasTurret() || bAimBlocked))
	{
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
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
	if (!bOutOfReach && !bStuckAgain && bMoveOrder)
	{
		// A fresh path from where it stands, with whatever moved into its way since.
		UE_LOG(LogWorldDivide, Verbose, TEXT("%s is stuck, backs off and paths again"), *GetName());
		BlockedReplanTime = Now;
		Movement->BackOff(BlockedBackOffSeconds);
		StartNextCommand();
		return;
	}
	// Stuck again, or a goal its steering cannot reach: the move ends where the unit stands. Attacks, harvests and builds
	// start their own moves again. A given-up move is a refused order: its issuer hears of it, before the stop pops it, and
	// the order after it may back off once of its own.
	UE_LOG(LogWorldDivide, Verbose, TEXT("%s gives up its move: %s"), *GetName(), bOutOfReach ? TEXT("out of reach") : TEXT("stuck"));
	BlockedReplanTime = bMoveOrder ? -1.0e9 : Now;
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

bool ARTSUnit::HoldShortOf(const FBox& Footprint)
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

	// Footprint and path compared flat: the unit's whole circle has to stay out.
	const FBox Flat(FVector(Footprint.Min.X, Footprint.Min.Y, -WORLD_MAX), FVector(Footprint.Max.X, Footprint.Max.Y, WORLD_MAX));
	const FBox Area = Flat.ExpandBy(FVector(GetSelectionRadius(), GetSelectionRadius(), 0.f));

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
		const bool bLeaving = Area.IsInsideXY(Start)
			&& Flat.ComputeSquaredDistanceToPoint(End) >= Flat.ComputeSquaredDistanceToPoint(Start);
		bEnters = Length > UE_KINDA_SMALL_NUMBER && !bLeaving && FMath::LineBoxIntersection(Area, Start, End, End - Start);
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
	if (CommandQueue.IsEmpty() || CommandQueue[0].Type != ERTSCommandType::Attack)
	{
		return;
	}
	AAIController* AI = GetController<AAIController>();
	if (!AI)
	{
		return;
	}

	AActor* Target = CommandQueue[0].TargetActor;
	if (!WeaponComponent->CanTarget(Target))
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
				&& RTSApproach::FindReachableSpot(*this, Building->GetActorLocation(), Building->GetFootprint(),
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
	const bool bAttacking = Current && Current->Type == ERTSCommandType::Attack;
	WeaponComponent->SetOrderedTarget(bAttacking ? Current->TargetActor.Get() : nullptr);
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
		&& (CommandQueue[0].Type == ERTSCommandType::Attack || CommandQueue[0].Type == ERTSCommandType::Harvest
			|| CommandQueue[0].Type == ERTSCommandType::Build))
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
