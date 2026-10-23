#include "Buildings/RTSBuildingRuin.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSKitPieceData.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSUnit.h"

namespace
{
	/** The building's height left as rubble. */
	constexpr float CollapsedHeight = 0.12f;

	/** How far the rubble leans as it settles, in degrees. */
	constexpr float CollapseTilt = 3.f;

	/** How much of the team colour is left on the rubble. */
	constexpr float CharredShade = 0.15f;

	/** Below the ground the sunk rubble ends, so none of it shows. */
	constexpr float SinkMargin = 100.f;
}

ARTSBuildingRuin::ARTSBuildingRuin()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);

	bReplicates = true;
	// Every player sees every building for now, so every player sees its fall.
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);
}

ARTSBuildingRuin* ARTSBuildingRuin::SpawnFor(const ARTSBuilding& Building)
{
	UWorld* World = Building.GetWorld();
	URTSBuildingData* Data = Building.GetBuildingData();
	if (!World || !Data || !Building.HasAuthority())
	{
		return nullptr;
	}
	ARTSBuildingRuin* Ruin = World->SpawnActorDeferred<ARTSBuildingRuin>(StaticClass(), Building.GetActorTransform(), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Ruin)
	{
		return nullptr;
	}
	Ruin->BuildingData = Data;
	Ruin->TeamId = Building.GetTeamId();
	Ruin->ConstructionFraction = Building.GetConstructionProgress();
	Ruin->FinishSpawning(Building.GetActorTransform());
	Ruin->SetLifeSpan(Ruin->GetLifetime());
	return Ruin;
}

float ARTSBuildingRuin::GetLifetime() const
{
	return BuildingData ? BuildingData->RuinCollapseSeconds + BuildingData->RuinSeconds + BuildingData->RuinFadeSeconds : 0.f;
}

void ARTSBuildingRuin::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer || !BuildingData)
	{
		SetActorTickEnabled(false);
		return;
	}
	BuildMeshes();
	Pose(0.f);
}

void ARTSBuildingRuin::BuildMeshes()
{
	RTSBuildPlacement::GetMeshPlacement(*BuildingData, MeshBaseOffset, MeshHeight);
	MeshScale = BuildingData->MeshScale * FVector(1.f, 1.f, FMath::Max(ConstructionFraction, 0.05f));
	Mesh->SetStaticMesh(BuildingData->Mesh);

	// Each part on its own, its mounted kit pieces too, without its motion or the flag: the ruin only lives a minute.
	TArray<FRTSBuildingPart> Parts = BuildingData->Parts;
	Parts.Append(RTSKit::Expand(*BuildingData).Parts);
	for (const FRTSBuildingPart& Data : Parts)
	{
		if (!Data.Mesh || Data.Name == BuildingData->FlagPart)
		{
			continue;
		}
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Part->SetStaticMesh(Data.Mesh);
		Part->SetRelativeTransform(Data.Transform);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(Data.bCastShadow);
		Part->SetBoundsScale(Data.BoundsScale);
		Part->SetCullDistance(Data.MaxDrawDistance);
		Part->SetupAttachment(Mesh);
		AddInstanceComponent(Part);
		Part->RegisterComponent();
	}

	if (BuildingData->RuinMesh)
	{
		Rubble = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Rubble->SetStaticMesh(BuildingData->RuinMesh);
		Rubble->SetRelativeScale3D(BuildingData->MeshScale);
		Rubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rubble->SetGenerateOverlapEvents(false);
		Rubble->SetCanEverAffectNavigation(false);
		Rubble->SetVisibility(false);
		Rubble->SetupAttachment(Root);
		AddInstanceComponent(Rubble);
		Rubble->RegisterComponent();
	}

	// Dark and burnt out: the team colour all but gone, every lamp, window and glow out.
	const FVector4 Charred(ARTSUnit::GetTeamColor(TeamId) * CharredShade);
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents(Meshes);
	for (UStaticMeshComponent* Each : Meshes)
	{
		Each->SetCustomPrimitiveDataVector4(RTSPrimitiveData::TeamColor, Charred);
		Each->SetCustomPrimitiveDataFloat(RTSPrimitiveData::LampOn, 0.f);
		for (int32 Index = RTSPrimitiveData::LightWindows; Index <= RTSPrimitiveData::LightNight; ++Index)
		{
			Each->SetCustomPrimitiveDataFloat(Index, 0.f);
		}
	}
}

void ARTSBuildingRuin::Pose(float InAge)
{
	Age = InAge;
	if (!BuildingData)
	{
		return;
	}
	const float Collapse = FMath::Clamp(Age / BuildingData->RuinCollapseSeconds, 0.f, 1.f);
	const float FadeStart = BuildingData->RuinCollapseSeconds + BuildingData->RuinSeconds;
	const float Fade = FMath::Clamp((Age - FadeStart) / BuildingData->RuinFadeSeconds, 0.f, 1.f);

	// Slow at first, then all at once, as a building gives way.
	const float Fall = Collapse * Collapse;
	const float Height = FMath::Lerp(1.f, CollapsedHeight, Fall);
	const float Sink = Fade * (MeshHeight * CollapsedHeight + SinkMargin);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, MeshBaseOffset * Height - Sink), FRotator(0.f, 0.f, CollapseTilt * Fall));
	Mesh->SetRelativeScale3D(MeshScale * FVector(1.f, 1.f, Height));

	if (Rubble)
	{
		Mesh->SetVisibility(Collapse < 1.f, /*bPropagateToChildren*/ true);
		Rubble->SetVisibility(Collapse >= 1.f);
		Rubble->SetRelativeLocation(FVector(0.f, 0.f, MeshBaseOffset - Sink));
	}
}

void ARTSBuildingRuin::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Pose(Age + DeltaSeconds);
	// Nothing moves while the rubble lies; the tick comes back for the sinking.
	if (Age >= BuildingData->RuinCollapseSeconds && Age < BuildingData->RuinCollapseSeconds + BuildingData->RuinSeconds)
	{
		SetActorTickEnabled(false);
		const float Resume = BuildingData->RuinCollapseSeconds + BuildingData->RuinSeconds - Age;
		Age += Resume;
		GetWorldTimerManager().SetTimer(FadeTimer, this, &ThisClass::ResumeTick, FMath::Max(Resume, KINDA_SMALL_NUMBER));
	}
}

void ARTSBuildingRuin::ResumeTick()
{
	SetActorTickEnabled(true);
}

void ARTSBuildingRuin::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ARTSBuildingRuin, BuildingData, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSBuildingRuin, TeamId, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARTSBuildingRuin, ConstructionFraction, COND_InitialOnly);
}
