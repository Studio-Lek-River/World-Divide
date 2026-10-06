#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
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
 * placed building. Another has three tanks destroy an enemy building
 * and checks that its hole in the navmesh closes again; a variant hides the building behind the map's west wall, so the tanks must
 * approach it from another side. The harvest test buys a supply depot next to a small supply source and follows its
 * harvester through every load until the source is empty. The unreachable-docks test walls the nearest source and depot in
 * with buildings and checks that the harvester uses the open ones instead, then ends its run, loaded, once only the walled
 * depot is left. The unreachable test sends a tank and a harvester at targets beyond
 * the edge of the map and checks that both orders end instead of looping. The obstacle tests check that no navmesh lies inside
 * or on top of the map's walls and blocks, and that tanks sent onto a block stop at its edge on their own side.
 */
namespace
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const FVector MoveTarget(0.f, 0.f, 0.f);
	constexpr int32 UnitsPerTeam = 16;
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

	/** Sends a Build order through the player controller's RPC; returns the new building, or null if the server refused. */
	ARTSBuilding* RequestBuild(ARTSPlayerController& Controller, URTSBuildingData* Data, const FVector& Location)
	{
		UWorld* World = Controller.GetWorld();
		TSet<ARTSBuilding*> Before;
		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			Before.Add(*It);
		}

		FRTSCommand Command;
		Command.Type = ERTSCommandType::Build;
		Command.EntityData = Data;
		Command.TargetLocation = Location;
		Controller.Server_IssueCommand(TArray<ARTSUnit*>(), Command);

		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			if (!Before.Contains(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** What the client's ghost would show for this spot. */
	bool ClientAccepts(UWorld& World, const URTSBuildingData& Data, const FVector& Location)
	{
		FVector Snapped;
		return RTSBuildPlacement::CanPlace(World, Data, Location, Snapped);
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

#endif
