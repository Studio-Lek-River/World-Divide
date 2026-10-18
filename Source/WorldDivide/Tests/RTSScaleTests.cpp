#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSAIBuildOrderData.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSHarvesterData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Misc/PackageName.h"
#include "GameFramework/PlayerStart.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSHullClearance.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestGeometry.h"
#include "Tests/RTSTestPIE.h"
#include "Tests/RTSTestScale.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

/**
 * The real scale of WORLD_SCALE_STANDARD.md (#136). The dimension test holds every placeholder unit and building to the
 * standard's size on the axis that binds its fit, and checks the sizes against each other: infantry next to the M20, light
 * vehicles under the battle tank, buildings at their compressed heights. The exit test spawns every unit each producer sends
 * out at its exit and checks the hull, not just the nav agent, clears the building and the rally point lies past it. The dock
 * test follows a harvester through two loads on L_TestMap and checks its hull never overlaps the depot or the source. The
 * lane test drives a tank's hull along paths between every two bases and from each base to every supply source of every
 * generated map, as the hull clearance fits them, and checks the hull clears every obstacle on the way and every obstacle
 * gap is a vehicle lane. The AI-versus-AI test plays L_SkirmishDuel with both sides run by the skirmish AI until one wins.
 */
namespace RTSScaleTests
{
	using RTSTests::TestFlags;

	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	const TCHAR* HarvesterPath = TEXT("/Game/Data/Units/DA_TestHarvester.DA_TestHarvester");
	const TCHAR* SupplyDepotPath = TEXT("/Game/Data/Buildings/DA_TestSupplyDepot.DA_TestSupplyDepot");
	const TCHAR* FactionPath = TEXT("/Game/Data/Factions/DA_FactionUSA.DA_FactionUSA");
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* DuelMap = TEXT("/Game/Maps/L_SkirmishDuel");

	/** Every placeholder matches the standard within this share on its binding axis (#136 acceptance). */
	constexpr float SizeTolerance = 0.1f;
	/** The M20's hull as exported and checked on import (WORLD_SCALE_STANDARD.md section 3), and its tolerance. */
	constexpr float M20HullLength = 710.2f;
	constexpr float M20HullTolerance = 0.01f;
	/** A real truck the supply depot's dock side must take (WORLD_SCALE_STANDARD.md section 3, supply depot). */
	constexpr float StandardTruckLength = 1000.f;
	/** The margin on either side of a vehicle in an exit lane (WORLD_SCALE_STANDARD.md rule 1). */
	constexpr float ExitLaneMargin = 200.f;
	/** A hull may touch an obstacle by this much at most: the hull box against the obstacle's box. */
	constexpr float OverlapTolerance = 2.f;

	enum class EAxis : uint8 { Length, Width, Height };

	/** A placeholder unit, the standard's size for its class on the axis its fit binds, in cm. */
	struct FUnitStandard
	{
		const TCHAR* Path;
		EAxis Axis;
		float Size;
	};

	const FUnitStandard UnitStandards[] = {
		{ TankPath, EAxis::Width, 370.f },
		{ TEXT("/Game/Data/Units/DA_TestAbrams.DA_TestAbrams"), EAxis::Width, 370.f },
		{ InfantryPath, EAxis::Height, 180.f },
		{ TEXT("/Game/Data/Units/DA_TestGrenadier.DA_TestGrenadier"), EAxis::Height, 180.f },
		{ TEXT("/Game/Data/Units/DA_TestFlakTruck.DA_TestFlakTruck"), EAxis::Width, 250.f },
		{ HarvesterPath, EAxis::Width, 250.f },
		{ TEXT("/Game/Data/Units/DA_TestBuilder.DA_TestBuilder"), EAxis::Width, 300.f },
		{ TEXT("/Game/Data/Units/DA_TestJet.DA_TestJet"), EAxis::Width, 700.f },
	};

	/** A placeholder building: the standard's footprint in 1 m cells and its height band, in cm. */
	struct FBuildingStandard
	{
		const TCHAR* Path;
		FIntPoint Footprint;
		float MinHeight;
		float MaxHeight;
	};

	const FBuildingStandard BuildingStandards[] = {
		{ TEXT("/Game/Data/Buildings/DA_TestHeadquarters.DA_TestHeadquarters"), FIntPoint(28, 28), 1800.f, 2500.f },
		{ TEXT("/Game/Data/Buildings/DA_TestVehicleFactory.DA_TestVehicleFactory"), FIntPoint(24, 24), 1200.f, 1400.f },
		{ TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant"), FIntPoint(20, 20), 1200.f, 1600.f },
		{ TEXT("/Game/Data/Buildings/DA_TestBarracks.DA_TestBarracks"), FIntPoint(16, 16), 600.f, 800.f },
		{ SupplyDepotPath, FIntPoint(16, 16), 800.f, 1000.f },
		{ TEXT("/Game/Data/Buildings/DA_TestSuperweapon.DA_TestSuperweapon"), FIntPoint(24, 24), 3000.f, 3500.f },
		{ TEXT("/Game/Data/Buildings/DA_TestTower.DA_TestTower"), FIntPoint(6, 6), 1000.f, 1200.f },
		{ TEXT("/Game/Data/Buildings/DA_TestAirfield.DA_TestAirfield"), FIntPoint(24, 48), 100.f, 200.f },
	};

	/** The standard's supply source: 12 x 12 m, 2 to 4 m high. */
	const FIntPoint SupplySourceFootprint(12, 12);
	constexpr float SupplySourceMinHeight = 200.f;
	constexpr float SupplySourceMaxHeight = 400.f;

	/** The generated maps the lane test drives: the test map and every skirmish map. */
	const TCHAR* LaneMaps[] = {
		TEXT("/Game/Maps/L_TestMap"),
		TEXT("/Game/Maps/L_Skirmish"),
		TEXT("/Game/Maps/L_SkirmishDuel"),
		TEXT("/Game/Maps/L_RiverFord"),
		TEXT("/Game/Maps/L_QuarryCross"),
		TEXT("/Game/Maps/L_SixPeaks"),
	};
	/** Paths are sampled this often along their way, in cm. */
	constexpr float LaneSampleStep = 100.f;
	constexpr double NavTimeout = 60.0;

	bool IsWithin(float Value, float Standard, float Tolerance)
	{
		return FMath::Abs(Value - Standard) <= Tolerance * Standard;
	}

	/** A unit's mesh height in the world, its hull box's height. */
	float GetHullHeight(const URTSUnitData& Data)
	{
		return RTSUnitShape::FromData(Data).Hull.GetSize().Z;
	}

	/** A building's mesh height: the mesh's bounds times its scale. */
	float GetMeshHeight(const URTSEntityData& Data)
	{
		return Data.Mesh ? Data.Mesh->GetBoundingBox().GetSize().Z * Data.MeshScale.Z : 0.f;
	}

	/** The ground units a building sends out of its exit: what it produces and what it brings along, without aircraft. */
	TArray<URTSUnitData*> GetExitUnits(const URTSBuildingData& Data)
	{
		TArray<URTSUnitData*> Units;
		for (URTSUnitData* Unit : Data.Units)
		{
			if (Unit && !Unit->IsA<URTSAircraftData>())
			{
				Units.AddUnique(Unit);
			}
		}
		if (Data.SpawnOnComplete && !Data.SpawnOnComplete->IsA<URTSAircraftData>())
		{
			Units.AddUnique(Data.SpawnOnComplete);
		}
		return Units;
	}

	/** A building's footprint seen from above, as it stands. */
	FBox2D GetFootprintBox(const ARTSBuilding& Building)
	{
		const FVector2D Center(Building.GetActorLocation());
		const FVector2D Extent(RTSBuildPlacement::GetFootprintExtent(Building.GetFootprint()));
		return FBox2D(Center - Extent, Center + Extent);
	}

	FBox2D GetFootprintBox(const ARTSSupplySource& Source)
	{
		const FVector2D Center(Source.GetActorLocation());
		const FVector2D Extent(RTSBuildPlacement::GetFootprintExtent(Source.Footprint));
		return FBox2D(Center - Extent, Center + Extent);
	}

	UWorld* GetPlayWorld()
	{
		return GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	}

	ARTSUnit* SpawnUnit(UWorld& World, URTSUnitData* Data, int32 TeamId, const FVector& Location)
	{
		FTransform Transform(Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	ARTSBuilding* SpawnBuilding(UWorld& World, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		const FTransform Transform(Location);
		ARTSBuilding* Building = World.SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Building->InitFromData(Data, TeamId);
		Building->FinishSpawning(Transform);
		return Building;
	}

	/** Waits for a play session with its navmesh built and the player seated, then applies the test time dilation. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSScaleWaitForPlay, FAutomationTestBase*, Test, TSharedRef<double>, Start);
	bool FRTSScaleWaitForPlay::Update()
	{
		if (*Start == 0.0)
		{
			*Start = FPlatformTime::Seconds();
		}
		UWorld* World = GetPlayWorld();
		const ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (World && World->HasBegunPlay() && Controller && Controller->GetTeamId() != INDEX_NONE && Controller->GetFaction()
			&& RTSTests::IsNavigationReady(*World))
		{
			RTSTests::ApplyTestTimeDilation(*World);
			return true;
		}
		if (FPlatformTime::Seconds() - *Start > NavTimeout)
		{
			Test->AddError(TEXT("PIE did not start or the navmesh was not built in time"));
			return true;
		}
		return false;
	}

	// ---- Dimensions -------------------------------------------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPlaceholderDimensionsTest, "WorldDivide.Scale.PlaceholderDimensions", TestFlags)

	bool FRTSPlaceholderDimensionsTest::RunTest(const FString& Parameters)
	{
		const FNavDataConfig& Narrowest = RTSNavAgents::GetNarrowestAgent();
		const FNavDataConfig& Widest = RTSNavAgents::GetAgents().Last();
		for (const FUnitStandard& Standard : UnitStandards)
		{
			const URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Standard.Path);
			if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Standard.Path), Data))
			{
				continue;
			}
			const FRTSUnitShape Shape = RTSUnitShape::FromData(*Data);
			const FVector Size = Shape.Hull.GetSize();
			const float Measured = Standard.Axis == EAxis::Length ? Size.X : Standard.Axis == EAxis::Width ? Size.Y : Size.Z;
			AddInfo(FString::Printf(TEXT("%s: %.0f x %.0f x %.0f cm"), *Data->GetName(), Size.X, Size.Y, Size.Z));
			TestTrue(*FString::Printf(TEXT("%s is %.0f cm on its binding axis, the standard %.0f cm within %.0f%%"), *Data->GetName(), Measured,
				Standard.Size, SizeTolerance * 100.f), IsWithin(Measured, Standard.Size, SizeTolerance));
			TestTrue(*FString::Printf(TEXT("%s is scaled uniformly"), *Data->GetName()),
				FMath::IsNearlyEqual(Data->MeshScale.X, Data->MeshScale.Z, 1.e-3f) && FMath::IsNearlyEqual(Data->MeshScale.Y, Data->MeshScale.Z, 1.e-3f));
			if (!Data->IsA<URTSAircraftData>())
			{
				const bool bInfantry = Standard.Axis == EAxis::Height;
				TestEqual(*FString::Printf(TEXT("%s walks the %s navmesh"), *Data->GetName(), bInfantry ? TEXT("infantry") : TEXT("vehicle")),
					RTSNavAgents::SelectAgent(*Data).Name, (bInfantry ? Narrowest : Widest).Name);
			}
		}

		// The M20 keeps its real size: the hull as exported, about 8 m with its gun.
		const URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
		const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
		const URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
		const URTSUnitData* Harvester = LoadObject<URTSUnitData>(nullptr, HarvesterPath);
		if (TestNotNull(TEXT("M20 data"), M20) && TestNotNull(TEXT("Tank data"), Tank) && TestNotNull(TEXT("Infantry data"), Infantry)
			&& TestNotNull(TEXT("Harvester data"), Harvester) && TestNotNull(TEXT("The M20 has its hull mesh"), M20->Mesh.Get()))
		{
			const float M20Hull = M20->Mesh->GetBoundingBox().GetSize().X * M20->MeshScale.X;
			TestTrue(*FString::Printf(TEXT("The M20's hull is %.1f cm, the exported %.1f cm"), M20Hull, M20HullLength),
				IsWithin(M20Hull, M20HullLength, M20HullTolerance));
			TestEqual(TEXT("The M20 is not scaled"), M20->MeshScale, FVector::OneVector);
			const FVector M20Size = RTSUnitShape::FromData(*M20).Hull.GetSize();
			const FVector TankSize = RTSUnitShape::FromData(*Tank).Hull.GetSize();
			const FVector TruckSize = RTSUnitShape::FromData(*Harvester).Hull.GetSize();
			// Light vehicles are smaller than the battle tank.
			TestTrue(*FString::Printf(TEXT("The M20 (%.0f x %.0f cm) is shorter and narrower than the battle tank (%.0f x %.0f cm)"),
				M20Size.X, M20Size.Y, TankSize.X, TankSize.Y), M20Size.X < TankSize.X && M20Size.Y < TankSize.Y);
			TestTrue(TEXT("A truck is narrower than the battle tank"), TruckSize.Y < TankSize.Y);
			// Infantry stands at its real height next to the M20, turret and all: 1.8 m against 3.36 m.
			const float InfantryHeight = GetHullHeight(*Infantry);
			const float Ratio = InfantryHeight / RTSUnitShape::FromData(*M20).Selection.GetSize().Z;
			TestTrue(*FString::Printf(TEXT("Infantry stands %.2f of the M20's height, as 1.8 m does next to 3.36 m"), Ratio),
				IsWithin(Ratio, 180.f / 336.f, SizeTolerance));
			TestTrue(*FString::Printf(TEXT("Infantry (%.0f cm) fits the infantry nav agent's %.0f cm"), RTSUnitShape::FromData(*Infantry).Hull.GetSize().Y,
				2.f * Narrowest.AgentRadius), RTSUnitShape::FromData(*Infantry).Hull.GetSize().Y <= 2.f * Narrowest.AgentRadius);
		}

		for (const FBuildingStandard& Standard : BuildingStandards)
		{
			const URTSBuildingData* Data = LoadObject<URTSBuildingData>(nullptr, Standard.Path);
			if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Standard.Path), Data))
			{
				continue;
			}
			const float Height = GetMeshHeight(*Data);
			AddInfo(FString::Printf(TEXT("%s: %d x %d m, %.0f cm high"), *Data->GetName(), Data->Footprint.X, Data->Footprint.Y, Height));
			TestEqual(*FString::Printf(TEXT("%s has the standard's footprint"), *Data->GetName()), Data->Footprint, Standard.Footprint);
			TestTrue(*FString::Printf(TEXT("%s is %.0f cm high, within the standard's %.0f to %.0f cm"), *Data->GetName(), Height,
				Standard.MinHeight, Standard.MaxHeight),
				Height >= Standard.MinHeight * (1.f - SizeTolerance) && Height <= Standard.MaxHeight * (1.f + SizeTolerance));
			// Its mesh fills its footprint, the 5 % border left, rather than standing small on it.
			const FVector MeshSize = Data->Mesh ? Data->Mesh->GetBoundingBox().GetSize() * Data->MeshScale : FVector::ZeroVector;
			const FVector2D FootprintSize = 2.f * FVector2D(RTSBuildPlacement::GetFootprintExtent(Data->Footprint));
			TestTrue(*FString::Printf(TEXT("%s's mesh fills its footprint"), *Data->GetName()),
				FMath::Max(MeshSize.X / FootprintSize.X, MeshSize.Y / FootprintSize.Y) >= 1.f - SizeTolerance
				&& MeshSize.X <= FootprintSize.X + 1.f && MeshSize.Y <= FootprintSize.Y + 1.f);
		}

		const ARTSSupplySource* Source = GetDefault<ARTSSupplySource>();
		TestEqual(TEXT("A supply source has the standard's footprint"), Source->Footprint, SupplySourceFootprint);
		TestTrue(TEXT("A supply source is 2 to 4 m high"), Source->Height >= SupplySourceMinHeight && Source->Height <= SupplySourceMaxHeight);
		return true;
	}

	// ---- Production exits -------------------------------------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionExitsTest, "WorldDivide.Scale.ProductionExits", TestFlags)

	bool FRTSProductionExitsTest::RunTest(const FString& Parameters)
	{
		const URTSFactionData* Faction = LoadObject<URTSFactionData>(nullptr, FactionPath);
		if (!TestNotNull(TEXT("The test faction"), Faction))
		{
			return false;
		}
		int32 Checked = 0;
		for (URTSBuildingData* Data : Faction->Buildings)
		{
			const TArray<URTSUnitData*> Units = Data ? GetExitUnits(*Data) : TArray<URTSUnitData*>();
			if (Units.IsEmpty())
			{
				continue;
			}
			// A fresh world per building, so nothing else stands at its exit.
			const RTSTests::FRTSTestWorld TestWorld;
			ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, 0, FVector::ZeroVector);
			Building->UpdateConstruction(Data->BuildTime);
			const FBox2D BuildingBox = GetFootprintBox(*Building);
			const FVector2D Extent = BuildingBox.GetExtent();
			const float LaneLength = RTSBuildPlacement::GetExitLaneLength(*Data);
			const FVector Rally = Building->GetProductionComponent()->GetRallyPoint();
			for (URTSUnitData* UnitData : Units)
			{
				const FRTSUnitShape Shape = RTSUnitShape::FromData(*UnitData);
				const FString What = FString::Printf(TEXT("%s out of %s"), *UnitData->GetName(), *Data->GetName());
				// Standard rule 1: the exit side is at least the unit's width plus 2 m on either side.
				TestTrue(*FString::Printf(TEXT("%s: the exit side (%.0f cm) takes the hull's %.0f cm with %.0f cm either side"), *What,
					2.f * Extent.Y, 2.f * Shape.GetHalfWidth(), ExitLaneMargin), 2.f * Extent.Y >= 2.f * (Shape.GetHalfWidth() + ExitLaneMargin));

				ARTSUnit* Unit = Building->SpawnUnitAtExit(UnitData);
				if (!TestNotNull(*FString::Printf(TEXT("%s spawns"), *What), Unit))
				{
					continue;
				}
				++Checked;
				const FRTSFootprint Footprint = Unit->GetFootprint();
				const float Gap = RTSTests::GetHullGap(Footprint, BuildingBox);
				TestTrue(*FString::Printf(TEXT("%s: the hull spawns clear of the building (%.0f cm)"), *What, Gap),
					Gap >= RTSBuildPlacement::ExitClearance - OverlapTolerance);
				// Driving its own length straight out, the hull only gets further away.
				FRTSFootprint Out = Footprint;
				Out.Center += Building->GetActorForwardVector() * 2.f * Shape.GetHalfLength();
				TestTrue(*FString::Printf(TEXT("%s: it drives a hull's length out clear of the building"), *What),
					RTSTests::GetHullGap(Out, BuildingBox) > Gap);
				const float SpawnFront = FVector::Dist2D(Unit->GetActorLocation(), Building->GetActorLocation()) - Extent.X
					+ FMath::Max(Shape.GetHalfLength(), Shape.GetHalfWidth());
				TestTrue(*FString::Printf(TEXT("%s: the exit lane (%.0f cm) reaches past the spawned hull (%.0f cm)"), *What, LaneLength, SpawnFront),
					LaneLength >= SpawnFront - 1.f);
				if (Data->Units.Contains(UnitData))
				{
					const float RallyOut = Rally.X - BuildingBox.Max.X;
					TestTrue(*FString::Printf(TEXT("%s: the rally point (%.0f cm out) lies past the spawned hull (%.0f cm out)"), *What, RallyOut, SpawnFront),
						RallyOut >= SpawnFront);
				}
				Unit->Destroy();
			}
			// The depot's dock side takes a real 10 m truck.
			if (Data->bAcceptsSupply)
			{
				TestTrue(*FString::Printf(TEXT("%s's dock side (%.0f cm) takes a %.0f cm truck"), *Data->GetName(), 2.f * Extent.GetMin(), StandardTruckLength),
					2.f * Extent.GetMin() >= StandardTruckLength);
			}
		}
		TestTrue(TEXT("Every producer was checked"), Checked > 0);
		return true;
	}

	// ---- Docking ----------------------------------------------------------------------------------------------------------

	struct FDockState
	{
		double PhaseStart = 0.0;
		TWeakObjectPtr<ARTSBuilding> Depot;
		TWeakObjectPtr<ARTSSupplySource> Source;
		TWeakObjectPtr<ARTSUnit> Harvester;
		TWeakObjectPtr<ARTSPlayerState> Player;
		int32 LastCredits = 0;
		int32 Deliveries = 0;
		float WorstDepotGap = TNumericLimits<float>::Max();
		float WorstSourceGap = TNumericLimits<float>::Max();
	};

	constexpr int32 DockLoads = 2;
	constexpr double DockTimeout = 90.0;

	/**
	 * A depot and a source on L_TestMap's empty strip north of the bases, a harvester's length and the AI's lane apart, and
	 * the depot's own harvester sent to work them.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSStartDocking, FAutomationTestBase*, Test, TSharedRef<FDockState>, State);
	bool FRTSStartDocking::Update()
	{
		UWorld* World = GetPlayWorld();
		ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		URTSBuildingData* DepotData = LoadObject<URTSBuildingData>(nullptr, SupplyDepotPath);
		const URTSUnitData* HarvesterData = LoadObject<URTSUnitData>(nullptr, HarvesterPath);
		const FBox2D Block = World ? RTSTests::GetLabelledBox(*World, TEXT("Block_North")) : FBox2D(ForceInit);
		if (!Controller || !DepotData || !HarvesterData || !Block.bIsValid)
		{
			Test->AddError(TEXT("No controller, depot or harvester data, or L_TestMap lacks Block_North"));
			return true;
		}
		const float StripY = 0.5f * (Block.Max.Y + RTSTests::GetMapHalfSize(*World));
		const float Apart = RTSBuildPlacement::GetFootprintExtent(DepotData->Footprint).X + RTSBuildPlacement::GetFootprintExtent(GetDefault<ARTSSupplySource>()->Footprint).X
			+ RTSTests::GetHullSize(*HarvesterData).X + ARTSAIPlayer::GetBuildingGap();
		const FVector DepotSpot(0.f, StripY, 0.f);
		const FVector SourceSpot(Apart, StripY, 0.f);
		ARTSSupplySource* Source = World->SpawnActorDeferred<ARTSSupplySource>(ARTSSupplySource::StaticClass(), FTransform(SourceSpot),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Source->Amount = 100 * CastChecked<URTSHarvesterData>(HarvesterData)->CarryAmount;
		Source->FinishSpawning(FTransform(SourceSpot));
		ARTSBuilding* Depot = SpawnBuilding(*World, DepotData, Controller->GetTeamId(), DepotSpot);
		Depot->UpdateConstruction(DepotData->BuildTime);
		State->Depot = Depot;
		State->Source = Source;
		State->Player = Controller->GetPlayerState<ARTSPlayerState>();
		State->LastCredits = State->Player.IsValid() ? State->Player->GetCredits() : 0;
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	/** Follows the depot's harvester for DockLoads deliveries, measuring its hull's gaps to the depot and the source every frame. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyDocking, FAutomationTestBase*, Test, TSharedRef<FDockState>, State);
	bool FRTSVerifyDocking::Update()
	{
		const ARTSBuilding* Depot = State->Depot.Get();
		const ARTSSupplySource* Source = State->Source.Get();
		const ARTSPlayerState* Player = State->Player.Get();
		if (!Depot || !Source || !Player)
		{
			Test->AddError(TEXT("Lost the depot, the source or the player"));
			return true;
		}
		if (!State->Harvester.IsValid())
		{
			for (TActorIterator<ARTSUnit> It(Depot->GetWorld()); It; ++It)
			{
				if (It->GetHarvesterComponent()->CanHarvest() && It->GetTeamId() == Depot->GetTeamId()
					&& FVector::Dist2D(It->GetActorLocation(), Depot->GetActorLocation()) < 2.f * RTSBuildPlacement::GetExitLaneLength(*Depot->GetBuildingData()) + 2000.f)
				{
					State->Harvester = *It;
				}
			}
		}
		if (const ARTSUnit* Harvester = State->Harvester.Get())
		{
			const FRTSFootprint Footprint = Harvester->GetFootprint();
			State->WorstDepotGap = FMath::Min(State->WorstDepotGap, RTSTests::GetHullGap(Footprint, GetFootprintBox(*Depot)));
			State->WorstSourceGap = FMath::Min(State->WorstSourceGap, RTSTests::GetHullGap(Footprint, GetFootprintBox(*Source)));
		}
		if (Player->GetCredits() > State->LastCredits)
		{
			++State->Deliveries;
		}
		State->LastCredits = Player->GetCredits();
		if (State->Deliveries < DockLoads && FPlatformTime::Seconds() - State->PhaseStart < DockTimeout)
		{
			return false;
		}
		Test->TestTrue(TEXT("The depot brought a harvester"), State->Harvester.IsValid());
		Test->TestTrue(FString::Printf(TEXT("The harvester delivered %d loads"), State->Deliveries), State->Deliveries >= DockLoads);
		Test->TestTrue(FString::Printf(TEXT("Its hull never entered the depot (closest %.0f cm)"), State->WorstDepotGap),
			State->WorstDepotGap >= -OverlapTolerance);
		Test->TestTrue(FString::Printf(TEXT("Its hull never entered the source (closest %.0f cm)"), State->WorstSourceGap),
			State->WorstSourceGap >= -OverlapTolerance);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSDockLanesTest, "WorldDivide.Scale.DockLanes", TestFlags)

	bool FRTSDockLanesTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(TestMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
			return false;
		}
		const TSharedRef<FDockState> State = MakeShared<FDockState>();
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSScaleWaitForPlay(this, MakeShared<double>(0.0)));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSStartDocking(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyDocking(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	// ---- Map lanes --------------------------------------------------------------------------------------------------------

	/**
	 * Where a vehicle leaves a base or stops at a source: from Center toward Toward, past Clearance and a hull's length, on
	 * NavMesh.
	 */
	bool FindNavPointOutside(const ARecastNavMesh& NavMesh, const FVector& Center, const FVector& Toward, float Clearance, float HullLength,
		FVector& OutPoint)
	{
		const FVector Direction = (Toward - Center).GetSafeNormal2D();
		for (float Out = Clearance + HullLength; Out < Clearance + 10.f * HullLength; Out += 0.5f * HullLength)
		{
			FNavLocation Projected;
			if (NavMesh.ProjectPoint(Center + Direction * Out, Projected, FVector(50.f, 50.f, 500.f)))
			{
				OutPoint = Projected.Location;
				return true;
			}
		}
		return false;
	}

	/**
	 * Every gap between two of the map's obstacles is closed or at least a vehicle lane (standard rule 4), and a tank's hull,
	 * on the path the hull clearance fits for it, clears every obstacle between every two bases and from each base to every
	 * supply source. Infantry finds the same paths.
	 */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSCheckMapLanes, FAutomationTestBase*, Test, FString, MapPath);
	bool FRTSCheckMapLanes::Update()
	{
		UWorld* World = GetPlayWorld();
		const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
		const URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
		UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!World || !Tank || !Infantry || !NavSystem)
		{
			Test->AddError(TEXT("No play world, navigation or unit data"));
			return true;
		}

		// The obstacles, seen from above: two are touching or a vehicle lane apart.
		TArray<TPair<FString, FBox2D>> Obstacles;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			const FString Label = It->GetActorLabel();
			if (Label.StartsWith(TEXT("Wall_")) || Label.StartsWith(TEXT("Block_")) || Label.StartsWith(TEXT("Water_")))
			{
				FVector Origin;
				FVector Extent;
				It->GetActorBounds(false, Origin, Extent);
				Obstacles.Emplace(Label, FBox2D(FVector2D(Origin - Extent), FVector2D(Origin + Extent)));
			}
		}
		const float Lane = ARTSAIPlayer::GetBuildingGap();
		for (int32 A = 0; A < Obstacles.Num(); ++A)
		{
			for (int32 B = A + 1; B < Obstacles.Num(); ++B)
			{
				const FBox2D& First = Obstacles[A].Value;
				const FBox2D& Second = Obstacles[B].Value;
				const float Gap = FMath::Max(FMath::Max(Second.Min.X - First.Max.X, First.Min.X - Second.Max.X),
					FMath::Max(Second.Min.Y - First.Max.Y, First.Min.Y - Second.Max.Y));
				Test->TestFalse(*FString::Printf(TEXT("%s: %s and %s are touching or a %.0f cm lane apart (%.0f cm)"), *MapPath,
					*Obstacles[A].Key, *Obstacles[B].Key, Lane, Gap), Gap > 1.f && Gap < Lane);
			}
		}

		const FRTSUnitShape Shape = RTSUnitShape::FromData(*Tank);
		const FNavDataConfig& VehicleAgent = RTSNavAgents::SelectAgent(Shape);
		const FNavDataConfig& InfantryAgent = RTSNavAgents::SelectAgent(*Infantry);
		const ARecastNavMesh* VehicleNav = RTSNavAgents::GetNavMesh(*World, VehicleAgent);
		const ARecastNavMesh* InfantryNav = RTSNavAgents::GetNavMesh(*World, InfantryAgent);
		if (!Test->TestNotNull(TEXT("A vehicle navmesh"), VehicleNav) || !Test->TestNotNull(TEXT("An infantry navmesh"), InfantryNav))
		{
			return true;
		}

		// Each base's way out, toward the map's middle, and each source's dock, toward the base it is checked from.
		TArray<FVector> Bases;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Bases.Add(It->GetActorLocation());
		}
		TArray<const ARTSSupplySource*> Sources;
		for (TActorIterator<ARTSSupplySource> It(World); It; ++It)
		{
			Sources.Add(*It);
		}
		const URTSBuildingData* Headquarters = LoadObject<URTSBuildingData>(nullptr, TEXT("/Game/Data/Buildings/DA_TestHeadquarters.DA_TestHeadquarters"));
		const float BaseClearance = Headquarters ? RTSBuildPlacement::GetFootprintExtent(Headquarters->Footprint).Size2D() : 0.f;
		const float HullLength = 2.f * Shape.GetHalfLength();

		TArray<TPair<FVector, FVector>> Routes;
		for (int32 A = 0; A < Bases.Num(); ++A)
		{
			FVector From;
			if (!FindNavPointOutside(*VehicleNav, Bases[A], FVector::ZeroVector, BaseClearance, HullLength, From))
			{
				Test->AddError(FString::Printf(TEXT("%s: no way out of the base at %s"), *MapPath, *Bases[A].ToCompactString()));
				continue;
			}
			for (int32 B = A + 1; B < Bases.Num(); ++B)
			{
				FVector To;
				if (FindNavPointOutside(*VehicleNav, Bases[B], FVector::ZeroVector, BaseClearance, HullLength, To))
				{
					Routes.Emplace(From, To);
				}
			}
			for (const ARTSSupplySource* Source : Sources)
			{
				FVector To;
				const float SourceClearance = RTSBuildPlacement::GetFootprintExtent(Source->Footprint).Size2D();
				if (FindNavPointOutside(*VehicleNav, Source->GetActorLocation(), Bases[A], SourceClearance, HullLength, To))
				{
					Routes.Emplace(From, To);
				}
				else
				{
					Test->AddError(FString::Printf(TEXT("%s: no dock beside the source at %s"), *MapPath, *Source->GetActorLocation().ToCompactString()));
				}
			}
		}

		int32 Samples = 0;
		for (const TPair<FVector, FVector>& Route : Routes)
		{
			const FString What = FString::Printf(TEXT("%s: %s to %s"), *MapPath, *Route.Key.ToCompactString(), *Route.Value.ToCompactString());
			FPathFindingQuery Query(nullptr, *VehicleNav, Route.Key, Route.Value);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = NavSystem->FindPathSync(Query);
			if (!Test->TestTrue(*FString::Printf(TEXT("%s: a tank finds a full path"), *What), Result.IsSuccessful() && !Result.IsPartial() && Result.Path.IsValid()))
			{
				continue;
			}
			FPathFindingQuery InfantryQuery(nullptr, *InfantryNav, Route.Key, Route.Value);
			InfantryQuery.SetAllowPartialPaths(false);
			const FPathFindingResult InfantryResult = NavSystem->FindPathSync(InfantryQuery);
			Test->TestTrue(*FString::Printf(TEXT("%s: infantry finds a full path"), *What), InfantryResult.IsSuccessful() && !InfantryResult.IsPartial());

			// The path as a tank drives it, its hull checked against the obstacles all along.
			TArray<FNavPathPoint> Points = Result.Path->GetPathPoints();
			RTSHullClearance::AdjustPath(*VehicleNav, Shape, Points);
			float Worst = TNumericLimits<float>::Max();
			FVector WorstAt = FVector::ZeroVector;
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				const FVector Start = Points[Index - 1].Location;
				const FVector End = Points[Index].Location;
				const FVector2D Heading = FVector2D(End - Start).GetSafeNormal();
				const float Length = FVector::Dist2D(Start, End);
				for (float Along = 0.f; Along <= Length && !Heading.IsNearlyZero(); Along += LaneSampleStep)
				{
					const FVector At = Start + FVector(Heading, 0.f) * Along;
					const float Clearance = RTSHullClearance::GetClearance(*VehicleNav, RTSHullClearance::MakeFootprint(Shape, At, Heading));
					++Samples;
					if (Clearance < Worst)
					{
						Worst = Clearance;
						WorstAt = At;
					}
				}
			}
			Test->TestTrue(*FString::Printf(TEXT("%s: the tank's hull clears every obstacle (closest %.0f cm at %s)"), *What, Worst,
				*WorstAt.ToCompactString()), Worst >= -OverlapTolerance);
		}
		Test->AddInfo(FString::Printf(TEXT("%s: %d routes, %d hull samples"), *MapPath, Routes.Num(), Samples));
		Test->TestTrue(*FString::Printf(TEXT("%s: routes were driven"), *MapPath), Routes.Num() > 0);
		return true;
	}

	IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSMapVehicleLanesTest, "WorldDivide.Scale.MapVehicleLanes", TestFlags)

	void FRTSMapVehicleLanesTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
	{
		for (const TCHAR* Map : LaneMaps)
		{
			OutBeautifiedNames.Add(FPackageName::GetShortName(Map));
			OutTestCommands.Add(Map);
		}
	}

	bool FRTSMapVehicleLanesTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(Parameters))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), *Parameters));
			return false;
		}
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSScaleWaitForPlay(this, MakeShared<double>(0.0)));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckMapLanes(this, Parameters));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}

	// ---- AI against AI ----------------------------------------------------------------------------------------------------

	constexpr int32 PlayerTeam = 0;
	constexpr int32 AITeam = 1;
	/** Game seconds the two AIs get to finish the match, and the real seconds that takes at the test dilation at most. */
	constexpr float MatchGameSeconds = 1800.f;
	constexpr double MatchTimeout = 900.0;

	struct FAIvsAIState
	{
		double PhaseStart = 0.0;
		float PhaseWorldStart = 0.f;
		TWeakObjectPtr<ARTSAIPlayer> AIs[2];
		int32 StartCredits[2] = { 0, 0 };
		bool bEarned[2] = { false, false };
		bool bBuildOrderDone[2] = { false, false };
		bool bVehicleLeft[2] = { false, false };
		bool bHitEnemy[2] = { false, false };
		float LowestEnemyHealth[2] = { 1.f, 1.f };
		int32 Waves[2] = { 0, 0 };
	};

	/** The player's team handed to a second skirmish AI, so both sides of L_SkirmishDuel play themselves. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSHandPlayerToAI, FAutomationTestBase*, Test, TSharedRef<FAIvsAIState>, State);
	bool FRTSHandPlayerToAI::Update()
	{
		UWorld* World = GetPlayWorld();
		ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		ARTSPlayerState* Player = Controller ? Controller->GetPlayerState<ARTSPlayerState>() : nullptr;
		ARTSAIPlayer* Opponent = nullptr;
		for (TActorIterator<ARTSAIPlayer> It(World); World && It; ++It)
		{
			Opponent = *It;
		}
		const APlayerStart* Start = nullptr;
		const FName Tag(*FString::Printf(TEXT("Team%d"), PlayerTeam));
		for (TActorIterator<APlayerStart> It(World); World && It; ++It)
		{
			Start = It->PlayerStartTag == Tag ? *It : Start;
		}
		if (!Player || !Opponent || !Start || !Test->TestEqual(TEXT("The player has team 0"), Player->GetTeamId(), PlayerTeam))
		{
			Test->AddError(TEXT("No player, AI opponent or PlayerStart of the player's team"));
			return true;
		}

		// The same player state, so the second AI spends the team's credits and orders its units as the player would.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARTSAIPlayer* AI = World->SpawnActor<ARTSAIPlayer>(Start->GetActorLocation(), FRotator::ZeroRotator, Params);
		APlayerState* Own = AI ? AI->PlayerState.Get() : nullptr;
		if (!AI)
		{
			Test->AddError(TEXT("Could not spawn the player's AI"));
			return true;
		}
		AI->SetPlayerState(Player);
		if (Own && Own != Player)
		{
			Own->Destroy();
		}
		AI->SetDifficulty(Opponent->GetDifficulty());
		AI->SetStartLocation(Start->GetActorLocation());
		State->AIs[PlayerTeam] = AI;
		State->AIs[AITeam] = Opponent;
		for (int32 Team = 0; Team < 2; ++Team)
		{
			State->StartCredits[Team] = State->AIs[Team]->GetRTSPlayerState()->GetCredits();
		}
		State->PhaseStart = FPlatformTime::Seconds();
		State->PhaseWorldStart = World->GetTimeSeconds();
		return true;
	}

	/** A checksum of where every unit stands, to the centimetre, and of every building's health, in actor order. */
	uint32 GetEndStateDigest(UWorld& World)
	{
		TArray<int64> Values;
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			Values.Add(FMath::RoundToInt64(It->GetActorLocation().X));
			Values.Add(FMath::RoundToInt64(It->GetActorLocation().Y));
		}
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			Values.Add(FMath::RoundToInt64(It->GetHealthComponent()->GetHealth()));
		}
		return FCrc::MemCrc32(Values.GetData(), Values.Num() * Values.GetTypeSize());
	}

	/** Watches both AIs until the match ends, recording that each ran a working base and fought. */
	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAIvsAI, FAutomationTestBase*, Test, TSharedRef<FAIvsAIState>, State);
	bool FRTSVerifyAIvsAI::Update()
	{
		const ARTSAIPlayer* First = State->AIs[0].Get();
		UWorld* World = First ? First->GetWorld() : nullptr;
		const ARTSGameState* GameState = World ? World->GetGameState<ARTSGameState>() : nullptr;
		if (!World || !GameState)
		{
			Test->AddError(TEXT("Lost the play world"));
			return true;
		}
		for (int32 Team = 0; Team < 2; ++Team)
		{
			const ARTSAIPlayer* AI = State->AIs[Team].Get();
			const ARTSPlayerState* Player = AI ? AI->GetRTSPlayerState() : nullptr;
			const URTSAIBuildOrderData* BuildOrder = AI ? AI->GetBuildOrder() : nullptr;
			if (!Player || !BuildOrder)
			{
				continue;
			}
			State->bEarned[Team] |= Player->GetCredits() > State->StartCredits[Team];
			State->StartCredits[Team] = FMath::Min(State->StartCredits[Team], Player->GetCredits());
			State->Waves[Team] = FMath::Max(State->Waves[Team], AI->GetWaveCount());
			if (!State->bBuildOrderDone[Team])
			{
				bool bDone = true;
				for (const FRTSBuildOrderStep& Step : BuildOrder->Steps)
				{
					bDone &= Step.Entry && AI->CountOwned(*Step.Entry) >= Step.Count;
				}
				State->bBuildOrderDone[Team] = bDone;
			}
			for (TActorIterator<ARTSUnit> It(World); It; ++It)
			{
				// A vehicle of this team away from every building of its own: one came out of a factory and drove off.
				if (It->GetTeamId() != Team || !It->GetUnitData() || It->GetBuilderComponent()->CanBuild()
					|| RTSNavAgents::SelectAgent(*It->GetUnitData()).Name == RTSNavAgents::GetNarrowestAgent().Name
					|| (It->GetQueuedCommandCount() == 0 && It->GetVelocity().IsNearlyZero()))
				{
					continue;
				}
				for (TActorIterator<ARTSBuilding> Building(World); Building; ++Building)
				{
					if (Building->GetTeamId() == Team && !Building->GetBuildingData()->Units.IsEmpty()
						&& FVector::Dist2D(It->GetActorLocation(), Building->GetActorLocation())
							> RTSBuildPlacement::GetFootprintExtent(Building->GetFootprint()).X + RTSBuildPlacement::GetExitLaneLength(*Building->GetBuildingData()))
					{
						State->bVehicleLeft[Team] = true;
					}
				}
			}
			for (TActorIterator<ARTSBuilding> Building(World); Building; ++Building)
			{
				if (Building->GetTeamId() != Team && Building->IsConstructed())
				{
					const URTSHealthComponent* Health = Building->GetHealthComponent();
					const float Share = Health->GetHealth() / FMath::Max(Health->GetMaxHealth(), 1.f);
					State->LowestEnemyHealth[Team] = FMath::Min(State->LowestEnemyHealth[Team], Share);
					State->bHitEnemy[Team] |= Share < 1.f;
				}
			}
		}

		const bool bOver = GameState->IsMatchOver();
		if (!bOver && World->GetTimeSeconds() - State->PhaseWorldStart < MatchGameSeconds && FPlatformTime::Seconds() - State->PhaseStart < MatchTimeout)
		{
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("Match ran %.0f game seconds"), World->GetTimeSeconds() - State->PhaseWorldStart));
		// Under -RTSTestFixedStep every run of a build plays the same match (#179): the same digest each time.
		Test->AddInfo(FString::Printf(TEXT("End state digest %08x"), GetEndStateDigest(*World)));
		for (int32 Team = 0; Team < 2; ++Team)
		{
			Test->AddInfo(FString::Printf(TEXT("Team %d: build order %d, harvested %d, a vehicle left its factory %d, waves %d, lowest an enemy building fell to %.0f%%"),
				Team, State->bBuildOrderDone[Team], State->bEarned[Team], State->bVehicleLeft[Team], State->Waves[Team], 100.f * State->LowestEnemyHealth[Team]));
			Test->TestTrue(*FString::Printf(TEXT("Team %d's AI completed its build order"), Team), State->bBuildOrderDone[Team]);
			Test->TestTrue(*FString::Printf(TEXT("Team %d's harvester delivered"), Team), State->bEarned[Team]);
			Test->TestTrue(*FString::Printf(TEXT("A vehicle of team %d drove out of its factory"), Team), State->bVehicleLeft[Team]);
			Test->TestTrue(*FString::Printf(TEXT("Team %d's AI sent waves"), Team), State->Waves[Team] > 0);
		}
		Test->TestTrue(TEXT("The match ended"), bOver);
		const int32 Winner = GameState->GetWinningTeam();
		if (Test->TestTrue(FString::Printf(TEXT("One AI won (team %d)"), Winner), Winner == PlayerTeam || Winner == AITeam))
		{
			// The loser may fall before its own waves get through.
			Test->TestTrue(TEXT("The winner's army reached and hit the enemy's buildings"), State->bHitEnemy[Winner]);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAIvsAISmokeTest, "WorldDivide.Smoke.AIvsAI", TestFlags)

	bool FRTSAIvsAISmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(DuelMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), DuelMap));
			return false;
		}
		const TSharedRef<FAIvsAIState> State = MakeShared<FAIvsAIState>();
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSScaleWaitForPlay(this, MakeShared<double>(0.0)));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSHandPlayerToAI(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAIvsAI(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

#endif
