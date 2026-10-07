#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSHarvesterData.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSApproach.h"
#include "Units/RTSUnit.h"

/**
 * End to end: open L_TestMap in PIE, wait for the navmesh, order every unit (own and enemy) through the
 * player controller's server RPC, and check that only the player's units walk around the wall to the target.
 * A second test orders a lone tank to attack an enemy out of range and checks it closes in and destroys it.
 * The building tests place buildings through the same RPC: valid and invalid spots, cost and credits, construction
 * over BuildTime, selecting and cancelling a construction site for a refund, and a unit routing around a freshly
 * placed building, and a tank waiting at the edge of a building placed in its path until the navmesh routes it around.
 * Another has three tanks destroy an enemy building
 * and checks that its hole in the navmesh closes again; a variant hides the building behind the map's west wall, so the tanks must
 * approach it from another side. The harvest test buys a supply depot next to a small supply source and follows its
 * harvester through every load until the source is empty. The unreachable-docks test walls the nearest source and depot in
 * with buildings and checks that the harvester uses the open ones instead, then ends its run, loaded, once only the walled
 * depot is left. The unreachable test sends a tank and a harvester at targets beyond
 * the edge of the map and checks that both orders end instead of looping. The obstacle tests check that no navmesh lies inside
 * or on top of the map's walls and blocks, and that tanks sent onto a block stop at its edge on their own side.
 * The building tests place buildings without units, as the debug build menu does, so they turn rts.DebugBuild on for each
 * request. The builder test sends builders instead: one walks to the site and places it, construction holds while it is
 * away and once a second builder dies there, the first finishes it, then builds two buildings queued with Shift. The site
 * clearing test checks that an enemy on the spot refuses a builder, while own units on it step aside, keeping their orders;
 * the builder's ghost turns red over the enemy but stays green over own units, which the debug ghost does not.
 * The production test selects a barracks, sets its rally point and queues three infantry through the controller, as the
 * HUD does: they must come out one build time apart and each walk to the rally point.
 */
namespace
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const FVector MoveTarget(0.f, 0.f, 0.f);
	constexpr int32 UnitsPerTeam = 18;
	constexpr float ArrivalRadius = 800.f;
	constexpr double NavTimeout = 30.0;
	constexpr double MoveTimeout = 60.0;

	/** The empty strip north of both bases, far out of range of the map's own units. */
	const FVector AttackerStart(-1500.f, 7000.f, 0.f);
	const FVector VictimStart(1500.f, 7000.f, 0.f);
	constexpr double AttackTimeout = 30.0;

	const TCHAR* TowerPath = TEXT("/Game/Data/Buildings/DA_TestTower.DA_TestTower");
	const TCHAR* PowerPlantPath = TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");

	/** Spots in the empty north strip, plus a wall and a spot beyond the edge of the ground. */
	const FVector BuildSpot(0.f, 7000.f, 0.f);
	const FVector UnitSpot(3000.f, 7000.f, 0.f);
	const FVector OtherBuildSpot(-3000.f, 7000.f, 0.f);
	const FVector WallSpot(-2500.f, 0.f, 0.f);
	const FVector OffMapSpot(11000.f, 0.f, 0.f);
	constexpr double ConstructionTimeout = 15.0;

	/** Where the building tests give the player a power plant, in the empty strip south of the bases. */
	const FVector PowerPlantSpot(-3000.f, -7000.f, 0.f);

	/** The route test drives a tank straight through BuildSpot after a power plant goes down there. */
	const FVector RouteStart(-1500.f, 7000.f, 0.f);
	const FVector RouteTarget(1500.f, 7000.f, 0.f);
	constexpr float RouteArrivalRadius = 150.f;
	constexpr double RouteTimeout = 30.0;

	/**
	 * The path test places the power plant on BuildSpot once the moving tank's circle is this close to its footprint, well
	 * before the navmesh can cut the hole. A second tank drives a parallel lane south of it, far from the building.
	 */
	constexpr float PathPlacementLead = 40.f;
	const FVector BystanderStart(-1500.f, 6200.f, 0.f);
	const FVector BystanderTarget(1500.f, 6200.f, 0.f);

	/** The building attack puts an enemy power plant on BuildSpot and sends tanks at it from well out of range. */
	const FVector BuildingAttackerStarts[] = {
		FVector(-3500.f, 6700.f, 0.f),
		FVector(-3500.f, 7000.f, 0.f),
		FVector(-3500.f, 7300.f, 0.f),
	};
	constexpr double BuildingAttackTimeout = 45.0;

	/**
	 * The wall test puts an enemy power plant just east of Wall_West, 200 cm thick at WallSpot. Its west edge lies half a
	 * tank's range east of the wall's centre, so the spot facing the tanks to the west lies inside the wall.
	 */
	const TCHAR* WallLabel = TEXT("Wall_West");
	const FVector BehindWallSpot(-1700.f, 0.f, 0.f);
	const FVector WallAttackerStarts[] = {
		FVector(-3500.f, -300.f, 0.f),
		FVector(-3500.f, 0.f, 0.f),
		FVector(-3500.f, 300.f, 0.f),
	};

	/** The unreachable test puts targets beyond the ground's east edge at x = 10000, out of range of any navmesh. */
	const FVector UnreachableBuildingSpot(13000.f, 7000.f, 0.f);
	const FVector UnreachableSourceSpot(13000.f, 5000.f, 0.f);
	const FVector UnreachableAttackerStart(8000.f, 7000.f, 0.f);
	const FVector UnreachableHarvesterStart(8000.f, 5000.f, 0.f);
	constexpr double UnreachableTimeout = 5.0;
	const TCHAR* HarvesterPath = TEXT("/Game/Data/Units/DA_TestHarvester.DA_TestHarvester");

	/** The harvest test puts a supply depot on BuildSpot and a source holding three loads beside it. */
	const TCHAR* SupplyDepotPath = TEXT("/Game/Data/Buildings/DA_TestSupplyDepot.DA_TestSupplyDepot");
	const FVector HarvestSourceSpot(900.f, 7000.f, 0.f);
	constexpr int32 HarvestLoads = 3;
	constexpr double HarvestTimeout = 90.0;
	/** Allowed difference in seconds between two cycles on the same route. */
	constexpr double CycleTolerance = 1.5;

	/**
	 * The unreachable-docks test walls a source and a depot in with a ring of ten of the player's power plants, two cells
	 * wide. Seen from the harvester each lies nearer than its reachable counterpart: the open source south of it, the open
	 * depot north of it.
	 */
	const FVector WalledSourceSpot(-400.f, 7000.f, 0.f);
	const FVector WalledDepotSpot(0.f, 7000.f, 0.f);
	const FVector RingSpots[] = {
		FVector(-800.f, 6600.f, 0.f), FVector(-400.f, 6600.f, 0.f), FVector(0.f, 6600.f, 0.f), FVector(400.f, 6600.f, 0.f),
		FVector(-800.f, 7000.f, 0.f), FVector(400.f, 7000.f, 0.f),
		FVector(-800.f, 7400.f, 0.f), FVector(-400.f, 7400.f, 0.f), FVector(0.f, 7400.f, 0.f), FVector(400.f, 7400.f, 0.f),
	};
	const FVector OpenSourceSpot(1500.f, 5000.f, 0.f);
	const FVector OpenDepotSpot(1500.f, 9000.f, 0.f);
	const FVector DockHarvesterStart(1500.f, 7000.f, 0.f);
	/** How far from a depot's footprint edge a harvester stands when it delivers there, with some slack. */
	constexpr float DeliveryReach = 300.f;
	constexpr double UnreachableDocksTimeout = 60.0;

	/** The obstacle move test sends three tanks from north of Block_North onto its roof, where a click on it lands. */
	const TCHAR* ObstacleLabel = TEXT("Block_North");
	const FVector ObstacleMoverStarts[] = {
		FVector(800.f, 4500.f, 0.f),
		FVector(1000.f, 4500.f, 0.f),
		FVector(1200.f, 4500.f, 0.f),
	};
	/** How far from the block's face a tank may stop: the navmesh ends an agent radius out, plus some slack. */
	constexpr float ObstacleEdgeTolerance = 200.f;
	constexpr double ObstacleMoveTimeout = 30.0;

	/**
	 * The builder test: two builders in the north strip take turns on a tower at BuildSpot, then the first builds a power
	 * plant and a tower queued behind it. A third builder by the west wall is sent off the map and into the wall.
	 */
	const TCHAR* BuilderPath = TEXT("/Game/Data/Units/DA_TestBuilder.DA_TestBuilder");
	const FVector BuilderStart(-2500.f, 7000.f, 0.f);
	const FVector SecondBuilderStart(2500.f, 7000.f, 0.f);
	const FVector BuilderAwaySpot(-2500.f, 6000.f, 0.f);
	const FVector NonBuilderStart(-2000.f, 7600.f, 0.f);
	const FVector ProbeStart(-3300.f, 0.f, 0.f);
	const FVector QueuedTowerSpot(-3000.f, 6000.f, 0.f);
	/** How far from the footprint edge a builder at work stands: its reach of 150 cm beyond its radius, plus some slack. */
	constexpr float BuilderReach = 200.f;
	constexpr double BuilderTimeout = 30.0;
	/** Game seconds a site is watched for progress while no builder stands at it. */
	constexpr double HoldSeconds = 1.5;

	/**
	 * The site clearing test: an enemy parked on OtherBuildSpot refuses the builder, then an idle own tank and an own infantry
	 * attacking a sturdy enemy dummy north of BuildSpot stand on the site of a tower there.
	 */
	const FVector SiteInfantryStart(60.f, 6960.f, 0.f);
	const FVector SiteDummySpot(0.f, 7500.f, 0.f);

	/** The production test: a barracks at BuildSpot sends three infantry to a rally point north-east of it. */
	const TCHAR* BarracksPath = TEXT("/Game/Data/Buildings/DA_TestBarracks.DA_TestBarracks");
	const FVector RallySpot(1200.f, 7600.f, 0.f);
	constexpr int32 ProductionCount = 3;
	/** Units sent to the same rally point crowd around it. */
	constexpr float RallyArrivalRadius = 250.f;
	/** Game seconds a unit may come out later or sooner than one build time after the one before it. */
	constexpr double ProductionTolerance = 0.5;
	constexpr double ProductionTimeout = 60.0;

	struct FSmokeState
	{
		TArray<TWeakObjectPtr<ARTSUnit>> OwnUnits;
		TArray<TWeakObjectPtr<ARTSUnit>> EnemyUnits;
		TArray<FVector> EnemyStartLocations;
		double PhaseStart = 0.0;

		// Attack order test only.
		TWeakObjectPtr<ARTSUnit> Attacker;
		TWeakObjectPtr<ARTSUnit> Victim;

		// Building tests only.
		TWeakObjectPtr<ARTSBuilding> Building;
		double PlacedAtWorldTime = 0.0;
		TWeakObjectPtr<ARTSUnit> Walker;
		bool bEnteredFootprint = false;
		float MaxDetour = 0.f;
		TWeakObjectPtr<ARTSUnit> Bystander;

		// Building attack test only.
		TArray<TWeakObjectPtr<ARTSUnit>> Attackers;
		TArray<FVector> AttackerStarts;
		FVector BuildingCenter = FVector::ZeroVector;

		// Harvest test only.
		TWeakObjectPtr<ARTSPlayerState> Player;
		TWeakObjectPtr<ARTSSupplySource> Source;
		TWeakObjectPtr<ARTSUnit> Harvester;
		int32 CarryAmount = 0;
		int32 CreditsAfterPurchase = 0;
		int32 LastCredits = 0;
		TArray<int32> Deposits;
		TArray<double> DepositTimes;

		// Unreachable-docks test only.
		TWeakObjectPtr<ARTSBuilding> OpenDepot;
		TWeakObjectPtr<ARTSSupplySource> WalledSource;
		int32 SourceAmount = 0;
		float FirstDeliveryDistance = -1.f;

		// Obstacle move test only.
		TArray<TWeakObjectPtr<ARTSUnit>> Movers;
		FBox ObstacleBox = FBox(ForceInit);

		// Builder test only.
		TWeakObjectPtr<ARTSUnit> Builder;
		TWeakObjectPtr<ARTSUnit> SecondBuilder;
		TWeakObjectPtr<ARTSUnit> NonBuilder;
		TWeakObjectPtr<ARTSUnit> Probe;
		int32 BuilderCredits = 0;
		float HeldProgress = 0.f;
		double HoldStartWorldTime = 0.0;
		bool bResumeIssued = false;
		TWeakObjectPtr<ARTSBuilding> QueuedPlant;
		TWeakObjectPtr<ARTSBuilding> QueuedTower;
		bool bTowerBeforePlant = false;

		// Site clearing test only.
		TWeakObjectPtr<ARTSUnit> SiteEnemy;
		TWeakObjectPtr<ARTSUnit> SiteTank;
		TWeakObjectPtr<ARTSUnit> SiteInfantry;
		TWeakObjectPtr<ARTSUnit> SiteDummy;

		// Production test only.
		TArray<TWeakObjectPtr<ARTSUnit>> UnitsBefore;
		TArray<TWeakObjectPtr<ARTSUnit>> Produced;
		TArray<double> ProducedTimes;
	};

	/** One of the map's walls or blocks, found by its label. */
	AStaticMeshActor* FindObstacle(UWorld* World, const TCHAR* Label)
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** True once the navmesh covers Location and is not rebuilding. */
	bool IsOnNavMesh(UWorld& World, const FVector& Location)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		FNavLocation Projected;
		return NavSystem && !NavSystem->IsNavigationBuildInProgress()
			&& NavSystem->ProjectPointToNavigation(Location, Projected, FVector(10.f, 10.f, 250.f));
	}

	int32 CountBuildings(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/** Turns rts.DebugBuild on or off; returns what it was. */
	bool SetDebugBuild(bool bEnabled)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.DebugBuild"));
		const bool bWas = Variable->GetBool();
		Variable->Set(bEnabled, ECVF_SetByConsole);
		return bWas;
	}

	FRTSCommand MakeBuild(URTSBuildingData* Data, const FVector& Location, bool bQueue = false)
	{
		FRTSCommand Command;
		Command.Type = ERTSCommandType::Build;
		Command.EntityData = Data;
		Command.TargetLocation = Location;
		Command.bQueue = bQueue;
		return Command;
	}

	/**
	 * Sends a Build order without units through the player controller's RPC, as the debug build menu does, with debug build
	 * on unless bDebugBuild is false. Returns the new building, or null if the server refused.
	 */
	ARTSBuilding* RequestBuild(ARTSPlayerController& Controller, URTSBuildingData* Data, const FVector& Location, bool bDebugBuild = true)
	{
		UWorld* World = Controller.GetWorld();
		TSet<ARTSBuilding*> Before;
		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			Before.Add(*It);
		}

		// The host's RPC runs right away, so debug build only needs to be on for the call.
		const bool bWasDebugBuild = SetDebugBuild(bDebugBuild);
		Controller.Server_IssueCommand(TArray<ARTSUnit*>(), MakeBuild(Data, Location));
		SetDebugBuild(bWasDebugBuild);

		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			if (!Before.Contains(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The building of Data on the grid spot for Location, or null. */
	ARTSBuilding* FindBuildingAt(UWorld* World, const URTSBuildingData* Data, const FVector& Location)
	{
		const FVector Snapped = RTSBuildPlacement::SnapToGrid(Location, Data->Footprint);
		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			if (It->GetBuildingData() == Data && FVector::Dist2D(It->GetActorLocation(), Snapped) < 1.f)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Unit stands next to Site's footprint, where a builder works. */
	bool IsAtSite(const ARTSUnit& Unit, const ARTSBuilding& Site)
	{
		const FVector Location = Unit.GetActorLocation();
		return FVector::Dist2D(Location, Site.GetClosestFootprintPoint(Location)) <= Unit.GetSelectionRadius() + BuilderReach;
	}

	/** Unit's selection circle stays clear of Site's footprint. */
	bool IsOffFootprint(const ARTSUnit& Unit, const ARTSBuilding& Site)
	{
		const FVector Location = Unit.GetActorLocation();
		return FVector::Dist2D(Location, Site.GetClosestFootprintPoint(Location)) >= Unit.GetSelectionRadius();
	}

	/** What the client's ghost would show for this spot: placed directly, or by a builder of BuilderTeam when given. */
	bool ClientAccepts(UWorld& World, const URTSBuildingData& Data, const FVector& Location, int32 BuilderTeam = INDEX_NONE)
	{
		FVector Snapped;
		return BuilderTeam == INDEX_NONE
			? RTSBuildPlacement::CanPlace(World, Data, Location, Snapped)
			: RTSBuildPlacement::CanBuilderPlace(World, Data, Location, Snapped, BuilderTeam);
	}

	ARTSPlayerController* GetTestController(FAutomationTestBase& Test)
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller)
		{
			Test.AddError(TEXT("No ARTSPlayerController in the PIE world"));
		}
		return Controller;
	}

	/** A completed test power plant for TeamId, so its buildings construct at full speed and its towers fire. */
	ARTSBuilding* SpawnPowerPlant(UWorld* World, int32 TeamId)
	{
		URTSBuildingData* Data = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		if (!Data)
		{
			return nullptr;
		}
		const FTransform Transform(PowerPlantSpot);
		ARTSBuilding* Plant = World->SpawnActorDeferred<ARTSBuilding>(
			ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Plant->InitFromData(Data, TeamId);
		Plant->FinishSpawning(Transform);
		Plant->UpdateConstruction(Data->BuildTime);
		return Plant;
	}

	ARTSUnit* SpawnTestUnit(UWorld* World, const TCHAR* DataPath, int32 TeamId, const FVector& Location)
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, DataPath);
		if (!Data)
		{
			return nullptr;
		}
		FTransform Transform(Location);
		ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	ARTSSupplySource* SpawnSupplySource(UWorld* World, const FVector& Location, int32 Amount)
	{
		const FTransform Transform(Location);
		ARTSSupplySource* Source = World->SpawnActorDeferred<ARTSSupplySource>(
			ARTSSupplySource::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Source->Amount = Amount;
		Source->FinishSpawning(Transform);
		return Source;
	}

	/** A building of TeamId spawned in place, completed when bConstructed. */
	ARTSBuilding* SpawnBuilding(UWorld* World, URTSBuildingData* Data, int32 TeamId, const FVector& Location, bool bConstructed)
	{
		const FTransform Transform(Location);
		ARTSBuilding* Building = World->SpawnActorDeferred<ARTSBuilding>(
			ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Building->InitFromData(Data, TeamId);
		Building->FinishSpawning(Transform);
		if (bConstructed)
		{
			Building->UpdateConstruction(Data->BuildTime);
		}
		return Building;
	}

	/** An infantry of the team after TeamId that cannot fire, so it only stands in the way. */
	ARTSUnit* SpawnUnarmedEnemy(UWorld* World, int32 TeamId, const FVector& Location)
	{
		ARTSUnit* Enemy = SpawnTestUnit(World, InfantryPath, TeamId + 1, Location);
		if (Enemy)
		{
			Enemy->GetWeaponComponent()->SetWeaponData(nullptr);
		}
		return Enemy;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForNavigation, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSWaitForNavigation::Update()
{
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}

	UWorld* World = AutomationCommon::GetAnyGameWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const ARecastNavMesh* NavMesh = NavSystem ? Cast<ARecastNavMesh>(NavSystem->GetDefaultNavDataInstance()) : nullptr;
	const bool bReady = NavMesh && NavMesh->GetNavMeshTilesCount() > 0 && !NavSystem->IsNavigationBuildInProgress();
	if (bReady)
	{
		Test->AddInfo(FString::Printf(TEXT("Navmesh ready with %d tiles"), NavMesh->GetNavMeshTilesCount()));
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("Navmesh was not built in time"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueMoveToAll, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueMoveToAll::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!Controller)
	{
		Test->AddError(TEXT("No ARTSPlayerController in the PIE world"));
		return true;
	}

	TArray<ARTSUnit*> Everyone;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		Everyone.Add(*It);
		if (It->GetTeamId() == Controller->GetTeamId())
		{
			State->OwnUnits.Add(*It);
		}
		else
		{
			State->EnemyUnits.Add(*It);
			State->EnemyStartLocations.Add(It->GetActorLocation());
		}
	}
	Test->TestEqual(TEXT("Own units spawned"), State->OwnUnits.Num(), UnitsPerTeam);
	Test->TestEqual(TEXT("Enemy units spawned"), State->EnemyUnits.Num(), UnitsPerTeam);

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Move;
	Command.TargetLocation = MoveTarget;
	Controller->Server_IssueCommand(Everyone, Command);

	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyArrival, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyArrival::Update()
{
	int32 Arrived = 0;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : State->OwnUnits)
	{
		if (Unit.IsValid() && FVector::Dist2D(Unit->GetActorLocation(), MoveTarget) <= ArrivalRadius)
		{
			++Arrived;
		}
	}

	const bool bAllArrived = Arrived == State->OwnUnits.Num() && Arrived > 0;
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bAllArrived && Elapsed < MoveTimeout)
	{
		return false;
	}

	Test->TestEqual(TEXT("Own units reached the target around the wall"), Arrived, State->OwnUnits.Num());
	Test->AddInfo(FString::Printf(TEXT("%d units arrived after %.1f s"), Arrived, Elapsed));
	for (const TWeakObjectPtr<ARTSUnit>& Unit : State->OwnUnits)
	{
		if (Unit.IsValid() && FVector::Dist2D(Unit->GetActorLocation(), MoveTarget) > ArrivalRadius)
		{
			Test->AddInfo(FString::Printf(TEXT("%s stuck at %s, velocity %s"), *Unit->GetName(),
				*Unit->GetActorLocation().ToCompactString(), *Unit->GetVelocity().ToCompactString()));
		}
	}

	for (int32 Index = 0; Index < State->EnemyUnits.Num(); ++Index)
	{
		const TWeakObjectPtr<ARTSUnit>& Enemy = State->EnemyUnits[Index];
		Test->TestTrue(TEXT("Enemy unit ignored the order"),
			Enemy.IsValid() && FVector::Dist2D(Enemy->GetActorLocation(), State->EnemyStartLocations[Index]) < 50.f);
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueAttack, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueAttack::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!Controller)
	{
		Test->AddError(TEXT("No ARTSPlayerController in the PIE world"));
		return true;
	}

	ARTSUnit* Attacker = SpawnTestUnit(World, TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank"), Controller->GetTeamId(), AttackerStart);
	ARTSUnit* Victim = SpawnTestUnit(World, TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry"), Controller->GetTeamId() + 1, VictimStart);
	if (!Attacker || !Victim)
	{
		Test->AddError(TEXT("Could not spawn the test units"));
		return true;
	}
	Test->TestTrue(TEXT("Attacker is armed"), Attacker->GetWeaponComponent()->IsArmed());
	Test->TestFalse(TEXT("Target starts out of range"), Attacker->GetWeaponComponent()->IsInRange(Victim));
	State->Attacker = Attacker;
	State->Victim = Victim;

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Attack;
	Command.TargetActor = Victim;
	Controller->Server_IssueCommand({ Attacker }, Command);

	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyAttack, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyAttack::Update()
{
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	// The attacker notices the kill on its own next tick, so also wait for its order to clear.
	const bool bOrderPending = State->Attacker.IsValid() && State->Attacker->GetQueuedCommandCount() > 0;
	if ((State->Victim.IsValid() || bOrderPending) && Elapsed < AttackTimeout)
	{
		return false;
	}

	Test->TestFalse(TEXT("Target destroyed"), State->Victim.IsValid());
	Test->AddInfo(FString::Printf(TEXT("Attack finished after %.1f s"), Elapsed));
	if (ARTSUnit* Attacker = State->Attacker.Get())
	{
		Test->TestTrue(TEXT("Attacker closed in on the target"),
			FVector::Dist2D(Attacker->GetActorLocation(), AttackerStart) > 500.f);
		Test->TestEqual(TEXT("Attack order completed"), Attacker->GetQueuedCommandCount(), 0);
	}
	else
	{
		Test->AddError(TEXT("Attacker died"));
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSCheckPlacement, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSCheckPlacement::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();
	URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	const URTSFactionData* Faction = Controller->GetFaction();
	if (!Tower || !Faction)
	{
		Test->AddError(TEXT("Missing the test tower or the player's faction"));
		return true;
	}
	Test->TestTrue(TEXT("The player's faction can build the tower"), Faction->Buildings.Contains(Tower));
	ARTSPlayerState* Player = Controller->GetPlayerState<ARTSPlayerState>();
	const int32 StartCredits = Player->GetCredits();
	Test->TestTrue(FString::Printf(TEXT("The player starts with credits (%d)"), StartCredits), StartCredits >= Tower->Cost);

	// Towers need power: without a plant the tower would build slower and never arm.
	const ARTSBuilding* Plant = SpawnPowerPlant(World, Controller->GetTeamId());
	if (!Plant || !Plant->IsConstructed())
	{
		Test->AddError(TEXT("Could not give the player a power plant"));
		return true;
	}
	Test->TestEqual(TEXT("The plant feeds the player's grid"), Player->GetPowerProduced(), Plant->GetBuildingData()->PowerOutput);

	// Outside debug build only builders place buildings: no menu without one, and the server refuses a Build without units.
	const bool bWasDebugBuild = SetDebugBuild(false);
	Test->TestTrue(TEXT("Nothing is selected"), Controller->GetSelectedUnits().IsEmpty());
	Test->TestTrue(TEXT("No build menu without a builder outside debug build"), Controller->GetBuildMenu().IsEmpty());
	SetDebugBuild(true);
	Test->TestEqual(TEXT("The debug build menu lists the faction's buildings"), Controller->GetBuildMenu().Num(), Faction->Buildings.Num());
	SetDebugBuild(bWasDebugBuild);
	Test->TestNull(TEXT("Server rejects a Build without a builder outside debug build"),
		RequestBuild(*Controller, Tower, BuildSpot, /*bDebugBuild*/ false));
	Test->TestEqual(TEXT("The refused Build cost nothing"), Player->GetCredits(), StartCredits);

	// A valid spot: the client shows green and the server builds.
	Test->TestTrue(TEXT("Client accepts an open spot"), ClientAccepts(*World, *Tower, BuildSpot));
	ARTSBuilding* Building = RequestBuild(*Controller, Tower, BuildSpot + FVector(37.f, -12.f, 150.f));
	if (!Building)
	{
		Test->AddError(TEXT("Server refused an open spot"));
		return true;
	}
	const FVector Expected = RTSBuildPlacement::SnapToGrid(BuildSpot + FVector(37.f, -12.f, 0.f), Tower->Footprint);
	Test->TestTrue(FString::Printf(TEXT("Placed on the grid at %s"), *Building->GetActorLocation().ToCompactString()),
		FVector::Dist2D(Building->GetActorLocation(), Expected) < 1.f);
	Test->TestTrue(TEXT("Placed on the ground, not at the requested height"), FMath::Abs(Building->GetActorLocation().Z) < 50.f);
	Test->TestEqual(TEXT("Placed for the player's team"), Building->GetTeamId(), Controller->GetTeamId());
	Test->TestEqual(TEXT("Placing charged the building's cost"), Player->GetCredits(), StartCredits - Tower->Cost);
	Test->TestFalse(TEXT("Starts under construction"), Building->IsConstructed());
	Test->TestFalse(TEXT("Inert while under construction"), Building->GetWeaponComponent()->IsArmed());
	State->Building = Building;
	State->PlacedAtWorldTime = World->GetTimeSeconds();

	// Invalid spots: the client shows red and the server refuses even when asked directly.
	ARTSUnit* Blocker = SpawnTestUnit(World, InfantryPath, Controller->GetTeamId(), UnitSpot);
	if (!Blocker)
	{
		Test->AddError(TEXT("Could not spawn the blocking unit"));
		return true;
	}
	const TPair<const TCHAR*, FVector> InvalidSpots[] = {
		{ TEXT("on another building"), BuildSpot },
		{ TEXT("on a unit"), UnitSpot },
		{ TEXT("on a wall"), WallSpot },
		{ TEXT("off the navmesh"), OffMapSpot },
	};
	for (const TPair<const TCHAR*, FVector>& Spot : InvalidSpots)
	{
		Test->TestFalse(FString::Printf(TEXT("Client rejects a spot %s"), Spot.Key), ClientAccepts(*World, *Tower, Spot.Value));
		Test->TestNull(FString::Printf(TEXT("Server rejects a spot %s"), Spot.Key), RequestBuild(*Controller, Tower, Spot.Value));
	}

	// The server also refuses what the faction cannot build, even on a good spot.
	URTSBuildingData* Foreign = DuplicateObject<URTSBuildingData>(Tower, GetTransientPackage());
	Test->TestTrue(TEXT("The foreign building's spot is fine"), ClientAccepts(*World, *Foreign, OtherBuildSpot));
	Test->TestNull(TEXT("Server rejects a building outside the faction"), RequestBuild(*Controller, Foreign, OtherBuildSpot));
	Test->TestNull(TEXT("Server rejects a Build without data"), RequestBuild(*Controller, nullptr, OtherBuildSpot));
	Test->TestEqual(TEXT("Rejected requests cost nothing"), Player->GetCredits(), StartCredits - Tower->Cost);

	// One credit short: the client shows red and the server refuses even on a good spot.
	Player->AddCredits(Tower->Cost - 1 - Player->GetCredits());
	Test->TestFalse(TEXT("Client cannot afford the tower one credit short"), Controller->CanAfford(*Tower));
	Test->TestNull(TEXT("Server rejects a building the player cannot afford"), RequestBuild(*Controller, Tower, OtherBuildSpot));
	Test->TestEqual(TEXT("The refused purchase cost nothing"), Player->GetCredits(), Tower->Cost - 1);
	Player->AddCredits(1);
	Test->TestTrue(TEXT("Client can afford the tower with exactly its cost"), Controller->CanAfford(*Tower));

	Test->TestEqual(TEXT("Only the valid request built anything besides the plant"), CountBuildings(World), 2);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyConstruction, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyConstruction::Update()
{
	ARTSBuilding* Building = State->Building.Get();
	if (!Building)
	{
		Test->AddError(TEXT("The placed building disappeared"));
		return true;
	}
	if (!Building->IsConstructed() && FPlatformTime::Seconds() - State->PhaseStart < ConstructionTimeout)
	{
		return false;
	}

	const float BuildTime = Building->GetBuildingData()->BuildTime;
	const double Elapsed = Building->GetWorld()->GetTimeSeconds() - State->PlacedAtWorldTime;
	Test->TestTrue(TEXT("Construction completed"), Building->IsConstructed());
	Test->TestTrue(FString::Printf(TEXT("Took %.2f s of game time, BuildTime is %.2f s"), Elapsed, BuildTime),
		FMath::Abs(Elapsed - BuildTime) <= 0.5);
	Test->TestTrue(TEXT("Armed once complete"), Building->GetWeaponComponent()->IsArmed());
	const ARTSPlayerState* Player = Building->GetWorld()->GetFirstPlayerController()->GetPlayerState<ARTSPlayerState>();
	Test->TestEqual(TEXT("The completed tower draws from the grid"), Player->GetPowerConsumed(), Building->GetBuildingData()->PowerDraw);
	Test->TestFalse(TEXT("The plant covers the tower"), Player->IsLowPower());
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSCheckCancelConstruction, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSCheckCancelConstruction::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSBuilding* Tower = State->Building.Get();
	if (!Controller || !Tower)
	{
		Test->AddError(TEXT("Missing the controller or the completed tower"));
		return true;
	}
	UWorld* World = Controller->GetWorld();
	URTSBuildingData* TowerData = Tower->GetBuildingData();
	ARTSPlayerState* Player = Controller->GetPlayerState<ARTSPlayerState>();

	// A second tower, held at half way so the refund is known.
	const int32 CreditsBefore = Player->GetCredits();
	ARTSBuilding* Site = RequestBuild(*Controller, TowerData, OtherBuildSpot);
	if (!Site)
	{
		Test->AddError(TEXT("Server refused the second tower"));
		return true;
	}
	Site->bAutoConstruct = false;
	Site->UpdateConstruction(TowerData->BuildTime * 0.5f);
	Test->TestEqual(TEXT("The site remembers what it cost"), Site->GetPaidCost(), TowerData->Cost);
	const int32 Refund = Site->GetCancelRefund();
	Test->TestEqual(TEXT("Half built, half the cost comes back"), Refund, TowerData->Cost / 2);

	// Only own buildings are selectable, and only own construction sites can be cancelled.
	const FTransform EnemyTransform(FVector(-6000.f, 7000.f, 0.f));
	ARTSBuilding* Enemy = World->SpawnActorDeferred<ARTSBuilding>(
		ARTSBuilding::StaticClass(), EnemyTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Enemy->InitFromData(TowerData, Controller->GetTeamId() + 1);
	Enemy->FinishSpawning(EnemyTransform);
	Test->TestTrue(TEXT("The own site is selectable"), Controller->SelectBuilding(Site) && Controller->GetSelectedBuilding() == Site);
	Test->TestTrue(TEXT("Selecting a building clears the unit selection"), Controller->GetSelectedUnits().IsEmpty());
	Test->TestFalse(TEXT("An enemy building is not selectable"), Controller->SelectBuilding(Enemy));
	Test->TestTrue(TEXT("The selection is unchanged"), Controller->GetSelectedBuilding() == Site);
	Test->TestFalse(TEXT("The client refuses to cancel an enemy site"), Controller->RequestCancelConstruction(Enemy));
	Test->TestFalse(TEXT("The client refuses to cancel a completed building"), Controller->RequestCancelConstruction(Tower));

	// The server refuses them too when asked directly.
	for (ARTSBuilding* Target : { Enemy, Tower })
	{
		FRTSCommand Command;
		Command.Type = ERTSCommandType::CancelConstruction;
		Command.TargetActor = Target;
		Controller->Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	}
	Test->TestTrue(TEXT("The server kept the enemy site and the completed tower"), IsValid(Enemy) && IsValid(Tower));
	Test->TestEqual(TEXT("Refused cancels pay nothing"), Player->GetCredits(), CreditsBefore - TowerData->Cost);
	Enemy->Destroy();

	Test->TestTrue(TEXT("The selected site can be cancelled"), Controller->RequestCancelConstruction(Controller->GetSelectedBuilding()));
	Test->TestFalse(TEXT("The cancelled site is gone"), IsValid(Site));
	Test->TestNull(TEXT("Nothing stays selected"), Controller->GetSelectedBuilding());
	Test->TestEqual(TEXT("The unbuilt share came back"), Player->GetCredits(), CreditsBefore - TowerData->Cost + Refund);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceObstacle, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSPlaceObstacle::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();

	ARTSUnit* Walker = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), RouteStart);
	ARTSBuilding* Building = RequestBuild(*Controller, LoadObject<URTSBuildingData>(nullptr, PowerPlantPath), BuildSpot);
	if (!Walker || !Building)
	{
		Test->AddError(TEXT("Could not spawn the walker or place the power plant"));
		return true;
	}
	State->Walker = Walker;
	State->Building = Building;
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForNavCut, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSWaitForNavCut::Update()
{
	const ARTSBuilding* Building = State->Building.Get();
	UWorld* World = Building ? Building->GetWorld() : nullptr;
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem)
	{
		Test->AddError(TEXT("No building or navigation system"));
		return true;
	}

	FNavLocation Projected;
	const bool bCut = !NavSystem->ProjectPointToNavigation(Building->GetActorLocation(), Projected, FVector(10.f, 10.f, 250.f))
		&& !NavSystem->IsNavigationBuildInProgress();
	if (bCut)
	{
		Test->AddInfo(FString::Printf(TEXT("Navmesh cut after %.1f s"), FPlatformTime::Seconds() - State->PhaseStart));
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("The building never cut the navmesh"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueRouteMove, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueRouteMove::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller || !State->Walker.IsValid())
	{
		Test->AddError(TEXT("No controller or walker"));
		return true;
	}

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Move;
	Command.TargetLocation = RouteTarget;
	Controller->Server_IssueCommand({ State->Walker.Get() }, Command);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueCrossingMoves, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueCrossingMoves::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();

	ARTSUnit* Walker = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), RouteStart);
	ARTSUnit* Bystander = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), BystanderStart);
	if (!Walker || !Bystander)
	{
		Test->AddError(TEXT("Could not spawn the tanks"));
		return true;
	}
	State->Walker = Walker;
	State->Bystander = Bystander;

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Move;
	Command.TargetLocation = RouteTarget;
	Controller->Server_IssueCommand({ Walker }, Command);
	Command.TargetLocation = BystanderTarget;
	Controller->Server_IssueCommand({ Bystander }, Command);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceOnPath, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSPlaceOnPath::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* Walker = State->Walker.Get();
	ARTSUnit* Bystander = State->Bystander.Get();
	if (!Controller || !Walker || !Bystander)
	{
		Test->AddError(TEXT("No controller or tanks"));
		return true;
	}

	URTSBuildingData* Data = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Data->Footprint);
	const float Gap = BuildSpot.X - Extent.X - Walker->GetSelectionRadius() - Walker->GetActorLocation().X;
	if (Gap > PathPlacementLead)
	{
		if (FPlatformTime::Seconds() - State->PhaseStart > RouteTimeout)
		{
			Test->AddError(FString::Printf(TEXT("The tank never neared the building spot (at %s)"), *Walker->GetActorLocation().ToCompactString()));
			return true;
		}
		return false;
	}

	ARTSBuilding* Building = RequestBuild(*Controller, Data, BuildSpot);
	if (!Building)
	{
		Test->AddError(FString::Printf(TEXT("Could not place the power plant in front of the tank (%.0f cm away)"), Gap));
		return true;
	}
	State->Building = Building;
	Test->TestTrue(TEXT("The navmesh hole is still pending"), Building->IsNavCutPending());
	Test->TestTrue(TEXT("The tank waits at the edge of the new building"), Walker->IsMoveHeld());
	Test->TestFalse(TEXT("The tank on the other lane keeps going"), Bystander->IsMoveHeld());
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSHoldUntilNavCut, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSHoldUntilNavCut::Update()
{
	const ARTSUnit* Walker = State->Walker.Get();
	const ARTSBuilding* Building = State->Building.Get();
	if (!Walker || !Building)
	{
		Test->AddError(TEXT("The tank or the building disappeared"));
		return true;
	}

	// Not even the tank's edge may cross into the footprint before the hole is there.
	if (!IsOffFootprint(*Walker, *Building))
	{
		State->bEnteredFootprint = true;
	}
	if (Building->IsNavCutPending() && FPlatformTime::Seconds() - State->PhaseStart <= NavTimeout)
	{
		return false;
	}

	Test->TestFalse(TEXT("The navmesh hole was built"), Building->IsNavCutPending());
	Test->TestFalse(FString::Printf(TEXT("The tank stayed off the footprint while it waited (at %s)"),
		*Walker->GetActorLocation().ToCompactString()), State->bEnteredFootprint);
	Test->AddInfo(FString::Printf(TEXT("Navmesh cut after %.2f s"), FPlatformTime::Seconds() - State->PhaseStart));
	State->bEnteredFootprint = false;
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyRoute, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyRoute::Update()
{
	const ARTSUnit* Walker = State->Walker.Get();
	const ARTSBuilding* Building = State->Building.Get();
	if (!Walker || !Building)
	{
		Test->AddError(TEXT("The walker or the building disappeared"));
		return true;
	}

	const FVector Location = Walker->GetActorLocation();
	const FVector Center = Building->GetActorLocation();
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Building->GetBuildingData()->Footprint);
	if (FMath::Abs(Location.X - Center.X) < Extent.X && FMath::Abs(Location.Y - Center.Y) < Extent.Y)
	{
		State->bEnteredFootprint = true;
	}
	State->MaxDetour = FMath::Max(State->MaxDetour, FMath::Abs(Location.Y - RouteStart.Y));

	const bool bArrived = FVector::Dist2D(Location, RouteTarget) <= RouteArrivalRadius;
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bArrived && Elapsed < RouteTimeout)
	{
		return false;
	}

	Test->TestTrue(FString::Printf(TEXT("Walker arrived (at %s after %.1f s)"), *Location.ToCompactString(), Elapsed), bArrived);
	Test->TestFalse(TEXT("Walker never entered the building's footprint"), State->bEnteredFootprint);
	Test->TestTrue(FString::Printf(TEXT("Walker went around the building (%.0f cm detour)"), State->MaxDetour),
		State->MaxDetour >= Extent.Y);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceEnemyBuilding, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSPlaceEnemyBuilding::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();

	// The enemy has no controller in PIE, so the server places its building directly.
	FRTSCommand Build;
	Build.Type = ERTSCommandType::Build;
	Build.EntityData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	Build.TargetLocation = BuildSpot;
	ARTSBuilding* Building = RTSCommands::PlaceBuilding(*World, Controller->GetFaction(), Controller->GetTeamId() + 1, Build);
	if (!Building)
	{
		Test->AddError(TEXT("Could not place the enemy power plant"));
		return true;
	}
	State->Building = Building;
	State->BuildingCenter = Building->GetActorLocation();

	for (const FVector& Start : BuildingAttackerStarts)
	{
		ARTSUnit* Attacker = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), Start);
		if (!Attacker)
		{
			Test->AddError(TEXT("Could not spawn an attacking tank"));
			return true;
		}
		Test->TestFalse(TEXT("The building starts out of the tank's range"), Attacker->GetWeaponComponent()->IsInRange(Building));
		State->Attackers.Add(Attacker);
		State->AttackerStarts.Add(Start);
	}
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceBuildingBehindWall, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSPlaceBuildingBehindWall::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();

	const AStaticMeshActor* Wall = FindObstacle(World, WallLabel);
	if (!Wall)
	{
		Test->AddError(FString::Printf(TEXT("No %s in the map"), WallLabel));
		return true;
	}
	State->ObstacleBox = Wall->GetComponentsBoundingBox();

	FRTSCommand Build;
	Build.Type = ERTSCommandType::Build;
	Build.EntityData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	Build.TargetLocation = BehindWallSpot;
	ARTSBuilding* Building = RTSCommands::PlaceBuilding(*World, Controller->GetFaction(), Controller->GetTeamId() + 1, Build);
	if (!Building)
	{
		Test->AddError(TEXT("Could not place the enemy power plant behind the wall"));
		return true;
	}
	State->Building = Building;

	for (const FVector& Start : WallAttackerStarts)
	{
		ARTSUnit* Attacker = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), Start);
		if (!Attacker)
		{
			Test->AddError(TEXT("Could not spawn an attacking tank"));
			return true;
		}
		Test->TestFalse(TEXT("The building starts out of the tank's range"), Attacker->GetWeaponComponent()->IsInRange(Building));
		State->Attackers.Add(Attacker);
		State->AttackerStarts.Add(Start);
	}
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSCheckWallApproach, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSCheckWallApproach::Update()
{
	const ARTSBuilding* Building = State->Building.Get();
	if (!Building)
	{
		Test->AddError(TEXT("The building behind the wall disappeared"));
		return true;
	}
	UWorld* World = Building->GetWorld();
	const FIntPoint Footprint = Building->GetBuildingData()->Footprint;

	for (const TWeakObjectPtr<ARTSUnit>& Attacker : State->Attackers)
	{
		if (!Attacker.IsValid())
		{
			continue;
		}
		// The same stand-off and reach the Attack order uses.
		const float Range = Attacker->GetWeaponComponent()->GetRange();
		const FVector Edge = Building->GetClosestFootprintPoint(Attacker->GetActorLocation());
		const FVector Facing = Edge + (Attacker->GetActorLocation() - Edge).GetSafeNormal2D() * Range * 0.5f;
		Test->TestFalse(FString::Printf(TEXT("The spot facing the tank (%s) lies in the wall"), *Facing.ToCompactString()),
			IsOnNavMesh(*World, Facing));

		FVector Spot;
		const bool bFound = RTSApproach::FindReachableSpot(*Attacker, Building->GetActorLocation(), Footprint, Range * 0.5f, Range, Spot);
		Test->TestTrue(FString::Printf(TEXT("A reachable spot on another side (%s)"), *Spot.ToCompactString()),
			bFound && FVector::Dist2D(Spot, Facing) > 200.f);
		// Only around the wall's end, on the building's side of it.
		Test->TestTrue(FString::Printf(TEXT("The spot (%s) lies past the wall"), *Spot.ToCompactString()),
			bFound && Spot.X > State->ObstacleBox.Max.X);
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceUnreachableTargets, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSPlaceUnreachableTargets::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();

	// Placement would refuse a spot off the navmesh, so the server spawns these directly.
	URTSBuildingData* PlantData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	const FTransform BuildingTransform(UnreachableBuildingSpot);
	ARTSBuilding* Building = World->SpawnActorDeferred<ARTSBuilding>(
		ARTSBuilding::StaticClass(), BuildingTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Building->InitFromData(PlantData, Controller->GetTeamId() + 1);
	Building->FinishSpawning(BuildingTransform);

	const FTransform SourceTransform(UnreachableSourceSpot);
	ARTSSupplySource* Source = World->SpawnActorDeferred<ARTSSupplySource>(
		ARTSSupplySource::StaticClass(), SourceTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Source->Amount = 1000;
	Source->FinishSpawning(SourceTransform);

	ARTSUnit* Attacker = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), UnreachableAttackerStart);
	ARTSUnit* Harvester = SpawnTestUnit(World, HarvesterPath, Controller->GetTeamId(), UnreachableHarvesterStart);
	if (!Attacker || !Harvester || !Harvester->GetHarvesterComponent()->CanHarvest())
	{
		Test->AddError(TEXT("Could not spawn the tank and the harvester"));
		return true;
	}
	State->Building = Building;
	State->Source = Source;
	State->Attacker = Attacker;
	State->Harvester = Harvester;

	FRTSCommand Attack;
	Attack.Type = ERTSCommandType::Attack;
	Attack.TargetActor = Building;
	Controller->Server_IssueCommand({ Attacker }, Attack);
	Test->TestEqual(TEXT("The tank took the Attack order"), Attacker->GetQueuedCommandCount(), 1);

	FRTSCommand Harvest;
	Harvest.Type = ERTSCommandType::Harvest;
	Harvest.TargetActor = Source;
	Controller->Server_IssueCommand({ Harvester }, Harvest);
	Test->TestEqual(TEXT("The harvester took the Harvest order"), Harvester->GetQueuedCommandCount(), 1);

	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyUnreachableTargets, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyUnreachableTargets::Update()
{
	const ARTSUnit* Attacker = State->Attacker.Get();
	const ARTSUnit* Harvester = State->Harvester.Get();
	if (!Attacker || !Harvester)
	{
		Test->AddError(TEXT("Lost the tank or the harvester"));
		return true;
	}
	const bool bOrdersPending = Attacker->GetQueuedCommandCount() > 0 || Harvester->GetQueuedCommandCount() > 0;
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (bOrdersPending && Elapsed < UnreachableTimeout)
	{
		return false;
	}

	Test->AddInfo(FString::Printf(TEXT("Orders ended after %.1f s"), Elapsed));
	Test->TestEqual(TEXT("The Attack order on an unreachable building ended"), Attacker->GetQueuedCommandCount(), 0);
	Test->TestEqual(TEXT("The Harvest order on an unreachable source ended"), Harvester->GetQueuedCommandCount(), 0);
	Test->TestEqual(TEXT("The harvester is idle"), Harvester->GetHarvesterComponent()->GetState(), ERTSHarvestState::Idle);
	Test->TestTrue(TEXT("The building is untouched"), State->Building.IsValid()
		&& State->Building->GetHealthComponent()->GetHealth() == State->Building->GetHealthComponent()->GetMaxHealth());
	Test->TestTrue(FString::Printf(TEXT("The tank stayed put (%s)"), *Attacker->GetActorLocation().ToCompactString()),
		FVector::Dist2D(Attacker->GetActorLocation(), UnreachableAttackerStart) < 100.f);
	Test->TestTrue(FString::Printf(TEXT("The harvester stayed put (%s)"), *Harvester->GetActorLocation().ToCompactString()),
		FVector::Dist2D(Harvester->GetActorLocation(), UnreachableHarvesterStart) < 100.f);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueBuildingAttack, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueBuildingAttack::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller || !State->Building.IsValid())
	{
		Test->AddError(TEXT("No controller or enemy building"));
		return true;
	}

	TArray<ARTSUnit*> Attackers;
	for (const TWeakObjectPtr<ARTSUnit>& Attacker : State->Attackers)
	{
		if (Attacker.IsValid())
		{
			Attackers.Add(Attacker.Get());
		}
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::Attack;
	Command.TargetActor = State->Building.Get();
	Controller->Server_IssueCommand(Attackers, Command);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyBuildingAttack, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyBuildingAttack::Update()
{
	bool bOrderPending = false;
	for (const TWeakObjectPtr<ARTSUnit>& Attacker : State->Attackers)
	{
		bOrderPending |= Attacker.IsValid() && Attacker->GetQueuedCommandCount() > 0;
	}
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if ((State->Building.IsValid() || bOrderPending) && Elapsed < BuildingAttackTimeout)
	{
		return false;
	}

	Test->TestFalse(TEXT("Enemy building destroyed"), State->Building.IsValid());
	Test->AddInfo(FString::Printf(TEXT("Building attack finished after %.1f s"), Elapsed));
	for (int32 Index = 0; Index < State->Attackers.Num(); ++Index)
	{
		const ARTSUnit* Attacker = State->Attackers[Index].Get();
		if (!Attacker)
		{
			Test->AddError(TEXT("An attacker died"));
			continue;
		}
		Test->TestTrue(TEXT("Attacker closed in on the building"),
			FVector::Dist2D(Attacker->GetActorLocation(), State->AttackerStarts[Index]) > 500.f);
		Test->TestEqual(TEXT("Attack order completed"), Attacker->GetQueuedCommandCount(), 0);
	}
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForNavRestored, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSWaitForNavRestored::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	if (!World)
	{
		Test->AddError(TEXT("No PIE world"));
		return true;
	}
	if (IsOnNavMesh(*World, State->BuildingCenter))
	{
		Test->AddInfo(FString::Printf(TEXT("Navmesh hole closed after %.1f s"), FPlatformTime::Seconds() - State->PhaseStart));
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("The destroyed building's navmesh hole never closed"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSBuySupplyDepot, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSBuySupplyDepot::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();
	ARTSPlayerState* Player = Controller->GetPlayerState<ARTSPlayerState>();
	URTSBuildingData* Depot = LoadObject<URTSBuildingData>(nullptr, SupplyDepotPath);
	const URTSHarvesterData* HarvesterData = Depot ? Cast<URTSHarvesterData>(Depot->SpawnOnComplete) : nullptr;
	if (!Player || !HarvesterData || !Depot->bAcceptsSupply)
	{
		Test->AddError(TEXT("Missing the player state, or the test supply depot does not take supplies and bring a harvester"));
		return true;
	}
	State->Player = Player;
	State->CarryAmount = HarvesterData->CarryAmount;
	if (!SpawnPowerPlant(World, Controller->GetTeamId()))
	{
		Test->AddError(TEXT("Could not give the player a power plant"));
		return true;
	}

	const FTransform SourceTransform(HarvestSourceSpot);
	ARTSSupplySource* Source = World->SpawnActorDeferred<ARTSSupplySource>(
		ARTSSupplySource::StaticClass(), SourceTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Source->Amount = HarvestLoads * State->CarryAmount;
	Source->FinishSpawning(SourceTransform);
	State->Source = Source;

	const int32 CreditsBefore = Player->GetCredits();
	ARTSBuilding* Building = RequestBuild(*Controller, Depot, BuildSpot);
	if (!Building)
	{
		Test->AddError(TEXT("Server refused the supply depot"));
		return true;
	}
	Test->TestEqual(TEXT("The depot cost its price"), Player->GetCredits(), CreditsBefore - Depot->Cost);
	State->Building = Building;
	State->CreditsAfterPurchase = Player->GetCredits();
	State->LastCredits = State->CreditsAfterPurchase;
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForHarvester, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSWaitForHarvester::Update()
{
	ARTSBuilding* Depot = State->Building.Get();
	if (!Depot)
	{
		Test->AddError(TEXT("The supply depot disappeared"));
		return true;
	}

	TArray<ARTSUnit*> Harvesters;
	for (TActorIterator<ARTSUnit> It(Depot->GetWorld()); It; ++It)
	{
		if (It->GetHarvesterComponent()->CanHarvest())
		{
			Harvesters.Add(*It);
		}
	}
	if (Harvesters.IsEmpty() && FPlatformTime::Seconds() - State->PhaseStart < ConstructionTimeout)
	{
		return false;
	}

	Test->TestTrue(TEXT("The depot completed"), Depot->IsConstructed());
	if (!Test->TestEqual(TEXT("The completed depot brought one harvester"), Harvesters.Num(), 1))
	{
		return true;
	}
	Test->TestEqual(TEXT("The harvester is the player's"), Harvesters[0]->GetTeamId(), Depot->GetTeamId());
	Test->TestEqual(TEXT("It set off on a Harvest order"), Harvesters[0]->GetQueuedCommandCount(), 1);
	Test->TestTrue(TEXT("It picked the source next to the depot"),
		Harvesters[0]->GetHarvesterComponent()->GetSource() == State->Source.Get());
	State->Harvester = Harvesters[0];
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyHarvest, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyHarvest::Update()
{
	const ARTSPlayerState* Player = State->Player.Get();
	const ARTSUnit* Harvester = State->Harvester.Get();
	if (!Player || !Harvester)
	{
		Test->AddError(TEXT("Lost the player state or the harvester"));
		return true;
	}

	if (Player->GetCredits() != State->LastCredits)
	{
		State->Deposits.Add(Player->GetCredits() - State->LastCredits);
		State->DepositTimes.Add(Harvester->GetWorld()->GetTimeSeconds());
		State->LastCredits = Player->GetCredits();
	}
	const bool bDone = State->Deposits.Num() >= HarvestLoads && Harvester->GetQueuedCommandCount() == 0;
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bDone && Elapsed < HarvestTimeout)
	{
		return false;
	}

	Test->AddInfo(FString::Printf(TEXT("Harvest finished after %.1f s"), Elapsed));
	if (!Test->TestEqual(TEXT("One delivery per load"), State->Deposits.Num(), HarvestLoads))
	{
		return true;
	}
	for (int32 Index = 0; Index < HarvestLoads; ++Index)
	{
		Test->TestEqual(FString::Printf(TEXT("Delivery %d is a full load"), Index + 1), State->Deposits[Index], State->CarryAmount);
	}
	Test->TestEqual(TEXT("Credits rose by everything the source held"),
		Player->GetCredits(), State->CreditsAfterPurchase + HarvestLoads * State->CarryAmount);

	// The first trip starts at the depot's door; the later ones drive the same route and take the same time.
	const double FirstCycle = State->DepositTimes[1] - State->DepositTimes[0];
	const double SecondCycle = State->DepositTimes[2] - State->DepositTimes[1];
	Test->TestTrue(FString::Printf(TEXT("Steady cycles (%.2f s, %.2f s)"), FirstCycle, SecondCycle),
		FMath::Abs(FirstCycle - SecondCycle) <= CycleTolerance);

	Test->TestTrue(TEXT("The source is empty"), State->Source.IsValid() && State->Source->IsDepleted());
	Test->TestEqual(TEXT("With no other source in reach the harvester went idle"),
		Harvester->GetHarvesterComponent()->GetState(), ERTSHarvestState::Idle);
	Test->TestEqual(TEXT("Nothing left on board"), Harvester->GetHarvesterComponent()->GetCarried(), 0);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSPlaceUnreachableDocks, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSPlaceUnreachableDocks::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();
	const int32 TeamId = Controller->GetTeamId();
	URTSBuildingData* PlantData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	URTSBuildingData* TestDepot = LoadObject<URTSBuildingData>(nullptr, SupplyDepotPath);
	if (!PlantData || !TestDepot || !SpawnPowerPlant(World, TeamId))
	{
		Test->AddError(TEXT("Missing the test power plant or supply depot"));
		return true;
	}
	// Depots without a harvester of their own, so only the test's harvester runs.
	URTSBuildingData* DepotData = DuplicateObject<URTSBuildingData>(TestDepot, GetTransientPackage());
	DepotData->SpawnOnComplete = nullptr;

	for (const FVector& Spot : RingSpots)
	{
		SpawnBuilding(World, PlantData, TeamId, Spot, false);
	}
	ARTSBuilding* WalledDepot = SpawnBuilding(World, DepotData, TeamId, WalledDepotSpot, true);
	ARTSBuilding* OpenDepot = SpawnBuilding(World, DepotData, TeamId, OpenDepotSpot, true);
	ARTSUnit* Harvester = SpawnTestUnit(World, HarvesterPath, TeamId, DockHarvesterStart);
	const URTSHarvesterData* HarvesterData = Harvester ? Cast<URTSHarvesterData>(Harvester->GetUnitData()) : nullptr;
	if (!HarvesterData)
	{
		Test->AddError(TEXT("Could not spawn the harvester"));
		return true;
	}
	State->CarryAmount = HarvesterData->CarryAmount;
	State->SourceAmount = 3 * State->CarryAmount;
	ARTSSupplySource* WalledSource = SpawnSupplySource(World, WalledSourceSpot, State->SourceAmount);
	ARTSSupplySource* OpenSource = SpawnSupplySource(World, OpenSourceSpot, State->SourceAmount);

	Test->TestTrue(TEXT("Both depots are complete"), WalledDepot->IsConstructed() && OpenDepot->IsConstructed());
	Test->TestTrue(TEXT("The walled source is the nearest in reach"),
		ARTSSupplySource::FindNearest(*World, DockHarvesterStart, HarvesterData->SearchRadius) == WalledSource);
	Test->TestTrue(TEXT("The walled depot is the nearest"),
		URTSHarvesterComponent::FindNearestDepot(*World, TeamId, DockHarvesterStart) == WalledDepot);

	State->Player = Controller->GetPlayerState<ARTSPlayerState>();
	State->Building = WalledDepot;
	State->OpenDepot = OpenDepot;
	State->Source = OpenSource;
	State->WalledSource = WalledSource;
	State->Harvester = Harvester;
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueUnreachableDocksHarvest, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueUnreachableDocksHarvest::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* Harvester = State->Harvester.Get();
	if (!Controller || !Harvester || !State->Building.IsValid() || !State->OpenDepot.IsValid()
		|| !State->Source.IsValid() || !State->WalledSource.IsValid() || !State->Player.IsValid())
	{
		Test->AddError(TEXT("Lost the controller, the harvester, a depot, a source or the player state"));
		return true;
	}

	// The ring must really cut the walled ones off, with the stand-off and reach the harvester uses.
	const float Radius = Harvester->GetSelectionRadius();
	const auto IsReachable = [Harvester, Radius](const AActor& Dock, const FIntPoint& Footprint)
	{
		FVector Spot;
		return RTSApproach::FindReachableSpot(*Harvester, Dock.GetActorLocation(), Footprint, Radius + 60.f, Radius + 125.f, Spot);
	};
	Test->TestFalse(TEXT("The walled source cannot be reached"), IsReachable(*State->WalledSource, State->WalledSource->Footprint));
	Test->TestFalse(TEXT("The walled depot cannot be reached"), IsReachable(*State->Building, State->Building->GetBuildingData()->Footprint));
	Test->TestTrue(TEXT("The open source can be reached"), IsReachable(*State->Source, State->Source->Footprint));
	Test->TestTrue(TEXT("The open depot can be reached"), IsReachable(*State->OpenDepot, State->OpenDepot->GetBuildingData()->Footprint));

	// No source named: the harvester picks its own.
	FRTSCommand Harvest;
	Harvest.Type = ERTSCommandType::Harvest;
	Controller->Server_IssueCommand({ Harvester }, Harvest);
	Test->TestEqual(TEXT("The harvester took the Harvest order"), Harvester->GetQueuedCommandCount(), 1);

	State->CreditsAfterPurchase = State->Player->GetCredits();
	State->LastCredits = State->CreditsAfterPurchase;
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyUnreachableDocks, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyUnreachableDocks::Update()
{
	const ARTSPlayerState* Player = State->Player.Get();
	const ARTSUnit* Harvester = State->Harvester.Get();
	if (!Player || !Harvester)
	{
		Test->AddError(TEXT("Lost the player state or the harvester"));
		return true;
	}

	if (Player->GetCredits() != State->LastCredits)
	{
		State->Deposits.Add(Player->GetCredits() - State->LastCredits);
		State->LastCredits = Player->GetCredits();
		// The first load went to the open depot; take it away so the next load has only the walled one left.
		if (ARTSBuilding* OpenDepot = State->OpenDepot.Get(); OpenDepot && State->Deposits.Num() == 1)
		{
			const FVector Location = Harvester->GetActorLocation();
			State->FirstDeliveryDistance = FVector::Dist2D(Location, OpenDepot->GetClosestFootprintPoint(Location));
			OpenDepot->Destroy();
		}
	}
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (Harvester->GetQueuedCommandCount() > 0 && Elapsed < UnreachableDocksTimeout)
	{
		return false;
	}

	Test->AddInfo(FString::Printf(TEXT("Harvest order ended after %.1f s"), Elapsed));
	if (!Test->TestEqual(TEXT("One delivery, before the open depot went"), State->Deposits.Num(), 1))
	{
		return true;
	}
	Test->TestEqual(TEXT("The delivery is a full load"), State->Deposits[0], State->CarryAmount);
	Test->TestTrue(FString::Printf(TEXT("Delivered at the open depot (%.0f cm from its edge)"), State->FirstDeliveryDistance),
		State->FirstDeliveryDistance >= 0.f && State->FirstDeliveryDistance <= DeliveryReach);

	const URTSHarvesterComponent* Component = Harvester->GetHarvesterComponent();
	Test->TestEqual(TEXT("With only the walled depot left the Harvest order ended"), Harvester->GetQueuedCommandCount(), 0);
	Test->TestEqual(TEXT("The harvester is idle"), Component->GetState(), ERTSHarvestState::Idle);
	Test->TestEqual(TEXT("The second load stays on board"), Component->GetCarried(), State->CarryAmount);
	Test->TestEqual(TEXT("Credits rose by the one delivery"), Player->GetCredits(), State->CreditsAfterPurchase + State->CarryAmount);
	Test->TestTrue(TEXT("Both loads came from the open source"),
		State->Source.IsValid() && State->Source->GetRemaining() == State->SourceAmount - 2 * State->CarryAmount);
	Test->TestTrue(TEXT("The walled source is untouched"),
		State->WalledSource.IsValid() && State->WalledSource->GetRemaining() == State->SourceAmount);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRTSCheckSolidObstacles, FAutomationTestBase*, Test);
bool FRTSCheckSolidObstacles::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem)
	{
		Test->AddError(TEXT("No PIE world or navigation system"));
		return true;
	}

	int32 Obstacles = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const FString Label = It->GetActorLabel();
		if (!Label.StartsWith(TEXT("Wall_")) && !Label.StartsWith(TEXT("Block_")))
		{
			continue;
		}
		++Obstacles;

		FVector Center;
		FVector Extent;
		It->GetActorBounds(false, Center, Extent);
		FNavLocation Projected;
		// Tall enough to reach the ground inside the block and its roof.
		Test->TestFalse(FString::Printf(TEXT("No navmesh inside or on %s"), *Label),
			NavSystem->ProjectPointToNavigation(Center, Projected, FVector(10.f, 10.f, Extent.Z + 100.f)));

		const FVector Beside(Center.X - Extent.X - 300.f, Center.Y, 0.f);
		Test->TestTrue(FString::Printf(TEXT("Navmesh on the ground beside %s"), *Label),
			NavSystem->ProjectPointToNavigation(Beside, Projected, FVector(10.f, 10.f, 250.f)));
	}
	Test->TestEqual(TEXT("Walls and blocks found"), Obstacles, 4);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueObstacleMove, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueObstacleMove::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();

	const AStaticMeshActor* Obstacle = FindObstacle(World, ObstacleLabel);
	if (!Obstacle)
	{
		Test->AddError(FString::Printf(TEXT("No %s in the map"), ObstacleLabel));
		return true;
	}
	State->ObstacleBox = Obstacle->GetComponentsBoundingBox();

	TArray<ARTSUnit*> Movers;
	for (const FVector& Start : ObstacleMoverStarts)
	{
		ARTSUnit* Mover = SpawnTestUnit(World, TankPath, Controller->GetTeamId(), Start);
		if (!Mover)
		{
			Test->AddError(TEXT("Could not spawn a tank"));
			return true;
		}
		Movers.Add(Mover);
		State->Movers.Add(Mover);
	}

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Move;
	Command.TargetLocation = FVector(State->ObstacleBox.GetCenter().X, State->ObstacleBox.GetCenter().Y, State->ObstacleBox.Max.Z);
	Controller->Server_IssueCommand(Movers, Command);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyObstacleMove, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyObstacleMove::Update()
{
	const FBox& Box = State->ObstacleBox;
	const auto DistanceToFace = [&Box](const FVector& Location)
	{
		return FVector::Dist2D(Location, FVector(
			FMath::Clamp(Location.X, Box.Min.X, Box.Max.X), FMath::Clamp(Location.Y, Box.Min.Y, Box.Max.Y), Location.Z));
	};

	bool bAllAtEdge = true;
	for (const TWeakObjectPtr<ARTSUnit>& Mover : State->Movers)
	{
		bAllAtEdge &= Mover.IsValid() && Mover->GetQueuedCommandCount() == 0
			&& DistanceToFace(Mover->GetActorLocation()) <= ObstacleEdgeTolerance;
	}
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bAllAtEdge && Elapsed < ObstacleMoveTimeout)
	{
		return false;
	}

	for (const TWeakObjectPtr<ARTSUnit>& Mover : State->Movers)
	{
		if (!Mover.IsValid())
		{
			Test->AddError(TEXT("A tank disappeared"));
			continue;
		}
		const FVector Location = Mover->GetActorLocation();
		const float Distance = DistanceToFace(Location);
		Test->AddInfo(FString::Printf(TEXT("%s stopped at %s, %.0f cm from the block"), *Mover->GetName(), *Location.ToCompactString(), Distance));
		Test->TestTrue(FString::Printf(TEXT("%s stopped at the block's edge (%.0f cm out, at %s after %.1f s)"),
			*Mover->GetName(), Distance, *Location.ToCompactString(), Elapsed), Distance > 0.f && Distance <= ObstacleEdgeTolerance);
		Test->TestTrue(FString::Printf(TEXT("%s stayed on its own side of the block"), *Mover->GetName()), Location.Y > Box.Max.Y);
		Test->TestEqual(FString::Printf(TEXT("%s finished its order"), *Mover->GetName()), Mover->GetQueuedCommandCount(), 0);
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueBuilderBuild, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueBuilderBuild::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();
	const int32 TeamId = Controller->GetTeamId();
	URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	ARTSUnit* Builder = SpawnTestUnit(World, BuilderPath, TeamId, BuilderStart);
	ARTSUnit* SecondBuilder = SpawnTestUnit(World, BuilderPath, TeamId, SecondBuilderStart);
	ARTSUnit* NonBuilder = SpawnTestUnit(World, TankPath, TeamId, NonBuilderStart);
	ARTSUnit* Probe = SpawnTestUnit(World, BuilderPath, TeamId, ProbeStart);
	// Towers need power to build at full speed.
	const ARTSBuilding* Plant = SpawnPowerPlant(World, TeamId);
	if (!Tower || !Builder || !SecondBuilder || !NonBuilder || !Probe || !Plant)
	{
		Test->AddError(TEXT("Could not spawn the builders, the tank, the power plant or load the tower"));
		return true;
	}
	State->Builder = Builder;
	State->SecondBuilder = SecondBuilder;
	State->NonBuilder = NonBuilder;
	State->Probe = Probe;
	ARTSPlayerState* Player = Controller->GetPlayerState<ARTSPlayerState>();
	State->Player = Player;
	State->BuilderCredits = Player->GetCredits();
	Test->TestTrue(TEXT("The builder can build the tower"), Builder->GetBuilderComponent()->CanBuild(Tower));
	Test->TestFalse(TEXT("A tank cannot build"), NonBuilder->GetBuilderComponent()->CanBuild());

	Controller->Server_IssueCommand({ Builder }, MakeBuild(Tower, BuildSpot));
	Test->TestEqual(TEXT("The builder took the order"), Builder->GetQueuedCommandCount(), 1);
	Test->TestEqual(TEXT("Nothing is placed before the builder arrives"), CountBuildings(World), 1);
	Test->TestEqual(TEXT("Nothing is charged before the builder arrives"), Player->GetCredits(), State->BuilderCredits);

	// A tank drops a new building; a builder sent off the map or into a wall places nothing.
	Controller->Server_IssueCommand({ NonBuilder }, MakeBuild(Tower, OtherBuildSpot));
	Test->TestEqual(TEXT("A tank drops a Build order"), NonBuilder->GetQueuedCommandCount(), 0);
	Controller->Server_IssueCommand({ Probe }, MakeBuild(Tower, OffMapSpot));
	Controller->Server_IssueCommand({ Probe }, MakeBuild(Tower, WallSpot, /*bQueue*/ true));
	Test->TestEqual(TEXT("The probe queued both orders"), Probe->GetQueuedCommandCount(), 2);

	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForBuilderSite, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSWaitForBuilderSite::Update()
{
	ARTSUnit* Builder = State->Builder.Get();
	const URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	if (!Builder || !Tower)
	{
		Test->AddError(TEXT("The builder disappeared"));
		return true;
	}
	ARTSBuilding* Site = FindBuildingAt(Builder->GetWorld(), Tower, BuildSpot);
	if (!Site)
	{
		if (FPlatformTime::Seconds() - State->PhaseStart > BuilderTimeout)
		{
			Test->AddError(FString::Printf(TEXT("The builder never placed the tower; it stands at %s"), *Builder->GetActorLocation().ToCompactString()));
			return true;
		}
		return false;
	}

	State->Building = Site;
	Test->TestTrue(FString::Printf(TEXT("The builder walked to the site (from %s to %s)"),
		*BuilderStart.ToCompactString(), *Builder->GetActorLocation().ToCompactString()),
		FVector::Dist2D(Builder->GetActorLocation(), BuilderStart) > 1500.f);
	Test->TestTrue(TEXT("The builder stands at the site when it places it"), IsAtSite(*Builder, *Site));
	Test->TestEqual(TEXT("Placed for the builder's team"), Site->GetTeamId(), Builder->GetTeamId());
	Test->TestEqual(TEXT("Placing charged the tower's cost"), State->Player->GetCredits(), State->BuilderCredits - Tower->Cost);
	Test->TestFalse(TEXT("The site does not build itself"), Site->bAutoConstruct);
	Test->TestFalse(TEXT("The site starts under construction"), Site->IsConstructed());
	Test->TestTrue(TEXT("The builder is constructing"), Builder->GetBuilderComponent()->GetState() == ERTSBuildState::Constructing);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

/** Waits until the first builder has built a quarter of the site, then sends it away. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSBuilderLeavesSite, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSBuilderLeavesSite::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* Builder = State->Builder.Get();
	const ARTSBuilding* Site = State->Building.Get();
	if (!Controller || !Builder || !Site)
	{
		Test->AddError(TEXT("Lost the builder or its site"));
		return true;
	}
	if (Site->GetConstructionProgress() < 0.25f && FPlatformTime::Seconds() - State->PhaseStart < BuilderTimeout)
	{
		return false;
	}
	Test->TestTrue(FString::Printf(TEXT("Construction advanced while the builder stayed (%.2f)"), Site->GetConstructionProgress()),
		Site->GetConstructionProgress() >= 0.25f);

	FRTSCommand Move;
	Move.Type = ERTSCommandType::Move;
	Move.TargetLocation = BuilderAwaySpot;
	Controller->Server_IssueCommand({ Builder }, Move);
	Test->TestTrue(TEXT("The builder left its Build order"), Builder->GetBuilderComponent()->GetState() == ERTSBuildState::Idle);
	State->HeldProgress = Site->GetConstructionProgress();
	State->HoldStartWorldTime = Builder->GetWorld()->GetTimeSeconds();
	return true;
}

/** Watches the site for HoldSeconds of game time and checks it did not advance. */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSVerifySiteHeld, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State, FString, Why);
bool FRTSVerifySiteHeld::Update()
{
	const ARTSBuilding* Site = State->Building.Get();
	if (!Site)
	{
		Test->AddError(TEXT("The site disappeared"));
		return true;
	}
	if (Site->GetWorld()->GetTimeSeconds() - State->HoldStartWorldTime < HoldSeconds)
	{
		return false;
	}
	Test->TestTrue(FString::Printf(TEXT("Construction holds %s (%.3f, was %.3f)"), *Why, Site->GetConstructionProgress(), State->HeldProgress),
		FMath::IsNearlyEqual(Site->GetConstructionProgress(), State->HeldProgress, 0.001f));
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

/** Sends the second builder to resume the site, waits until it has added some progress, then kills it there. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSSecondBuilderResumes, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSSecondBuilderResumes::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* SecondBuilder = State->SecondBuilder.Get();
	ARTSBuilding* Site = State->Building.Get();
	if (!Controller || !SecondBuilder || !Site)
	{
		Test->AddError(TEXT("Lost the second builder or the site"));
		return true;
	}
	if (!State->bResumeIssued)
	{
		// What a right click on the own site sends.
		FRTSCommand Resume;
		Resume.Type = ERTSCommandType::Build;
		Resume.TargetActor = Site;
		Controller->Server_IssueCommand({ SecondBuilder }, Resume);
		Test->TestEqual(TEXT("The second builder took the resume order"), SecondBuilder->GetQueuedCommandCount(), 1);
		Test->TestTrue(TEXT("The second builder works on the same site"), SecondBuilder->GetBuilderComponent()->GetSite() == Site);
		State->bResumeIssued = true;
		return false;
	}
	if (Site->GetConstructionProgress() < State->HeldProgress + 0.2f && FPlatformTime::Seconds() - State->PhaseStart < BuilderTimeout)
	{
		return false;
	}
	Test->TestTrue(FString::Printf(TEXT("The second builder advanced the site (%.2f, was %.2f)"), Site->GetConstructionProgress(), State->HeldProgress),
		Site->GetConstructionProgress() >= State->HeldProgress + 0.2f);
	Test->TestTrue(TEXT("The second builder stands at the site"), IsAtSite(*SecondBuilder, *Site));
	Test->TestFalse(TEXT("The site is not done yet"), Site->IsConstructed());

	SecondBuilder->GetHealthComponent()->ApplyDamage(SecondBuilder->GetHealthComponent()->GetMaxHealth() * 2.f, nullptr);
	Test->TestFalse(TEXT("The second builder died"), IsValid(SecondBuilder));
	State->HeldProgress = Site->GetConstructionProgress();
	State->HoldStartWorldTime = Site->GetWorld()->GetTimeSeconds();
	State->bResumeIssued = false;
	return true;
}

/** Sends the first builder back to finish the site and checks that nothing else was placed or charged. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSFirstBuilderFinishes, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSFirstBuilderFinishes::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* Builder = State->Builder.Get();
	ARTSBuilding* Site = State->Building.Get();
	if (!Controller || !Builder || !Site)
	{
		Test->AddError(TEXT("Lost the builder or the site"));
		return true;
	}
	if (!State->bResumeIssued)
	{
		FRTSCommand Resume;
		Resume.Type = ERTSCommandType::Build;
		Resume.TargetActor = Site;
		Controller->Server_IssueCommand({ Builder }, Resume);
		State->bResumeIssued = true;
		return false;
	}
	if ((!Site->IsConstructed() || Builder->GetQueuedCommandCount() > 0) && FPlatformTime::Seconds() - State->PhaseStart < BuilderTimeout)
	{
		return false;
	}

	UWorld* World = Builder->GetWorld();
	const URTSBuildingData* Tower = Site->GetBuildingData();
	Test->TestTrue(TEXT("The first builder finished the site"), Site->IsConstructed());
	Test->TestEqual(TEXT("Its Build order ended with the construction"), Builder->GetQueuedCommandCount(), 0);
	Test->TestTrue(TEXT("The builder is idle"), Builder->GetBuilderComponent()->GetState() == ERTSBuildState::Idle);
	Test->TestTrue(TEXT("The completed tower is armed"), Site->GetWeaponComponent()->IsArmed());
	const ARTSUnit* Probe = State->Probe.Get();
	Test->TestTrue(TEXT("The probe's orders off the map and into the wall ended"), Probe && Probe->GetQueuedCommandCount() == 0);
	Test->TestNull(TEXT("Nothing was placed off the map"), FindBuildingAt(World, Tower, OffMapSpot));
	Test->TestNull(TEXT("Nothing was placed in the wall"), FindBuildingAt(World, Tower, WallSpot));
	Test->TestNull(TEXT("The tank placed nothing"), FindBuildingAt(World, Tower, OtherBuildSpot));
	Test->TestEqual(TEXT("Only the tower was placed besides the plant"), CountBuildings(World), 2);
	Test->TestEqual(TEXT("Only the tower was charged"), State->Player->GetCredits(), State->BuilderCredits - Tower->Cost);
	return true;
}

/** Orders a power plant and queues a tower behind it with Shift. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueQueuedBuilds, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueQueuedBuilds::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* Builder = State->Builder.Get();
	URTSBuildingData* Plant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	if (!Controller || !Builder || !Plant || !Tower)
	{
		Test->AddError(TEXT("Lost the builder or the building data"));
		return true;
	}
	State->Player->AddCredits(Plant->Cost + Tower->Cost);
	State->BuilderCredits = State->Player->GetCredits();
	Controller->Server_IssueCommand({ Builder }, MakeBuild(Plant, OtherBuildSpot));
	Controller->Server_IssueCommand({ Builder }, MakeBuild(Tower, QueuedTowerSpot, /*bQueue*/ true));
	Test->TestEqual(TEXT("The builder queued both buildings"), Builder->GetQueuedCommandCount(), 2);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyQueuedBuilds, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyQueuedBuilds::Update()
{
	ARTSUnit* Builder = State->Builder.Get();
	const URTSBuildingData* Plant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
	const URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	if (!Builder || !Plant || !Tower)
	{
		Test->AddError(TEXT("Lost the builder"));
		return true;
	}
	UWorld* World = Builder->GetWorld();
	if (!State->QueuedPlant.IsValid())
	{
		State->QueuedPlant = FindBuildingAt(World, Plant, OtherBuildSpot);
	}
	if (!State->QueuedTower.IsValid())
	{
		State->QueuedTower = FindBuildingAt(World, Tower, QueuedTowerSpot);
		// The queued tower must wait for the plant to be finished.
		State->bTowerBeforePlant |= State->QueuedTower.IsValid() && !(State->QueuedPlant.IsValid() && State->QueuedPlant->IsConstructed());
	}

	const bool bDone = State->QueuedPlant.IsValid() && State->QueuedPlant->IsConstructed()
		&& State->QueuedTower.IsValid() && State->QueuedTower->IsConstructed() && Builder->GetQueuedCommandCount() == 0;
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bDone && Elapsed < BuilderTimeout * 2.0)
	{
		return false;
	}

	Test->AddInfo(FString::Printf(TEXT("Queued builds finished after %.1f s"), Elapsed));
	Test->TestTrue(TEXT("The queued power plant was built"), State->QueuedPlant.IsValid() && State->QueuedPlant->IsConstructed());
	Test->TestTrue(TEXT("The queued tower was built"), State->QueuedTower.IsValid() && State->QueuedTower->IsConstructed());
	Test->TestFalse(TEXT("The tower was placed only after the plant was finished"), State->bTowerBeforePlant);
	Test->TestEqual(TEXT("Both were charged"), State->Player->GetCredits(), State->BuilderCredits - Plant->Cost - Tower->Cost);
	Test->TestEqual(TEXT("The builder's queue is empty"), Builder->GetQueuedCommandCount(), 0);
	return true;
}

/** Parks an unarmed enemy on OtherBuildSpot and orders a builder to put a tower there. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueEnemyBlockedBuild, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueEnemyBlockedBuild::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();
	const int32 TeamId = Controller->GetTeamId();
	URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	ARTSUnit* Builder = SpawnTestUnit(World, BuilderPath, TeamId, BuilderStart);
	ARTSUnit* Enemy = SpawnUnarmedEnemy(World, TeamId, OtherBuildSpot);
	if (!Tower || !Builder || !Enemy)
	{
		Test->AddError(TEXT("Could not spawn the builder or the enemy, or load the tower"));
		return true;
	}
	State->Builder = Builder;
	State->SiteEnemy = Enemy;
	ARTSPlayerState* Player = Controller->GetPlayerState<ARTSPlayerState>();
	State->Player = Player;
	State->BuilderCredits = Player->GetCredits();

	Test->TestFalse(TEXT("A builder's ghost rejects a spot covered by an enemy"), ClientAccepts(*World, *Tower, OtherBuildSpot, TeamId));
	Controller->Server_IssueCommand({ Builder }, MakeBuild(Tower, OtherBuildSpot));
	Test->TestEqual(TEXT("The builder took the order"), Builder->GetQueuedCommandCount(), 1);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

/** Waits for the builder to give up on the enemy's spot, then checks nothing was placed, charged or moved. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyEnemyBlocksSite, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyEnemyBlocksSite::Update()
{
	ARTSUnit* Builder = State->Builder.Get();
	ARTSUnit* Enemy = State->SiteEnemy.Get();
	const URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	if (!Builder || !Enemy || !Tower)
	{
		Test->AddError(TEXT("Lost the builder or the enemy"));
		return true;
	}
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (Builder->GetQueuedCommandCount() > 0 && Elapsed < BuilderTimeout)
	{
		return false;
	}

	Test->TestEqual(TEXT("The builder dropped the order"), Builder->GetQueuedCommandCount(), 0);
	Test->TestTrue(FString::Printf(TEXT("The builder walked up to the site first (stands at %s)"), *Builder->GetActorLocation().ToCompactString()),
		FVector::Dist2D(Builder->GetActorLocation(), BuilderStart) > 100.f);
	Test->TestNull(TEXT("Nothing was placed on the enemy"), FindBuildingAt(Builder->GetWorld(), Tower, OtherBuildSpot));
	Test->TestEqual(TEXT("Nothing was charged"), State->Player->GetCredits(), State->BuilderCredits);
	Test->TestTrue(FString::Printf(TEXT("The enemy was not moved (stands at %s)"), *Enemy->GetActorLocation().ToCompactString()),
		FVector::Dist2D(Enemy->GetActorLocation(), OtherBuildSpot) < 10.f);
	Enemy->Destroy();
	return true;
}

/**
 * Parks an idle own tank and an own infantry attacking a sturdy enemy dummy on BuildSpot, then sends the builder to put a
 * tower there.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueOwnBlockedBuild, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSIssueOwnBlockedBuild::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	ARTSUnit* Builder = State->Builder.Get();
	if (!Controller || !Builder)
	{
		Test->AddError(TEXT("Lost the builder"));
		return true;
	}
	UWorld* World = Controller->GetWorld();
	const int32 TeamId = Controller->GetTeamId();
	URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	ARTSUnit* Tank = SpawnTestUnit(World, TankPath, TeamId, BuildSpot);
	ARTSUnit* Infantry = SpawnTestUnit(World, InfantryPath, TeamId, SiteInfantryStart);
	ARTSUnit* Dummy = SpawnUnarmedEnemy(World, TeamId, SiteDummySpot);
	// Towers need power to build at full speed.
	const ARTSBuilding* Plant = SpawnPowerPlant(World, TeamId);
	if (!Tower || !Tank || !Infantry || !Dummy || !Plant)
	{
		Test->AddError(TEXT("Could not spawn the tank, the infantry, the dummy or the power plant"));
		return true;
	}
	// Outlives every shot of the tank, the infantry and the finished tower.
	Dummy->GetHealthComponent()->InitHealth(1.e6f);
	State->SiteTank = Tank;
	State->SiteInfantry = Infantry;
	State->SiteDummy = Dummy;

	Test->TestFalse(TEXT("The debug ghost rejects a spot covered by an own tank"), ClientAccepts(*World, *Tower, BuildSpot));
	Test->TestTrue(TEXT("A builder's ghost accepts a spot covered by an own tank"), ClientAccepts(*World, *Tower, BuildSpot, TeamId));

	FRTSCommand Attack;
	Attack.Type = ERTSCommandType::Attack;
	Attack.TargetActor = Dummy;
	Controller->Server_IssueCommand({ Infantry }, Attack);
	Test->TestEqual(TEXT("The infantry took the attack"), Infantry->GetQueuedCommandCount(), 1);

	State->BuilderCredits = State->Player->GetCredits();
	Controller->Server_IssueCommand({ Builder }, MakeBuild(Tower, BuildSpot));
	Test->TestEqual(TEXT("The builder took the order"), Builder->GetQueuedCommandCount(), 1);
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

/** Waits for the builder to finish the tower and checks the own units stepped off its site and kept their orders. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifySiteCleared, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifySiteCleared::Update()
{
	ARTSUnit* Builder = State->Builder.Get();
	ARTSUnit* Tank = State->SiteTank.Get();
	ARTSUnit* Infantry = State->SiteInfantry.Get();
	ARTSUnit* Dummy = State->SiteDummy.Get();
	const URTSBuildingData* Tower = LoadObject<URTSBuildingData>(nullptr, TowerPath);
	if (!Builder || !Tank || !Infantry || !Dummy || !Tower)
	{
		Test->AddError(TEXT("Lost the builder, the tank, the infantry or the dummy"));
		return true;
	}
	UWorld* World = Builder->GetWorld();
	const ARTSBuilding* Site = FindBuildingAt(World, Tower, BuildSpot);
	const bool bDone = Site && Site->IsConstructed() && Builder->GetQueuedCommandCount() == 0
		&& Infantry->GetWeaponComponent()->GetTarget() == Dummy;
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bDone && Elapsed < BuilderTimeout)
	{
		return false;
	}

	Test->AddInfo(FString::Printf(TEXT("The site was cleared and built after %.1f s"), Elapsed));
	if (!Site)
	{
		Test->AddError(FString::Printf(TEXT("The builder never placed the tower; tank at %s, infantry at %s"),
			*Tank->GetActorLocation().ToCompactString(), *Infantry->GetActorLocation().ToCompactString()));
		return true;
	}
	Test->TestFalse(TEXT("The builder placed the tower"), Site->bAutoConstruct);
	Test->TestTrue(TEXT("The builder finished the tower"), Site->IsConstructed());
	Test->TestEqual(TEXT("Its Build order ended with the construction"), Builder->GetQueuedCommandCount(), 0);
	Test->TestEqual(TEXT("The tower was charged once"), State->Player->GetCredits(), State->BuilderCredits - Tower->Cost);
	Test->TestTrue(FString::Printf(TEXT("The tank stepped off the site (stands at %s)"), *Tank->GetActorLocation().ToCompactString()),
		IsOffFootprint(*Tank, *Site));
	Test->TestEqual(TEXT("The tank is idle again"), Tank->GetQueuedCommandCount(), 0);
	Test->TestTrue(FString::Printf(TEXT("The infantry stepped off the site (stands at %s)"), *Infantry->GetActorLocation().ToCompactString()),
		IsOffFootprint(*Infantry, *Site));
	Test->TestEqual(TEXT("The infantry kept its attack and nothing else"), Infantry->GetQueuedCommandCount(), 1);
	Test->TestTrue(TEXT("The infantry fires at the dummy again"), Infantry->GetWeaponComponent()->GetTarget() == Dummy);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSQueueProduction, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSQueueProduction::Update()
{
	ARTSPlayerController* Controller = GetTestController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld* World = Controller->GetWorld();
	ARTSPlayerState* Player = Controller->GetPlayerState<ARTSPlayerState>();
	URTSBuildingData* BarracksData = LoadObject<URTSBuildingData>(nullptr, BarracksPath);
	URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
	if (!Player || !BarracksData || !Infantry || !SpawnPowerPlant(World, Controller->GetTeamId()))
	{
		Test->AddError(TEXT("Missing the player state, the test barracks or infantry, or the power plant"));
		return true;
	}

	ARTSBuilding* Site = SpawnBuilding(World, BarracksData, Controller->GetTeamId(), BuildSpot, false);
	Test->TestTrue(TEXT("A barracks under construction can be selected"), Controller->SelectBuilding(Site));
	Test->TestNull(TEXT("It offers no production yet"), Controller->GetProductionBuilding());
	Test->TestFalse(TEXT("Nor can units be queued there"), Controller->RequestProduce(Site, Infantry));
	Site->UpdateConstruction(BarracksData->BuildTime);

	ARTSBuilding* Barracks = Controller->GetProductionBuilding();
	if (!Test->TestTrue(TEXT("The completed barracks is the production building"), Barracks == Site))
	{
		return true;
	}
	Test->TestTrue(TEXT("Its menu offers infantry"), Controller->GetProductionMenu().Contains(Infantry));
	Test->TestTrue(TEXT("The build menu stays empty"), Controller->GetBuildMenu().IsEmpty());
	Test->TestTrue(TEXT("The rally point starts outside the barracks"),
		Barracks->GetProductionComponent()->GetRallyPoint().X > Barracks->GetClosestFootprintPoint(RallySpot).X);

	// What a right click with the barracks selected and 1 pressed three times send. The host's RPCs run right away.
	Test->TestTrue(TEXT("The rally point request goes out"), Controller->RequestRallyPoint(Barracks, RallySpot));
	Test->TestTrue(TEXT("The rally point moved"), Barracks->GetProductionComponent()->GetRallyPoint().Equals(RallySpot));
	const int32 CreditsBefore = Player->GetCredits();
	for (int32 Index = 0; Index < ProductionCount; ++Index)
	{
		Test->TestTrue(FString::Printf(TEXT("Infantry %d is queued"), Index), Controller->RequestProduce(Barracks, Infantry));
	}
	Test->TestEqual(TEXT("Each is charged"), Player->GetCredits(), CreditsBefore - ProductionCount * Infantry->Cost);

	// A fourth, cancelled again as Delete does, is refunded and leaves the three.
	Test->TestTrue(TEXT("A fourth is queued"), Controller->RequestProduce(Barracks, Infantry));
	Test->TestTrue(TEXT("And cancelled"), Controller->RequestCancelProduction(Barracks, Infantry));
	Test->TestEqual(TEXT("The cancelled one is refunded"), Player->GetCredits(), CreditsBefore - ProductionCount * Infantry->Cost);
	Test->TestEqual(TEXT("Three are left in the queue"), Barracks->GetProductionComponent()->GetQueue().Num(), ProductionCount);

	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		State->UnitsBefore.Add(*It);
	}
	State->Building = Barracks;
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyProduction, FAutomationTestBase*, Test, TSharedRef<FSmokeState>, State);
bool FRTSVerifyProduction::Update()
{
	const ARTSBuilding* Barracks = State->Building.Get();
	if (!Barracks)
	{
		Test->AddError(TEXT("The barracks disappeared"));
		return true;
	}

	// Only the barracks adds units during this test.
	UWorld* World = Barracks->GetWorld();
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		if (!State->UnitsBefore.Contains(*It) && !State->Produced.Contains(*It))
		{
			State->Produced.Add(*It);
			State->ProducedTimes.Add(World->GetTimeSeconds());
		}
	}
	bool bArrived = State->Produced.Num() == ProductionCount;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : State->Produced)
	{
		bArrived &= Unit.IsValid() && Unit->GetQueuedCommandCount() == 0
			&& FVector::Dist2D(Unit->GetActorLocation(), RallySpot) <= RallyArrivalRadius;
	}
	const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
	if (!bArrived && Elapsed < ProductionTimeout)
	{
		return false;
	}

	Test->AddInfo(FString::Printf(TEXT("Production finished after %.1f s"), Elapsed));
	if (!Test->TestEqual(TEXT("The barracks produced the three infantry"), State->Produced.Num(), ProductionCount))
	{
		return true;
	}
	const float BuildTime = Barracks->GetBuildingData()->Units[0]->BuildTime;
	for (int32 Index = 0; Index < ProductionCount; ++Index)
	{
		const ARTSUnit* Unit = State->Produced[Index].Get();
		if (!Test->TestNotNull(FString::Printf(TEXT("Infantry %d is alive"), Index), Unit))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Unit->GetActorLocation(), RallySpot);
		Test->TestEqual(FString::Printf(TEXT("Infantry %d is the player's"), Index), Unit->GetTeamId(), Barracks->GetTeamId());
		Test->TestTrue(FString::Printf(TEXT("Infantry %d reached the rally point (%.0f cm away)"), Index, Distance),
			Distance <= RallyArrivalRadius);
		if (Index > 0)
		{
			const double Gap = State->ProducedTimes[Index] - State->ProducedTimes[Index - 1];
			Test->TestTrue(FString::Printf(TEXT("Infantry %d came out one build time after the one before (%.2f s)"), Index, Gap),
				FMath::Abs(Gap - BuildTime) <= ProductionTolerance);
		}
	}
	Test->TestEqual(TEXT("The queue is empty"), Barracks->GetProductionComponent()->GetQueue().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTestMapSmokeTest, "WorldDivide.Smoke.TestMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSTestMapSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueMoveToAll(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyArrival(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAttackOrderSmokeTest, "WorldDivide.Smoke.AttackOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSAttackOrderSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueAttack(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyAttack(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingPlacementSmokeTest, "WorldDivide.Smoke.BuildingPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSBuildingPlacementSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckPlacement(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyConstruction(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckCancelConstruction(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRouteAroundBuildingSmokeTest, "WorldDivide.Smoke.RouteAroundBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSRouteAroundBuildingSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceObstacle(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavCut(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueRouteMove(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyRoute(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingOnPathSmokeTest, "WorldDivide.Smoke.BuildingOnPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSBuildingOnPathSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueCrossingMoves(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceOnPath(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSHoldUntilNavCut(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyRoute(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAttackBuildingSmokeTest, "WorldDivide.Smoke.AttackBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSAttackBuildingSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceEnemyBuilding(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavCut(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueBuildingAttack(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyBuildingAttack(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavRestored(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAttackBuildingBehindWallSmokeTest, "WorldDivide.Smoke.AttackBuildingBehindWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSAttackBuildingBehindWallSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceBuildingBehindWall(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavCut(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckWallApproach(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueBuildingAttack(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyBuildingAttack(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUnreachableTargetsSmokeTest, "WorldDivide.Smoke.UnreachableTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSUnreachableTargetsSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceUnreachableTargets(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyUnreachableTargets(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHarvestSmokeTest, "WorldDivide.Smoke.Harvest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSHarvestSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSBuySupplyDepot(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForHarvester(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyHarvest(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHarvestUnreachableDocksSmokeTest, "WorldDivide.Smoke.HarvestUnreachableDocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSHarvestUnreachableDocksSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSPlaceUnreachableDocks(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavCut(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueUnreachableDocksHarvest(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyUnreachableDocks(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSolidObstaclesSmokeTest, "WorldDivide.Smoke.SolidObstacles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSSolidObstaclesSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSCheckSolidObstacles(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMoveIntoObstacleSmokeTest, "WorldDivide.Smoke.MoveIntoObstacle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSMoveIntoObstacleSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueObstacleMove(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyObstacleMove(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuilderConstructsSmokeTest, "WorldDivide.Smoke.BuilderConstructs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSBuilderConstructsSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueBuilderBuild(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForBuilderSite(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSBuilderLeavesSite(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifySiteHeld(this, State, TEXT("while its builder is away")));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSSecondBuilderResumes(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifySiteHeld(this, State, TEXT("once its builder died")));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSFirstBuilderFinishes(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueQueuedBuilds(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyQueuedBuilds(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuilderClearsSiteSmokeTest, "WorldDivide.Smoke.BuilderClearsSite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSBuilderClearsSiteSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueEnemyBlockedBuild(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyEnemyBlocksSite(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueOwnBlockedBuild(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifySiteCleared(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionSmokeTest, "WorldDivide.Smoke.Production",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRTSProductionSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSQueueProduction(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyProduction(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
