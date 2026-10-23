#include "Buildings/RTSCivilBuilding.h"

#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

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
	}
	ShowState();
}

void ARTSCivilBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARTSCivilBuilding, State);
}

void ARTSCivilBuilding::HandleDamaged(float Amount, AActor* DamageInstigator)
{
	UpdateState();
	ForceNetUpdate();
}

void ARTSCivilBuilding::OnRep_State()
{
	ShowState();
}

void ARTSCivilBuilding::UpdateState()
{
	check(HasAuthority());
	const ERTSCivilBuildingState Resolved = ResolveState(HealthComponent->GetHealthFraction(),
		Data ? Data->DamagedHealthFraction : 0.5f);
	if (Resolved != State)
	{
		State = Resolved;
		ShowState();
	}
}

void ARTSCivilBuilding::ShowState()
{
	UStaticMesh* Wanted = Data ? Data->GetMesh(State) : nullptr;
	if (Mesh->GetStaticMesh() != Wanted)
	{
		Mesh->SetStaticMesh(Wanted);
	}
}
