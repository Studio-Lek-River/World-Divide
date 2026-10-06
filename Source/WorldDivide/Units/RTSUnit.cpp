#include "Units/RTSUnit.h"

#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Navigation/PathFollowingComponent.h"
#include "Net/UnrealNetwork.h"
#include "Units/RTSUnitAIController.h"
#include "WorldDivide.h"

namespace
{
	/** Engine basic shapes expose this parameter; real unit materials should too. */
	const FName TeamColorParameter(TEXT("Color"));

	constexpr float MoveAcceptanceRadius = 25.f;

	/** Attackers stop this far inside their range so a slowly retreating target does not slip straight out again. */
	constexpr float AttackApproachFraction = 0.8f;
}

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
	WeaponComponent->SetWeaponData(UnitData ? UnitData->Weapon : nullptr);
	ApplyUnitData();
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
	if (Mesh->GetStaticMesh())
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0))
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

	UpdateAttackOrder();

	// Turn toward the direction of travel, or toward the target while standing. Rotation replicates with movement.
	if (Movement->Velocity.SizeSquared2D() > FMath::Square(10.f))
	{
		FaceTowardYaw(Movement->Velocity.Rotation().Yaw, DeltaSeconds);
	}
	else if (const ARTSUnit* Target = WeaponComponent->GetTarget())
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

void ARTSUnit::ExecuteCommand(const FRTSCommand& Command)
{
	check(HasAuthority());

	if (Command.Type == ERTSCommandType::Stop)
	{
		CommandQueue.Reset();
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
		CommandQueue.Reset();
	}
	CommandQueue.Add(Command);

	if (!Command.bQueue || bWasIdle)
	{
		StartNextCommand();
	}
}

void ARTSUnit::StartNextCommand()
{
	AAIController* AI = GetController<AAIController>();
	if (!AI)
	{
		CommandQueue.Reset();
	}

	while (!CommandQueue.IsEmpty() && !StartCommand(*AI, CommandQueue[0]))
	{
		CommandQueue.RemoveAt(0);
	}
	ApplyWeaponMode();
}

bool ARTSUnit::StartCommand(AAIController& AI, const FRTSCommand& Command)
{
	if (Command.Type == ERTSCommandType::Move)
	{
		// The AI controller may report AlreadyAtGoal or a failure back through NotifyMoveFinished
		// before MoveToLocation returns; that is handled here instead of re-entering the queue.
		bRequestingMove = true;
		bMoveFinishedDuringRequest = false;
		const EPathFollowingRequestResult::Type Result = AI.MoveToLocation(
			Command.TargetLocation, MoveAcceptanceRadius,
			/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
			/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
		bRequestingMove = false;

		if (Result == EPathFollowingRequestResult::Failed)
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("%s could not path to %s"), *GetName(), *Command.TargetLocation.ToString());
		}
		return Result == EPathFollowingRequestResult::RequestSuccessful && !bMoveFinishedDuringRequest;
	}

	if (Command.Type == ERTSCommandType::Attack)
	{
		// Clients pick the target, so the server checks it: armed, alive, and not on our side.
		const ARTSUnit* Target = Cast<ARTSUnit>(Command.TargetActor);
		if (!WeaponComponent->IsArmed() || !WeaponComponent->CanTarget(Target))
		{
			return false;
		}
		// Drop whatever move was running; UpdateAttackOrder starts the approach on the next tick.
		AI.StopMovement();
		return true;
	}

	return false;
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

	ARTSUnit* Target = Cast<ARTSUnit>(CommandQueue[0].TargetActor);
	if (!WeaponComponent->CanTarget(Target))
	{
		AI->StopMovement();
		CommandQueue.RemoveAt(0);
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
		// Following the actor keeps the path updated while the target moves.
		AI->MoveToActor(Target, WeaponComponent->GetRange() * AttackApproachFraction,
			/*bStopOnOverlap*/ true, /*bUsePathfinding*/ true, /*bCanStrafe*/ true, /*FilterClass*/ nullptr,
			/*bAllowPartialPath*/ true);
	}
}

void ARTSUnit::ApplyWeaponMode()
{
	const FRTSCommand* Current = CommandQueue.IsEmpty() ? nullptr : &CommandQueue[0];
	const bool bAttacking = Current && Current->Type == ERTSCommandType::Attack;
	WeaponComponent->SetOrderedTarget(bAttacking ? Cast<ARTSUnit>(Current->TargetActor) : nullptr);
	// Units on the move ignore enemies; attack-move will change that.
	WeaponComponent->SetAutoAcquire(Current == nullptr);
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
	// UpdateAttackOrder starts and stops the approach itself and ends the order when the target dies.
	if (!CommandQueue.IsEmpty() && CommandQueue[0].Type == ERTSCommandType::Attack)
	{
		return;
	}

	if (!CommandQueue.IsEmpty())
	{
		CommandQueue.RemoveAt(0);
	}
	StartNextCommand();
}

void ARTSUnit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ARTSUnit, UnitData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSUnit, TeamId, COND_InitialOnly);
}
