#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Commands/RTSCommand.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestPIE.h"
#include "Units/RTSUnit.h"

/**
 * Terrain on L_TestMap: a tank and an infantry of the east team drive west over Hill_East, the ridge between Wall_East and
 * the east base, 1 m high with 15 degree ramps. Every frame both stand on the ground under them, neither floating nor
 * sunk; they cross the top rather than going around; the tank's mesh leans on the ramps and the infantry stays upright.
 */
namespace RTSTerrainSmokeTest
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	constexpr double NavTimeout = 30.0;
	constexpr double DriveTimeout = 30.0;

	/** The east team, so the east army standing nearby leaves them be; clear of its blocks and of Wall_East. */
	constexpr int32 EastTeam = 1;
	const FVector TankStart(5250.f, 1300.f, 0.f);
	const FVector InfantryStart(5250.f, 1600.f, 0.f);
	constexpr float TargetX = 3700.f;

	constexpr float HillHeight = 100.f;
	/** Feet may be this far from the ground in any frame. */
	constexpr float GroundTolerance = 5.f;
	/** On a 15 degree ramp the tank's mesh leans at least this much. */
	constexpr float MinRampTilt = 10.f;
	constexpr float ArrivalRadius = 300.f;

	struct FTerrainState
	{
		TWeakObjectPtr<ARTSUnit> Tank;
		TWeakObjectPtr<ARTSUnit> Infantry;
		double PhaseStart = 0.0;
		float WorstGroundError = 0.f;
		FString WorstGroundErrorAt;
		float TankHighest = -UE_BIG_NUMBER;
		float InfantryHighest = -UE_BIG_NUMBER;
		float TankRampTilt = 0.f;
		float InfantryTilt = 0.f;
	};

	ARTSUnit* SpawnUnit(UWorld& World, const TCHAR* DataPath, const FVector& Location)
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, DataPath);
		if (!Data)
		{
			return nullptr;
		}
		// Facing west, toward the ridge.
		FTransform Transform(FRotator(0.f, 180.f, 0.f), Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, EastTeam);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	/** The ground under Unit, traced on the visibility channel past both test units, apart from the code under test. */
	bool TraceGround(const FTerrainState& State, const ARTSUnit& Unit, float& OutZ)
	{
		FCollisionQueryParams Params(TEXT("RTSTerrainSmokeTest"));
		Params.AddIgnoredActor(State.Tank.Get());
		Params.AddIgnoredActor(State.Infantry.Get());
		const FVector Feet = Unit.GetNavAgentLocation();
		FHitResult Hit;
		if (!Unit.GetWorld()->LineTraceSingleByChannel(Hit, Feet + FVector(0.f, 0.f, 500.f), Feet - FVector(0.f, 0.f, 500.f),
			ECC_Visibility, Params))
		{
			return false;
		}
		OutZ = Hit.ImpactPoint.Z;
		return true;
	}

	bool HasArrived(const ARTSUnit& Unit, const FVector& Start)
	{
		return Unit.GetQueuedCommandCount() == 0
			&& FVector::Dist2D(Unit.GetActorLocation(), FVector(TargetX, Start.Y, 0.f)) <= ArrivalRadius;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSTerrainWaitForNavigation, FAutomationTestBase*, Test, TSharedRef<RTSTerrainSmokeTest::FTerrainState>, State);
bool FRTSTerrainWaitForNavigation::Update()
{
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}

	// Wait for the session FRTSStartStandalonePIE asked for.
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	if (World && World->HasBegunPlay() && RTSTests::IsNavigationReady(*World))
	{
		RTSTests::ApplyTestTimeDilation(*World);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > RTSTerrainSmokeTest::NavTimeout)
	{
		Test->AddError(TEXT("PIE did not start or the navmesh was not built in time"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSTerrainIssueDrive, FAutomationTestBase*, Test, TSharedRef<RTSTerrainSmokeTest::FTerrainState>, State);
bool FRTSTerrainIssueDrive::Update()
{
	using namespace RTSTerrainSmokeTest;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	ARTSUnit* Tank = World ? SpawnUnit(*World, TankPath, TankStart) : nullptr;
	ARTSUnit* Infantry = World ? SpawnUnit(*World, InfantryPath, InfantryStart) : nullptr;
	if (!Tank || !Infantry)
	{
		Test->AddError(TEXT("No PIE world or unit data"));
		return true;
	}
	State->Tank = Tank;
	State->Infantry = Infantry;

	// Straight to the server, as the east team has no player to send them.
	for (const TPair<ARTSUnit*, FVector>& Drive : { MakeTuple(Tank, TankStart), MakeTuple(Infantry, InfantryStart) })
	{
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = FVector(TargetX, Drive.Value.Y, 0.f);
		Drive.Key->ExecuteCommand(Move);
	}
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSTerrainVerifyDrive, FAutomationTestBase*, Test, TSharedRef<RTSTerrainSmokeTest::FTerrainState>, State);
bool FRTSTerrainVerifyDrive::Update()
{
	using namespace RTSTerrainSmokeTest;
	ARTSUnit* Tank = State->Tank.Get();
	ARTSUnit* Infantry = State->Infantry.Get();
	if (!Tank || !Infantry)
	{
		Test->AddError(TEXT("The tank or the infantry is gone"));
		return true;
	}

	// One sample per frame.
	for (ARTSUnit* Unit : { Tank, Infantry })
	{
		float GroundZ = 0.f;
		if (!TraceGround(*State, *Unit, GroundZ))
		{
			Test->AddError(FString::Printf(TEXT("No ground under %s at %s"), *Unit->GetName(), *Unit->GetActorLocation().ToCompactString()));
			return true;
		}
		const float FeetZ = Unit->GetNavAgentLocation().Z;
		const float Error = FMath::Abs(FeetZ - GroundZ);
		if (Error > State->WorstGroundError)
		{
			State->WorstGroundError = Error;
			State->WorstGroundErrorAt = FString::Printf(TEXT("%s at %s, ground %.1f"), *Unit->GetName(),
				*Unit->GetNavAgentLocation().ToCompactString(), GroundZ);
		}
		const bool bOnRamp = GroundZ > 10.f && GroundZ < HillHeight - 10.f;
		const FRotator Tilt = Unit->GetMeshTilt();
		if (Unit == Tank)
		{
			State->TankHighest = FMath::Max(State->TankHighest, FeetZ);
			if (bOnRamp)
			{
				State->TankRampTilt = FMath::Max(State->TankRampTilt, FMath::Abs(Tilt.Pitch));
			}
		}
		else
		{
			State->InfantryHighest = FMath::Max(State->InfantryHighest, FeetZ);
			State->InfantryTilt = FMath::Max(State->InfantryTilt, FMath::Abs(Tilt.Pitch) + FMath::Abs(Tilt.Roll));
		}
	}

	const bool bArrived = HasArrived(*Tank, TankStart) && HasArrived(*Infantry, InfantryStart);
	if (!bArrived && FPlatformTime::Seconds() - State->PhaseStart < DriveTimeout)
	{
		return false;
	}

	Test->TestTrue(FString::Printf(TEXT("Both crossed the ridge to the west (tank at %s, infantry at %s)"),
		*Tank->GetActorLocation().ToCompactString(), *Infantry->GetActorLocation().ToCompactString()), bArrived);
	Test->TestTrue(FString::Printf(TEXT("Neither floated nor sank (worst %.1f cm: %s)"), State->WorstGroundError, *State->WorstGroundErrorAt),
		State->WorstGroundError <= GroundTolerance);
	Test->TestTrue(FString::Printf(TEXT("The tank drove over the top (highest %.0f cm)"), State->TankHighest),
		State->TankHighest >= HillHeight - GroundTolerance);
	Test->TestTrue(FString::Printf(TEXT("The infantry walked over the top (highest %.0f cm)"), State->InfantryHighest),
		State->InfantryHighest >= HillHeight - GroundTolerance);
	Test->TestTrue(FString::Printf(TEXT("The tank leaned on the ramps (most %.1f degrees)"), State->TankRampTilt),
		State->TankRampTilt >= MinRampTilt);
	Test->TestTrue(FString::Printf(TEXT("The infantry stayed upright (most %.2f degrees)"), State->InfantryTilt),
		State->InfantryTilt <= 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTerrainSmokeTest, "WorldDivide.Smoke.Terrain", RTSTests::TestFlags)

bool FRTSTerrainSmokeTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSTerrainSmokeTest::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSTerrainSmokeTest::TestMap));
		return false;
	}

	const TSharedRef<RTSTerrainSmokeTest::FTerrainState> State = MakeShared<RTSTerrainSmokeTest::FTerrainState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSTerrainWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSTerrainIssueDrive(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSTerrainVerifyDrive(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
