#include "Units/RTSUnit.h"

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
	Health = GetMaxHealth();
	ApplyUnitData();
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

float ARTSUnit::GetMaxHealth() const
{
	return UnitData ? UnitData->MaxHealth : 1.f;
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

	// Turn toward the direction of travel. Rotation replicates with movement.
	if (HasAuthority() && UnitData && Movement->Velocity.SizeSquared2D() > FMath::Square(10.f))
	{
		const FRotator Current = GetActorRotation();
		const FRotator Target(0.f, Movement->Velocity.Rotation().Yaw, 0.f);
		SetActorRotation(FMath::RInterpConstantTo(Current, Target, DeltaSeconds, UnitData->TurnRate));
	}
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
		return;
	}

	while (!CommandQueue.IsEmpty())
	{
		const FRTSCommand& Command = CommandQueue[0];
		if (Command.Type == ERTSCommandType::Move)
		{
			// The AI controller may report AlreadyAtGoal or a failure back through NotifyMoveFinished
			// before MoveToLocation returns; that is handled here instead of re-entering the queue.
			bRequestingMove = true;
			bMoveFinishedDuringRequest = false;
			const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
				Command.TargetLocation, MoveAcceptanceRadius,
				/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
				/*bCanStrafe*/ true, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
			bRequestingMove = false;

			if (Result == EPathFollowingRequestResult::RequestSuccessful && !bMoveFinishedDuringRequest)
			{
				return;
			}
			if (Result == EPathFollowingRequestResult::Failed)
			{
				UE_LOG(LogWorldDivide, Verbose, TEXT("%s could not path to %s"), *GetName(), *Command.TargetLocation.ToString());
			}
		}
		CommandQueue.RemoveAt(0);
	}
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
	DOREPLIFETIME(ARTSUnit, Health);
}
