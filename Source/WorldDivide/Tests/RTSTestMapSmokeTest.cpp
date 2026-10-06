#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
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

	struct FSmokeState
	{
		TArray<TWeakObjectPtr<ARTSUnit>> OwnUnits;
		TArray<TWeakObjectPtr<ARTSUnit>> EnemyUnits;
		TArray<FVector> EnemyStartLocations;
		double PhaseStart = 0.0;

		// Attack order test only.
		TWeakObjectPtr<ARTSUnit> Attacker;
		TWeakObjectPtr<ARTSUnit> Victim;
	};

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

#endif
