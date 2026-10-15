#include "Units/RTSUnit.h"

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/CapsuleComponent.h"
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
#include "GameFramework/FloatingPawnMovement.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/RTSPlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSApproach.h"
#include "Units/RTSGround.h"
#include "Units/RTSUnitAIController.h"
#include "WorldDivide.h"

namespace
{
	constexpr float MoveAcceptanceRadius = 25.f;

	/** Attackers stop this far inside their range so a slowly retreating target does not slip straight out again. */
	constexpr float AttackApproachFraction = 0.8f;

	/** Attackers head for a spot this fraction of their range outside a building's edge, well clear of its navmesh hole. */
	constexpr float BuildingApproachFraction = 0.5f;

	/** Units that cannot harvest or build stop this far outside the edge of a supply source or construction site they were sent to. */
	constexpr float DockStandOff = 100.f;

	/** A move into a wall or a building steps back toward the unit by this much until it finds navmesh, up to the distance. */
	constexpr float MoveSnapStep = 50.f;
	constexpr float MoveSnapDistance = 1000.f;
	/** Tall enough to reach the ground from the roof of a block, where a click on it lands. */
	const FVector MoveSnapExtent(MoveSnapStep, MoveSnapStep, 1000.f);

	/** A unit holds short of a new building once this much of its travel time lies between it and the footprint. */
	constexpr float HoldLookaheadSeconds = 0.2f;

	/** A fire kick jolts the mesh back this many cm per unit of effect scale and eases it home over this long. */
	constexpr float FireKickPerScale = 8.f;
	constexpr float FireKickSeconds = 0.15f;

	/**
	 * The ground is looked for from this far above a unit's feet, enough for the steepest ramp it climbs in a frame, down to
	 * this far below them, enough for a unit spawned above the ground.
	 */
	constexpr float GroundTraceUp = 100.f;
	constexpr float GroundTraceDown = 2000.f;

	/** A tilting vehicle eases its mesh toward the slope at this rate, so it rolls over a crest instead of snapping. */
	constexpr float TiltInterpSpeed = 10.f;

	/** A turret fires once it points this close to its target, in degrees. */
	constexpr float TurretAimTolerance = 5.f;

	/** Below this much driving (cm) and turning (degrees) since the last frame the wheels stand still. */
	constexpr float GearMinDistance = 0.01f;
	constexpr float GearMinTurn = 0.001f;

	/** Mesh's box, in Mesh's own space, grown by the parts on the hull: running gear reaches the ground below the hull. */
	FBox GetHullBox(const URTSUnitData& Data)
	{
		FBox Box(ForceInit);
		if (Data.Mesh)
		{
			Box += Data.Mesh->GetBoundingBox();
		}
		for (const FRTSVehiclePart& Part : Data.Parts)
		{
			if (Part.Mesh && Part.MountedOn == ERTSVehiclePartMount::Hull)
			{
				Box += Part.Mesh->GetBoundingBox().TransformBy(FTransform(Part.Rotation, Part.Offset));
			}
		}
		return Box;
	}

	/** A cosmetic part on Parent, like Mesh and Turret: the capsule alone collides and is picked. */
	template <typename T>
	T* MakePartComponent(AActor& Owner, USceneComponent& Parent, UStaticMesh* PartMesh)
	{
		T* Part = NewObject<T>(&Owner);
		Part->SetStaticMesh(PartMesh);
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

	Collision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Collision"));
	Collision->InitCapsuleSize(60.f, 60.f);
	// Units steer by the navmesh, which already routes around obstacles, so they don't sweep against the world:
	// a capsule resting on the ground counts as penetrating it and would block every move.
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

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	// No plane constraint: the path climbs and descends with the navmesh, and SnapToGround stands the unit on the ground.
	// Path following feeds acceleration input, so MaxSpeed and braking behave like player input would.
	Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;

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

	const float Radius = UnitData->SelectionRadius;
	float MeshHalfHeight = Radius;
	float MeshBottomZ = -Radius;

	Mesh->SetStaticMesh(UnitData->Mesh);
	Mesh->SetRelativeScale3D(UnitData->MeshScale);
	if (const FBox HullBox = GetHullBox(*UnitData); HullBox.IsValid)
	{
		MeshHalfHeight = HullBox.GetExtent().Z * UnitData->MeshScale.Z;
		MeshBottomZ = HullBox.Min.Z * UnitData->MeshScale.Z;
	}

	const float HalfHeight = FMath::Max(MeshHalfHeight, Radius);
	Collision->SetCapsuleSize(Radius, HalfHeight);
	// Stand the mesh, or the running gear below it, on the bottom of the capsule.
	MeshRestLocation = FVector(0.f, 0.f, -HalfHeight - MeshBottomZ);
	UpdateMeshTransform();

	Turret->SetStaticMesh(UnitData->TurretMesh);
	Turret->SetVisibility(UnitData->TurretMesh != nullptr);
	Turret->SetRelativeLocationAndRotation(UnitData->TurretOffset, FRotator(0.f, TurretYaw, 0.f));
	CreateParts();

	Movement->MaxSpeed = UnitData->MoveSpeed;

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
	LastGearPose.Reset();
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

	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> WheelSets;
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
			continue;
		}

		// One draw for every wheel of a kind: each wheel is an instance, placed relative to the hull.
		UInstancedStaticMeshComponent*& Set = WheelSets.FindOrAdd(Part.Mesh.Get());
		if (!Set)
		{
			Set = MakePartComponent<UInstancedStaticMeshComponent>(*this, *Parent, Part.Mesh);
			PartComponents.Add(Set);
		}
		Slot.Component = Set;
		Slot.Instance = Set->AddInstance(Rest, /*bWorldSpace*/ false);

		FWheel& Wheel = Wheels.AddDefaulted_GetRef();
		Wheel.Component = Set;
		Wheel.Instance = Slot.Instance;
		Wheel.Rest = Rest;
		Wheel.Radius = FMath::Max(Part.Mesh->GetBoundingBox().GetExtent().Z * UnitData->MeshScale.Z, 1.f);
		Wheel.Side = Part.Offset.Y * UnitData->MeshScale.Y;
	}
}

bool ARTSUnit::GetPartTransform(int32 PartIndex, FTransform& OutTransform) const
{
	if (!PartSlots.IsValidIndex(PartIndex) || !PartSlots[PartIndex].Component)
	{
		return false;
	}
	const FPartSlot& Slot = PartSlots[PartIndex];
	if (Slot.Instance == INDEX_NONE)
	{
		OutTransform = Slot.Component->GetComponentTransform();
		return true;
	}
	return CastChecked<UInstancedStaticMeshComponent>(Slot.Component)->GetInstanceTransform(Slot.Instance, OutTransform, /*bWorldSpace*/ true);
}

void ARTSUnit::UpdateRunningGear()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ARTSUnit::UpdateRunningGear);

	if (Wheels.IsEmpty())
	{
		return;
	}
	const FTransform Pose = GetActorTransform();
	if (!LastGearPose.IsSet())
	{
		LastGearPose = Pose;
		return;
	}
	const float Driven = FVector::DotProduct(Pose.GetLocation() - LastGearPose->GetLocation(), Pose.GetUnitAxis(EAxis::X));
	const float Turned = FMath::FindDeltaAngleDegrees(LastGearPose->Rotator().Yaw, Pose.Rotator().Yaw);
	LastGearPose = Pose;
	if (FMath::Abs(Driven) < GearMinDistance && FMath::Abs(Turned) < GearMinTurn)
	{
		return;
	}

	// Turning right (positive yaw) drives the left side (negative Y) forward and the right side back.
	const float TurnedRadians = FMath::DegreesToRadians(Turned);
	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Updates;
	for (FWheel& Wheel : Wheels)
	{
		Wheel.Angle = FMath::Fmod(Wheel.Angle + (Driven - Wheel.Side * TurnedRadians) / Wheel.Radius, UE_TWO_PI);
		// Rolling forward brings the top of the wheel forward: a nose-down pitch about the hull's Y axis, after the wheel's
		// own turn on the hull.
		const FQuat Roll = FRotator(-FMath::RadiansToDegrees(Wheel.Angle), 0.f, 0.f).Quaternion();
		TArray<FTransform>& Transforms = Updates.FindOrAdd(Wheel.Component);
		Transforms.SetNum(FMath::Max(Transforms.Num(), Wheel.Instance + 1));
		Transforms[Wheel.Instance] = FTransform(Roll * Wheel.Rest.GetRotation(), Wheel.Rest.GetLocation());
	}
	for (const TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& Update : Updates)
	{
		Update.Key->BatchUpdateInstancesTransforms(0, Update.Value, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true);
	}
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
	const float Goal = AimOverride ? AimOverride->X : Target ? GetTurretYawToward(*Target) : 0.f;
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
	// Up toward the target's middle from the trunnion; the hull's tilt is left out.
	float PitchGoal = 0.f;
	if (AimOverride)
	{
		PitchGoal = AimOverride->Y;
	}
	else if (Target)
	{
		const FVector ToTarget = Target->GetActorLocation() - Gun->GetComponentLocation();
		PitchGoal = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Z, ToTarget.Size2D()));
	}
	PitchGoal = FMath::Clamp(PitchGoal, UnitData->GunMinPitch, UnitData->GunMaxPitch);
	const float Pitch = FMath::FixedTurn(GunPitch, PitchGoal, UnitData->GunElevationRate * DeltaSeconds);
	if (Pitch != GunPitch)
	{
		GunPitch = Pitch;
		OnRep_GunPitch();
	}
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
	return !HasTurret() || FMath::Abs(FMath::FindDeltaAngleDegrees(TurretYaw, GetTurretYawToward(Target))) <= TurretAimTolerance;
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

float ARTSUnit::GetSelectionRadius() const
{
	return UnitData ? UnitData->SelectionRadius : Collision->GetUnscaledCapsuleRadius();
}

bool ARTSUnit::IsAirborne() const
{
	return AircraftComponent->IsAirborne();
}

FVector ARTSUnit::GetClosestFootprintPoint(const FVector& From) const
{
	const FVector Center = GetActorLocation();
	return Center + (From - Center).GetSafeNormal2D() * GetSelectionRadius();
}

float ARTSUnit::GetGroundOffset() const
{
	return Collision->GetScaledCapsuleHalfHeight();
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
		float GroundZ = 0.f;
		if (HasAuthority())
		{
			Normal = GroundNormal.Get(FVector::UpVector);
		}
		else
		{
			RTSGround::Trace(*GetWorld(), GetNavAgentLocation(), GroundTraceUp, GroundTraceDown, GroundZ, &Normal);
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
	// Leaning around the bottom of the capsule keeps the tracks on the slope.
	const FVector Pivot(0.f, 0.f, -Collision->GetUnscaledCapsuleHalfHeight());
	const FQuat Tilt = MeshTilt.Quaternion();
	Mesh->SetRelativeLocationAndRotation(Pivot + Tilt.RotateVector(MeshRestLocation + Kick - Pivot), Tilt);
}

void ARTSUnit::SnapToGround()
{
	check(HasAuthority());
	if (AircraftComponent->CanFly())
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
	FVector Location = GetActorLocation();
	const float StandZ = GroundZ + GetGroundOffset();
	if (!FMath::IsNearlyEqual(Location.Z, StandZ, 0.1f))
	{
		Location.Z = StandZ;
		SetActorLocation(Location);
	}
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
	UpdateRunningGear();
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

	// Turn toward the direction of travel, or toward the target while standing, unless a turret does the aiming. Rotation
	// replicates with movement.
	if (Movement->Velocity.SizeSquared2D() > FMath::Square(10.f))
	{
		FaceTowardYaw(Movement->Velocity.Rotation().Yaw, DeltaSeconds);
	}
	else if (const AActor* Target = WeaponComponent->GetTarget(); Target && !HasTurret())
	{
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		if (!ToTarget.IsNearlyZero())
		{
			FaceTowardYaw(ToTarget.Rotation().Yaw, DeltaSeconds);
		}
	}
	UpdateTurret(DeltaSeconds);
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
		Command.TargetLocation = Edge + (GetActorLocation() - Edge).GetSafeNormal2D() * (GetSelectionRadius() + DockStandOff);
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
		Command.TargetLocation = Edge + (GetActorLocation() - Edge).GetSafeNormal2D() * (GetSelectionRadius() + DockStandOff);
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
	float Remaining = Movement->GetMaxSpeed() * HoldLookaheadSeconds;
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
	for (FRTSCommand& Command : CommandQueue)
	{
		BuilderComponent->RefundOrder(Command);
	}
	CommandQueue.Reset();
	BuilderComponent->UpdatePlannedSites(CommandQueue);
}

void ARTSUnit::PopCommand()
{
	BuilderComponent->RefundOrder(CommandQueue[0]);
	CommandQueue.RemoveAt(0);
	BuilderComponent->UpdatePlannedSites(CommandQueue);
}

bool ARTSUnit::RequestMove(AAIController& AI, const FVector& Location)
{
	// Inside a wall or a building there is no navmesh to stand on, so the unit stops at the edge on its own side
	// instead of dropping the order or walking around to the far one.
	FVector Destination = Location;
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const ANavigationData* NavData = NavSystem ? NavSystem->GetNavDataForProps(GetNavAgentPropertiesRef(), GetNavAgentLocation()) : nullptr;
	FNavLocation NavPoint;
	if (NavData && !NavSystem->ProjectPointToNavigation(Destination, NavPoint, INVALID_NAVEXTENT, NavData))
	{
		const FVector Back = (GetNavAgentLocation() - Destination).GetSafeNormal2D();
		for (float Distance = 0.f; Distance <= MoveSnapDistance; Distance += MoveSnapStep)
		{
			if (NavSystem->ProjectPointToNavigation(Destination + Back * Distance, NavPoint, MoveSnapExtent, NavData))
			{
				Destination = NavPoint.Location;
				break;
			}
		}
	}

	MoveGoal = Destination;

	// The AI controller may report AlreadyAtGoal or a failure back through NotifyMoveFinished
	// before MoveToLocation returns; that is handled here instead of re-entering the queue.
	bRequestingMove = true;
	bMoveFinishedDuringRequest = false;
	const EPathFollowingRequestResult::Type Result = AI.MoveToLocation(
		Destination, MoveAcceptanceRadius,
		/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
	bRequestingMove = false;

	if (Result == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("%s could not path to %s"), *GetName(), *Location.ToString());
	}
	return Result == EPathFollowingRequestResult::RequestSuccessful && !bMoveFinishedDuringRequest;
}

bool ARTSUnit::StartCommand(AAIController& AI, FRTSCommand& Command)
{
	if (Command.Type == ERTSCommandType::Move || Command.Type == ERTSCommandType::AttackMove)
	{
		return RequestMove(AI, Command.TargetLocation);
	}
	MoveGoal.Reset();

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
	// A move held short of a new building waits for ReleaseHold, which paths it again.
	if (!AI || IsMoveHeld())
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
	else if (!bMoving && !RequestMove(*AI, CommandQueue[0].TargetLocation))
	{
		// Already at the target, or no way left there.
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
}
