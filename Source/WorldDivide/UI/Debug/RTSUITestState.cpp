#include "UI/Debug/RTSUITestState.h"

#if !UE_BUILD_SHIPPING

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSHarvesterData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSUnit.h"

const FVector RTSUITestState::Center(0.f, 7000.f, 0.f);
const FName RTSUITestState::SpawnedTag(TEXT("RTSUITestState"));

namespace
{
	/** The buildings stand in a row behind the units, this far apart, the first at RowStart. */
	const FVector BuildingRowStart(-1400.f, 800.f, 0.f);
	constexpr float BuildingSpacing = 700.f;
	const FVector UnitRowStart(-750.f, -300.f, 0.f);
	constexpr float UnitSpacing = 300.f;
	/** More towers than this and the faction's power draw is too small for the state. */
	constexpr int32 MaxPowerDrawBuildings = 8;
	constexpr float GroundSearchHeight = 5000.f;

	const FName QueueBuildingTag(TEXT("RTSUITestState.Queue"));

	/** Where Offset from Center meets the ground, or Center's height without ground there. */
	FVector OnGround(const UWorld& World, const FVector& Offset)
	{
		FVector Location = RTSUITestState::Center + Offset;
		FHitResult Hit;
		const FVector Up(0.f, 0.f, GroundSearchHeight);
		if (World.LineTraceSingleByObjectType(Hit, Location + Up, Location - Up, ECC_WorldStatic))
		{
			Location.Z = Hit.ImpactPoint.Z;
		}
		return Location;
	}

	ARTSBuilding* SpawnConstructed(UWorld& World, URTSBuildingData& Data, int32 TeamId, const FVector& Location)
	{
		const FTransform Transform(Location);
		ARTSBuilding* Building = World.SpawnActorDeferred<ARTSBuilding>(
			ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Building)
		{
			return nullptr;
		}
		Building->Tags.Add(RTSUITestState::SpawnedTag);
		Building->InitFromData(&Data, TeamId);
		Building->FinishSpawning(Transform);
		// Enough to complete it even while the team is low on power.
		Building->UpdateConstruction(Data.BuildTime / ARTSPlayerState::LowPowerProductionRate);
		return Building;
	}

	ARTSUnit* SpawnUnit(UWorld& World, URTSUnitData& Data, int32 TeamId, const FVector& Location)
	{
		FTransform Transform(Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit)
		{
			return nullptr;
		}
		Unit->Tags.Add(RTSUITestState::SpawnedTag);
		Unit->InitFromData(&Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	/** Units that stand where they are put: no builder, harvester or aircraft. */
	bool IsPlainUnit(const URTSUnitData* Data)
	{
		return Data && !Data->IsA<URTSBuilderData>() && !Data->IsA<URTSHarvesterData>() && !Data->IsA<URTSAircraftData>();
	}

	/** What the state needs from the faction, found in its buildings and their production lists. */
	struct FStateData
	{
		URTSBuilderData* Builder = nullptr;
		URTSBuildingData* PowerPlant = nullptr;
		URTSBuildingData* PowerDraw = nullptr;
		URTSBuildingData* Barracks = nullptr;
		TArray<URTSUnitData*> Units;
	};

	FStateData FindStateData(const URTSFactionData& Faction)
	{
		FStateData Found;
		TArray<URTSBuildingData*> Buildings;
		for (URTSBuildingData* Building : Faction.Buildings)
		{
			Buildings.AddUnique(Building);
		}
		Buildings.AddUnique(Faction.Headquarters);
		for (URTSBuildingData* Building : Buildings)
		{
			if (!Building)
			{
				continue;
			}
			if (!Found.PowerPlant && Building->PowerOutput > 0)
			{
				Found.PowerPlant = Building;
			}
			// A defence draws power and produces nothing, so it adds no menu of its own.
			if (!Found.PowerDraw && Building->PowerDraw > 0 && Building->Units.IsEmpty() && Building->Upgrades.IsEmpty())
			{
				Found.PowerDraw = Building;
			}
			for (URTSUnitData* Unit : Building->Units)
			{
				if (!Found.Builder && Unit && Unit->IsA<URTSBuilderData>())
				{
					Found.Builder = Cast<URTSBuilderData>(Unit);
				}
				if (IsPlainUnit(Unit))
				{
					Found.Units.AddUnique(Unit);
					if (!Found.Barracks && Building != Faction.Headquarters)
					{
						Found.Barracks = Building;
					}
				}
			}
		}
		return Found;
	}
}

bool RTSUITestState::Apply(ARTSPlayerController& Controller, FString& OutError)
{
	UWorld* World = Controller.GetWorld();
	ARTSPlayerState* Player = Controller.GetPlayerState<ARTSPlayerState>();
	if (!World || World->GetNetMode() == NM_Client || !Player)
	{
		OutError = TEXT("The UI test state needs the host's or a standalone game's player");
		return false;
	}
	const URTSFactionData* Faction = Player->GetFaction();
	const FStateData Data = Faction ? FindStateData(*Faction) : FStateData();
	if (!Data.Builder || !Data.PowerPlant || !Data.PowerDraw || !Data.Barracks || Data.Units.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Faction %s lacks a builder, a power plant, a building that draws power or one that produces units"),
			*GetNameSafe(Faction));
		return false;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(SpawnedTag))
		{
			It->Destroy();
		}
	}
	Player->RecalculatePower();

	const int32 TeamId = Player->GetTeamId();
	int32 Slot = 0;
	auto NextBuildingSpot = [&]() { return OnGround(*World, BuildingRowStart + FVector(BuildingSpacing * Slot++, 0.f, 0.f)); };

	SpawnConstructed(*World, *Data.PowerPlant, TeamId, NextBuildingSpot());
	ARTSBuilding* Barracks = SpawnConstructed(*World, *Data.Barracks, TeamId, NextBuildingSpot());
	ARTSBuilding* LastDraw = nullptr;
	Player->RecalculatePower();
	for (int32 Index = 0; Index < MaxPowerDrawBuildings && !Player->IsLowPower(); ++Index)
	{
		LastDraw = SpawnConstructed(*World, *Data.PowerDraw, TeamId, NextBuildingSpot());
		Player->RecalculatePower();
	}
	if (!Barracks || !Player->IsLowPower())
	{
		OutError = TEXT("The UI test state could not put the player on low power");
		return false;
	}

	// Enough for the queue, then exactly the state's credits.
	Player->AddCredits(Credits * 10);
	Barracks->Tags.Add(QueueBuildingTag);
	// Its units in turn, those it may produce yet: one may need an upgrade first.
	const TArray<TObjectPtr<URTSUnitData>>& Producible = Data.Barracks->Units;
	URTSProductionComponent* Production = Barracks->GetProductionComponent();
	for (int32 Index = 0; Index < QueuedItems * Producible.Num() && Production->GetQueue().Num() < QueuedItems; ++Index)
	{
		Production->Enqueue(*Player, Producible[Index % Producible.Num()]);
	}
	if (Production->GetQueue().Num() < QueuedItems)
	{
		OutError = FString::Printf(TEXT("%s queued only %d units"), *Data.Barracks->GetName(), Production->GetQueue().Num());
		return false;
	}
	Player->AddCredits(Credits - Player->GetCredits());

	TArray<ARTSUnit*> Selection;
	for (int32 Index = 0; Index < SelectedUnits; ++Index)
	{
		URTSUnitData* UnitData = Index == 0 ? Data.Builder : Data.Units[(Index - 1) % Data.Units.Num()];
		const FVector Location = OnGround(*World, UnitRowStart + FVector(UnitSpacing * Index, 0.f, 0.f));
		if (ARTSUnit* Unit = SpawnUnit(*World, *UnitData, TeamId, Location))
		{
			Selection.Add(Unit);
		}
	}
	Controller.SelectUnits(Selection);

	Controller.NotifyAttacked((LastDraw ? LastDraw : Barracks)->GetActorLocation(), ERTSAttackAlert::Base);
	if (ARTSCameraPawn* Camera = Controller.GetPawn<ARTSCameraPawn>())
	{
		Camera->JumpTo(Center);
	}
	return true;
}

ARTSBuilding* RTSUITestState::FindQueueBuilding(const UWorld& World)
{
	for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
	{
		if (It->ActorHasTag(QueueBuildingTag))
		{
			return *It;
		}
	}
	return nullptr;
}

#endif
