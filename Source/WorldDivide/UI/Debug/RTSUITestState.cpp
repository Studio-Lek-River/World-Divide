#include "UI/Debug/RTSUITestState.h"

#if !UE_BUILD_SHIPPING

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSHarvesterData.h"
#include "Data/RTSUpgradeData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "UObject/UObjectIterator.h"

const FVector RTSUITestState::Center(0.f, 21000.f, 0.f);
const FName RTSUITestState::SpawnedTag(TEXT("RTSUITestState"));

namespace
{
	/**
	 * The buildings stand in a row along X behind the units, their footprints BuildingGap apart, the first from RowStart; the
	 * units stand in a row in front, their hulls UnitGap apart.
	 */
	const FVector BuildingRowStart(-3000.f, 800.f, 0.f);
	constexpr float BuildingGap = 800.f;
	const FVector UnitRowStart(-1500.f, -600.f, 0.f);
	constexpr float UnitGap = 300.f;
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
	float BuildingRow = 0.f;
	auto NextBuildingSpot = [&](const URTSBuildingData& Building)
	{
		const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Building.Footprint);
		const FVector Spot = BuildingRowStart + FVector(BuildingRow + Extent.X, Extent.Y, 0.f);
		BuildingRow += 2.f * Extent.X + BuildingGap;
		return OnGround(*World, Spot);
	};

	SpawnConstructed(*World, *Data.PowerPlant, TeamId, NextBuildingSpot(*Data.PowerPlant));
	ARTSBuilding* Barracks = SpawnConstructed(*World, *Data.Barracks, TeamId, NextBuildingSpot(*Data.Barracks));
	ARTSBuilding* LastDraw = nullptr;
	Player->RecalculatePower();
	for (int32 Index = 0; Index < MaxPowerDrawBuildings && !Player->IsLowPower(); ++Index)
	{
		LastDraw = SpawnConstructed(*World, *Data.PowerDraw, TeamId, NextBuildingSpot(*Data.PowerDraw));
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
	float UnitRow = 0.f;
	for (int32 Index = 0; Index < SelectedUnits; ++Index)
	{
		URTSUnitData* UnitData = Index == 0 ? Data.Builder : Data.Units[(Index - 1) % Data.Units.Num()];
		const FRTSUnitShape Shape = RTSUnitShape::FromData(*UnitData);
		const FVector Location = OnGround(*World, UnitRowStart + FVector(UnitRow + Shape.GetHalfLength(), 0.f, 0.f));
		UnitRow += 2.f * Shape.GetHalfLength() + UnitGap;
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

namespace
{
	/** Where ApplyScene puts the one unit it selects: in front of Apply's row, in the camera's view. */
	const FVector SceneUnitSpot(0.f, -2200.f, 0.f);

	/** Sprites every faction resolves (Common), so the fixture upgrades look apart. */
	const TCHAR* FixtureIcons[] = {
		TEXT("Icon.RifleTraining"), TEXT("Icon.Tank"), TEXT("Icon.Power"), TEXT("Icon.RankStar"), TEXT("Icon.Overlays"),
		TEXT("Icon.Objectives"), TEXT("Icon.GeneralPowers"), TEXT("Icon.Credits"), TEXT("Icon.Lock"),
	};

	/** Completes Count new upgrades for Player that target Data, made at run time. */
	void InstallFixtureUpgrades(ARTSPlayerState& Player, URTSEntityData& Data, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			URTSUpgradeData* Upgrade = NewObject<URTSUpgradeData>(GetTransientPackage());
			Upgrade->DisplayName = FText::FromString(FString::Printf(TEXT("Fixture Upgrade %d"), Index + 1));
			Upgrade->Description = FText::FromString(TEXT("A screenshot fixture: an installed upgrade."));
			Upgrade->IconSprite = FixtureIcons[Index % UE_ARRAY_COUNT(FixtureIcons)];
			Upgrade->Stat = Index % 2 ? ERTSUpgradeStat::MaxHealth : ERTSUpgradeStat::WeaponDamage;
			Upgrade->Multiplier = 1.f + 0.05f * (Index + 1);
			Upgrade->Targets.Add(&Data);
			Player.CompleteUpgrade(*Upgrade);
		}
	}

	URTSUnitData* FindHeroUnitData()
	{
		for (TObjectIterator<URTSUnitData> It; It; ++It)
		{
			if (!It->HeroArtSprite.IsNone() && !It->HasAnyFlags(RF_ClassDefaultObject | RF_Transient))
			{
				return *It;
			}
		}
		return nullptr;
	}

	URTSAircraftData* FindAircraftData(const URTSFactionData& Faction)
	{
		for (const URTSBuildingData* Building : Faction.Buildings)
		{
			if (!Building)
			{
				continue;
			}
			for (URTSUnitData* Unit : Building->Units)
			{
				if (URTSAircraftData* Aircraft = Cast<URTSAircraftData>(Unit))
				{
					return Aircraft;
				}
			}
		}
		return nullptr;
	}
}

bool RTSUITestState::ApplyScene(ARTSPlayerController& Controller, const FString& Scene, FString& OutError)
{
	UWorld* World = Controller.GetWorld();
	ARTSPlayerState* Player = Controller.GetPlayerState<ARTSPlayerState>();
	const URTSFactionData* Faction = Player ? Player->GetFaction() : nullptr;
	if (!World || !Faction)
	{
		OutError = TEXT("The UI test state needs the host's or a standalone game's player");
		return false;
	}
	const FStateData Data = FindStateData(*Faction);

	if (Scene.Equals(TEXT("Building"), ESearchCase::IgnoreCase))
	{
		ARTSBuilding* Barracks = FindQueueBuilding(*World);
		if (!Barracks || !Controller.SelectBuilding(Barracks))
		{
			OutError = TEXT("Scene Building needs Apply's barracks");
			return false;
		}
		InstallFixtureUpgrades(*Player, *Barracks->GetBuildingData(), 2);
		return true;
	}

	URTSUnitData* UnitData = nullptr;
	int32 Upgrades = 0;
	if (Scene.StartsWith(TEXT("Hero"), ESearchCase::IgnoreCase) && Scene.Len() > 4 && Scene.Mid(4).IsNumeric())
	{
		UnitData = FindHeroUnitData();
		Upgrades = FCString::Atoi(*Scene.Mid(4));
	}
	else if (Scene.Equals(TEXT("LongLabel"), ESearchCase::IgnoreCase))
	{
		// A copy, so the long texts never reach the shared data.
		const URTSUnitData* Hero = FindHeroUnitData();
		UnitData = Hero ? DuplicateObject<URTSUnitData>(Hero, GetTransientPackage()) : nullptr;
		if (UnitData)
		{
			UnitData->DisplayName = FText::FromString(TEXT("Schwerer Kampfpanzer M20 Guardian Ausführung A2"));
			UnitData->RoleText = FText::FromString(TEXT("Hauptkampfpanzer der Panzergrenadierbrigade"));
			UnitData->Description = FText::FromString(
				TEXT("A deliberately long localised description that has to end in an ellipsis instead of pushing the upgrades away."));
		}
		Upgrades = 6;
	}
	else if (Scene.Equals(TEXT("Infantry"), ESearchCase::IgnoreCase))
	{
		UnitData = Data.Units.IsEmpty() ? nullptr : Data.Units[0];
		Upgrades = 2;
	}
	else if (Scene.Equals(TEXT("Vehicle"), ESearchCase::IgnoreCase))
	{
		UnitData = Data.Units.Num() < 2 ? nullptr : Data.Units.Last();
		Upgrades = 3;
	}
	else if (Scene.Equals(TEXT("Aircraft"), ESearchCase::IgnoreCase))
	{
		UnitData = FindAircraftData(*Faction);
		Upgrades = 1;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown scene %s: Hero<N>, LongLabel, Infantry, Vehicle, Aircraft or Building"), *Scene);
		return false;
	}
	if (!UnitData)
	{
		OutError = FString::Printf(TEXT("Scene %s finds no unit for it in faction %s or the loaded data"), *Scene, *Faction->GetName());
		return false;
	}

	ARTSUnit* Unit = SpawnUnit(*World, *UnitData, Player->GetTeamId(), OnGround(*World, SceneUnitSpot));
	if (!Unit)
	{
		OutError = FString::Printf(TEXT("Scene %s could not spawn %s"), *Scene, *UnitData->GetName());
		return false;
	}
	InstallFixtureUpgrades(*Player, *UnitData, Upgrades);
	Controller.SelectUnits({ Unit });
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
