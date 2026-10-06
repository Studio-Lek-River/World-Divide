#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Units/RTSUnit.h"

/**
 * End to end: open L_TestMap in PIE, wait for the navmesh, order every unit (own and enemy) through the
 * player controller's server RPC, and check that only the player's units walk around the wall to the target.
 * A second test orders a lone tank to attack an enemy out of range and checks it closes in and destroys it.
 * The building tests place buildings through the same RPC: valid and invalid spots, construction over BuildTime,
 * and a unit routing around a freshly placed building. The last test has three tanks destroy an enemy building and
 * checks that its hole in the navmesh closes again.
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
		FVector BuildingCenter = FVector::ZeroVector;
	};

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

	Test->TestEqual(TEXT("Only the valid request built anything"), CountBuildings(World), 1);
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
	}
	State->PhaseStart = FPlatformTime::Seconds();
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
			FVector::Dist2D(Attacker->GetActorLocation(), BuildingAttackerStarts[Index]) > 500.f);
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

#endif
