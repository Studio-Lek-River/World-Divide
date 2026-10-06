#include "Units/RTSUnitSpawner.h"

#include "Commands/RTSFormation.h"
#include "Components/BillboardComponent.h"
#include "Data/RTSUnitData.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

ARTSUnitSpawner::ARTSUnitSpawner()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
#if WITH_EDITORONLY_DATA
	if (UBillboardComponent* Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite")))
	{
		Sprite->SetupAttachment(RootComponent);
	}
#endif
}

void ARTSUnitSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Level actors that don't replicate report authority on clients too, so check the net mode instead.
	if (GetNetMode() == NM_Client)
	{
		return;
	}
	if (!UnitData)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("%s has no UnitData"), *GetName());
		return;
	}

	const TArray<FVector> Slots = RTSFormation::ComputeSlots(
		GetActorLocation(), GetActorRotation().Yaw, Count, UnitData->SelectionRadius * 2.5f);
	const FRotator Facing(0.f, GetActorRotation().Yaw, 0.f);

	for (const FVector& Slot : Slots)
	{
		FTransform Transform(Facing, Slot);
		ARTSUnit* Unit = GetWorld()->SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit)
		{
			continue;
		}
		Unit->InitFromData(UnitData, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
	}
}
