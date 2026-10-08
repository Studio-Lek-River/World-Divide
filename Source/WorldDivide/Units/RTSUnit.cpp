#include "Units/RTSUnit.h"

#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
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
#include "Units/RTSApproach.h"
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
	// They overlap each other until proper avoidance exists, and block mouse traces for picking.
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// Otherwise the navmesh grows on top of units and paths start on their roofs.
	Collision->SetCanEverAffectNavigation(false);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->SetPlaneConstraintNormal(FVector::UpVector);
	Movement->SetPlaneConstraintEnabled(true);
	// Path following feeds acceleration input, so MaxSpeed and braking behave like player input would.
	Movement->GetNavMovementProperties()->bUseAccelerationForPaths = true;

	HealthComponent = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));
	WeaponComponent = CreateDefaultSubobject<URTSWeaponComponent>(TEXT("Weapon"));
	HarvesterComponent = CreateDefaultSubobject<URTSHarvesterComponent>(TEXT("Harvester"));
	BuilderComponent = CreateDefaultSubobject<URTSBuilderComponent>(TEXT("Builder"));

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

	HealthComponent->OnDeath.AddDynamic(this, &ThisClass::HandleDeath);
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
	float MeshOriginZ = 0.f;

	Mesh->SetStaticMesh(UnitData->Mesh);
	Mesh->SetRelativeScale3D(UnitData->MeshScale);
	if (UnitData->Mesh)
	{
		const FBoxSphereBounds Bounds = UnitData->Mesh->GetBounds();
		MeshHalfHeight = Bounds.BoxExtent.Z * UnitData->MeshScale.Z;
		MeshOriginZ = Bounds.Origin.Z * UnitData->MeshScale.Z;
	}

	const float HalfHeight = FMath::Max(MeshHalfHeight, Radius);
	Collision->SetCapsuleSize(Radius, HalfHeight);
	// Stand the mesh on the bottom of the capsule.
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + MeshHalfHeight - MeshOriginZ));

	Movement->MaxSpeed = UnitData->MoveSpeed;

	ApplyTeamColor();
}

void ARTSUnit::ApplyTeamColor()
{
	if (!Mesh->GetStaticMesh())
	{
		return;
	}
	// Every slot, since a model can carry several materials.
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(Slot))
		{
			Material->SetVectorParameterValue(TeamColorParameter, GetTeamColor(TeamId));
		}
	}
}

FLinearColor ARTSUnit::GetTeamColor(int32 InTeamId)
{
	static const FLinearColor Colors[] = {
		FLinearColor(0.05f, 0.25f, 0.9f),
		FLinearColor(0.85f, 0.08f, 0.05f),
		FLinearColor(0.1f, 0.7f, 0.15f),
		FLinearColor(0.9f, 0.75f, 0.05f),
	};
	return Colors[FMath::Abs(InTeamId) % UE_ARRAY_COUNT(Colors)];
}

float ARTSUnit::GetSelectionRadius() const
{
	return UnitData ? UnitData->SelectionRadius : Collision->GetUnscaledCapsuleRadius();
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

void ARTSUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || !UnitData)
	{
		return;
	}
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

	UpdateAttackOrder();
	UpdateAttackMoveOrder();
	UpdateHarvestOrder(DeltaSeconds);
	UpdateBuildOrder(DeltaSeconds);

	// Turn toward the direction of travel, or toward the target while standing. Rotation replicates with movement.
	if (Movement->Velocity.SizeSquared2D() > FMath::Square(10.f))
	{
		FaceTowardYaw(Movement->Velocity.Rotation().Yaw, DeltaSeconds);
	}
	else if (const AActor* Target = WeaponComponent->GetTarget())
	{
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		if (!ToTarget.IsNearlyZero())
		{
			FaceTowardYaw(ToTarget.Rotation().Yaw, DeltaSeconds);
		}
	}
}

void ARTSUnit::FaceTowardYaw(float Yaw, float DeltaSeconds)
{
	const FRotator Current = GetActorRotation();
	SetActorRotation(FMath::RInterpConstantTo(Current, FRotator(0.f, Yaw, 0.f), DeltaSeconds, UnitData->TurnRate));
}

void ARTSUnit::ExecuteCommand(const FRTSCommand& InCommand)
{
	check(HasAuthority());

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
	}

	if (!Command.bQueue || bWasIdle)
	{
		StartNextCommand();
	}
}

void ARTSUnit::StepAside(const FVector& Location)
{
	check(HasAuthority());

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
	StartNextCommand();
}

bool ARTSUnit::HoldShortOf(const FBox& Footprint)
{
	check(HasAuthority());

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
}

void ARTSUnit::PopCommand()
{
	BuilderComponent->RefundOrder(CommandQueue[0]);
	CommandQueue.RemoveAt(0);
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
				&& RTSApproach::FindReachableSpot(*this, Building->GetActorLocation(), Building->GetBuildingData()->Footprint,
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

void ARTSUnit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ARTSUnit, UnitData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSUnit, TeamId, COND_InitialOnly);
}
