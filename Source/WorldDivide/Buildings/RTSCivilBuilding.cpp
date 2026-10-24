#include "Buildings/RTSCivilBuilding.h"

#include "AI/NavigationSystemBase.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/RTSGameState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Units/RTSUnit.h"

namespace
{
	/** A unit still stands when it finds ground this far above and below its feet: a span's pier stub, an abutment. */
	constexpr float StandTraceUp = 100.f;
	constexpr float StandTraceDown = 200.f;
}

ARTSCivilBuilding::ARTSCivilBuilding()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetGenerateOverlapEvents(false);
	// Its collision carves the navmesh like any wall; a map keeps a roof free of navmesh with a null nav volume, as the
	// greybox's blocks have.
	Mesh->SetCanEverAffectNavigation(true);

	HealthComponent = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));

	bReplicates = true;
	// Its state changes a few times a match at most; damage pushes an update at once.
	SetNetUpdateFrequency(1.f);
	bAlwaysRelevant = true;
}

ERTSCivilBuildingState ARTSCivilBuilding::ResolveState(float HealthFraction, float DamagedFraction)
{
	if (HealthFraction <= 0.f)
	{
		return ERTSCivilBuildingState::Rubble;
	}
	return HealthFraction <= DamagedFraction ? ERTSCivilBuildingState::Damaged : ERTSCivilBuildingState::Pristine;
}

FVector ARTSCivilBuilding::GetClosestFootprintPoint(const FVector& From) const
{
	const FVector Center = GetActorLocation();
	if (!Mesh->GetStaticMesh())
	{
		return Center;
	}
	FVector Point = Mesh->Bounds.GetBox().GetClosestPointTo(From);
	Point.Z = Center.Z;
	return Point;
}

void ARTSCivilBuilding::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// In the editor and at load: the state the map starts it in.
	State = StartState;
	ShowState();
}

void ARTSCivilBuilding::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && Data)
	{
		HealthComponent->InitHealth(Data->MaxHealth);
		// Before the damage handler is bound, so a map's ruin starts as one without a death or a damage event.
		if (StartState != ERTSCivilBuildingState::Pristine)
		{
			const float Fraction = StartState == ERTSCivilBuildingState::Rubble ? 1.f : 1.f - Data->DamagedHealthFraction;
			HealthComponent->ApplyDamage(Data->MaxHealth * Fraction, nullptr);
		}
		HealthComponent->OnDamaged.AddDynamic(this, &ARTSCivilBuilding::HandleDamaged);
		UpdateState();
		// The game state exists by BeginPlay; listed, area damage finds it without scanning every actor.
		if (ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>())
		{
			GameState->RegisterCivilBuilding(*this);
		}
	}
	ShowState();
}

void ARTSCivilBuilding::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ARTSGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ARTSGameState>() : nullptr)
	{
		GameState->UnregisterCivilBuilding(*this);
	}
	Super::EndPlay(EndPlayReason);
}

void ARTSCivilBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARTSCivilBuilding, State);
}

void ARTSCivilBuilding::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	UpdateState(DamageInstigator);
	ForceNetUpdate();
}

void ARTSCivilBuilding::OnRep_State()
{
	ShowState();
}

void ARTSCivilBuilding::UpdateState(AActor* DamageInstigator)
{
	check(HasAuthority());
	const ERTSCivilBuildingState Resolved = ResolveState(HealthComponent->GetHealthFraction(),
		Data ? Data->DamagedHealthFraction : 0.5f);
	if (Resolved != State)
	{
		const FBox Bounds = Mesh->Bounds.GetBox();
		State = Resolved;
		ShowState();
		if (State == ERTSCivilBuildingState::Rubble && HasActorBegunPlay())
		{
			KillUnitsWithoutGround(Bounds, DamageInstigator);
		}
	}
}

void ARTSCivilBuilding::KillUnitsWithoutGround(const FBox& Bounds, AActor* DamageInstigator)
{
	UWorld* World = GetWorld();
	// The scene's query structure still holds the old collision until physics runs, so the rubble's own collision is traced
	// directly and the world's ground without this building.
	static const FName TraceTag(TEXT("RTSCivilBuildingStand"));
	FCollisionQueryParams Params(TraceTag);
	Params.AddIgnoredActor(this);
	const FBox Held = Bounds.ExpandBy(FVector(0.f, 0.f, StandTraceUp), FVector(0.f, 0.f, StandTraceUp));
	TArray<ARTSUnit*> Fallen;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		ARTSUnit* Unit = *It;
		const FVector Feet = Unit->GetNavAgentLocation();
		if (!Held.IsInside(Feet) || Unit->IsAirborne() || Unit->GetHealthComponent()->IsDead())
		{
			continue;
		}
		const FVector Start = Feet + FVector(0.f, 0.f, StandTraceUp);
		const FVector End = Feet - FVector(0.f, 0.f, StandTraceDown);
		FHitResult Hit;
		if (Mesh->LineTraceComponent(Hit, Start, End, FCollisionQueryParams(TraceTag))
			|| World->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			continue;
		}
		Fallen.Add(Unit);
	}
	for (ARTSUnit* Unit : Fallen)
	{
		URTSHealthComponent* Health = Unit->GetHealthComponent();
		Health->ApplyDamage(Health->GetHealth(), DamageInstigator);
	}
}

void ARTSCivilBuilding::ShowState()
{
	UStaticMesh* Wanted = Data ? Data->GetMesh(State) : nullptr;
	if (Mesh->GetStaticMesh() != Wanted)
	{
		Mesh->SetStaticMesh(Wanted);
		// The navmesh rebuilds at runtime: a fallen bridge span's lane goes, and paths across it are found again.
		FNavigationSystem::UpdateComponentData(*Mesh);
	}
}
