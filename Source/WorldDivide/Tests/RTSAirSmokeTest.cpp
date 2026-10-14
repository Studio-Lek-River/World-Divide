#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSWeaponData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestPIE.h"
#include "UI/Layout/RTSUIBinding.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"

/**
 * End to end in PIE on L_TestMap: the player gets an airfield and a power plant in the empty strip south of the bases and
 * queues a jet there through the controller, as the HUD does. The jet must park on a pad, not walk to the rally point. An
 * attack on an enemy power plant at the far end of the strip must take it off its pad, up to cruise altitude and to the
 * target, where it fires its whole load and nothing more, then back down onto its own pad, where it rearms to a full load.
 */
namespace RTSAirSmokeTest
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* AirfieldPath = TEXT("/Game/Data/Buildings/DA_TestAirfield.DA_TestAirfield");
	const TCHAR* JetPath = TEXT("/Game/Data/Units/DA_TestJet.DA_TestJet");
	const TCHAR* PowerPlantPath = TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant");

	/** The empty strip south of both bases, far out of range of the map's own units. */
	const FVector AirfieldSpot(-6000.f, -8000.f, 0.f);
	const FVector PowerPlantSpot(-3000.f, -8500.f, 0.f);
	const FVector TargetSpot(5000.f, -8000.f, 0.f);

	constexpr double NavTimeout = 30.0;
	constexpr double ProductionTimeout = 60.0;
	constexpr double SortieTimeout = 90.0;

	struct FAirState
	{
		double PhaseStart = 0.0;
		TWeakObjectPtr<ARTSBuilding> Airfield;
		TWeakObjectPtr<ARTSUnit> Jet;
		TWeakObjectPtr<ARTSBuilding> Target;
		FVector Pad = FVector::ZeroVector;
		float TargetStartHealth = 0.f;
		float MaxZ = 0.f;
		bool bSpentLoad = false;
		double LandedAtWorldTime = 0.0;
	};

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

	ARTSBuilding* SpawnCompleted(UWorld& World, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		const FTransform Transform(Location);
		ARTSBuilding* Building = World.SpawnActorDeferred<ARTSBuilding>(
			ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Building->InitFromData(Data, TeamId);
		Building->FinishSpawning(Transform);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForAirPlay, FAutomationTestBase*, Test, TSharedRef<FAirState>, State);
	bool FRTSWaitForAirPlay::Update()
	{
		if (State->PhaseStart == 0.0)
		{
			State->PhaseStart = FPlatformTime::Seconds();
		}
		// AutomationOpenMap starts a PIE session that FRTSStartStandalonePIE replaces; wait for the replacement and its player.
		UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
		const ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (World && World->HasBegunPlay() && Controller && Controller->GetTeamId() != INDEX_NONE && Controller->GetFaction()
			&& RTSTests::IsNavigationReady(*World))
		{
			RTSTests::ApplyTestTimeDilation(*World);
			return true;
		}
		if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
		{
			Test->AddError(TEXT("PIE did not start in time"));
			return true;
		}
		return false;
	}

	/** What the selection panel shows for Key, with the local player's selection. */
	FRTSUIValue GetSelectionValue(APlayerController* Controller, const TCHAR* Key)
	{
		FRTSUIContext Context;
		Context.Player = Controller;
		FRTSUIValue Value;
		for (const IRTSUIBindingSource* Source : RTSUIBindings::GetDefaultSources())
		{
			if (Source->GetValue(Key, Context, Value))
			{
				break;
			}
		}
		return Value;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSQueueJet, FAutomationTestBase*, Test, TSharedRef<FAirState>, State);
	bool FRTSQueueJet::Update()
	{
		ARTSPlayerController* Controller = GetTestController(*Test);
		if (!Controller)
		{
			return true;
		}
		UWorld* World = Controller->GetWorld();
		URTSBuildingData* AirfieldData = LoadObject<URTSBuildingData>(nullptr, AirfieldPath);
		URTSBuildingData* PowerPlantData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		URTSAircraftData* JetData = LoadObject<URTSAircraftData>(nullptr, JetPath);
		if (!AirfieldData || !PowerPlantData || !JetData)
		{
			Test->AddError(TEXT("Missing the test airfield, power plant or jet"));
			return true;
		}

		SpawnCompleted(*World, PowerPlantData, Controller->GetTeamId(), PowerPlantSpot);
		ARTSBuilding* Airfield = SpawnCompleted(*World, AirfieldData, Controller->GetTeamId(), AirfieldSpot);
		Test->TestTrue(TEXT("The airfield can be selected"), Controller->SelectBuilding(Airfield));
		Test->TestTrue(TEXT("Its menu offers the jet"), Controller->GetProductionMenu().Contains(JetData));
		// What 1 with the airfield selected sends. The host's RPC runs right away.
		Test->TestTrue(TEXT("The jet is queued"), Controller->RequestProduce(Airfield, JetData));
		Test->TestEqual(TEXT("It keeps one of the four pads"), Airfield->GetFreePadCount(), AirfieldData->AirfieldPads - 1);
		State->Airfield = Airfield;
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSWaitForJet, FAutomationTestBase*, Test, TSharedRef<FAirState>, State);
	bool FRTSWaitForJet::Update()
	{
		ARTSBuilding* Airfield = State->Airfield.Get();
		if (!Airfield)
		{
			Test->AddError(TEXT("The airfield disappeared"));
			return true;
		}
		ARTSUnit* Jet = Airfield->GetPadAircraft(0);
		if (!Jet)
		{
			if (FPlatformTime::Seconds() - State->PhaseStart > ProductionTimeout)
			{
				Test->AddError(TEXT("The airfield never produced the jet"));
				return true;
			}
			return false;
		}

		const URTSAircraftComponent* Aircraft = Jet->GetAircraftComponent();
		State->Jet = Jet;
		State->Pad = Airfield->GetPadLocation(0) + FVector(0.f, 0.f, Jet->GetGroundOffset());
		Test->TestEqual(TEXT("The jet is parked"), Aircraft->GetState(), ERTSAircraftState::Parked);
		Test->TestTrue(FString::Printf(TEXT("On its pad, not at the exit (stands at %s)"), *Jet->GetActorLocation().ToCompactString()),
			FVector::Dist(Jet->GetActorLocation(), State->Pad) < 1.f);
		Test->TestEqual(TEXT("With no order to the rally point"), Aircraft->GetMission(), ERTSAircraftMission::None);
		Test->TestEqual(TEXT("And a full load"), Jet->GetWeaponComponent()->GetAmmo(),
			CastChecked<URTSAircraftData>(Jet->GetUnitData())->AmmoPerSortie);
		Test->TestFalse(TEXT("A full load needs no rearming"), Aircraft->IsRearming());

		ARTSPlayerController* Controller = GetTestController(*Test);
		if (Controller)
		{
			Controller->SelectUnits({ Jet });
			Test->TestTrue(TEXT("The selection panel shows an aircraft"), GetSelectionValue(Controller, TEXT("selection.isAircraft")).bBool);
			Test->TestEqual(TEXT("with its full load"), GetSelectionValue(Controller, TEXT("selection.ammo")).Number,
				GetSelectionValue(Controller, TEXT("selection.ammoMax")).Number);
			Test->TestTrue(TEXT("of more than nothing"), GetSelectionValue(Controller, TEXT("selection.ammoMax")).Number > 0.0);
			Test->TestFalse(TEXT("not rearming"), GetSelectionValue(Controller, TEXT("selection.isRearming")).bBool);
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSIssueSortie, FAutomationTestBase*, Test, TSharedRef<FAirState>, State);
	bool FRTSIssueSortie::Update()
	{
		ARTSPlayerController* Controller = GetTestController(*Test);
		URTSBuildingData* PowerPlantData = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		if (!Controller || !State->Jet.IsValid() || !PowerPlantData)
		{
			Test->AddError(TEXT("Lost the controller or the jet"));
			return true;
		}

		ARTSBuilding* Target = SpawnCompleted(*Controller->GetWorld(), PowerPlantData, Controller->GetTeamId() + 1, TargetSpot);
		State->Target = Target;
		State->TargetStartHealth = Target->GetHealthComponent()->GetHealth();
		State->MaxZ = State->Pad.Z;

		// What a right click on the enemy plant with the jet selected sends.
		FRTSCommand Attack;
		Attack.Type = ERTSCommandType::Attack;
		Attack.TargetActor = Target;
		Controller->Server_IssueCommand({ State->Jet.Get() }, Attack);
		Test->TestEqual(TEXT("The jet takes the attack"), State->Jet->GetAircraftComponent()->GetMission(), ERTSAircraftMission::Attack);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifySortie, FAutomationTestBase*, Test, TSharedRef<FAirState>, State);
	bool FRTSVerifySortie::Update()
	{
		const ARTSUnit* Jet = State->Jet.Get();
		const ARTSBuilding* Target = State->Target.Get();
		if (!Jet || !Target)
		{
			Test->AddError(TEXT("The jet or its target disappeared"));
			return true;
		}
		const URTSAircraftComponent* Aircraft = Jet->GetAircraftComponent();
		const URTSAircraftData* JetData = CastChecked<URTSAircraftData>(Jet->GetUnitData());
		State->MaxZ = FMath::Max(State->MaxZ, Jet->GetActorLocation().Z);
		State->bSpentLoad |= Jet->GetWeaponComponent()->GetAmmo() == 0;

		const bool bLanded = State->bSpentLoad && Aircraft->GetState() == ERTSAircraftState::Parked;
		const double Elapsed = FPlatformTime::Seconds() - State->PhaseStart;
		if (!bLanded && Elapsed < SortieTimeout)
		{
			return false;
		}

		Test->AddInfo(FString::Printf(TEXT("Sortie flown in %.1f s"), Elapsed));
		const float CruiseZ = State->Airfield.IsValid() ? State->Airfield->GetActorLocation().Z + Jet->GetGroundOffset() + JetData->CruiseAltitude : 0.f;
		Test->TestTrue(FString::Printf(TEXT("The jet climbed to cruise altitude (highest %.0f, cruise %.0f)"), State->MaxZ, CruiseZ),
			State->MaxZ >= CruiseZ - 1.f);
		Test->TestTrue(TEXT("It fired its whole load"), State->bSpentLoad);
		const float Damage = State->TargetStartHealth - Target->GetHealthComponent()->GetHealth();
		Test->TestEqual(TEXT("Every shot of the load hit the target, and no more"), Damage,
			JetData->AmmoPerSortie * JetData->Weapon->Damage);
		Test->TestTrue(FString::Printf(TEXT("It landed back on its pad (stands at %s)"), *Jet->GetActorLocation().ToCompactString()),
			bLanded && FVector::Dist(Jet->GetActorLocation(), State->Pad) < 1.f);
		Test->TestTrue(TEXT("Bound to the same airfield"), Aircraft->GetAirfield() == State->Airfield.Get());
		Test->TestTrue(TEXT("It is rearming on its pad"), Aircraft->IsRearming());
		if (ARTSPlayerController* Controller = GetTestController(*Test))
		{
			Controller->SelectUnits({ State->Jet.Get() });
			Test->TestTrue(TEXT("The selection panel shows it rearming"), GetSelectionValue(Controller, TEXT("selection.isRearming")).bBool);
			Test->TestTrue(TEXT("short of a full load"), GetSelectionValue(Controller, TEXT("selection.ammo")).Number
				< GetSelectionValue(Controller, TEXT("selection.ammoMax")).Number);
		}
		State->LandedAtWorldTime = Jet->GetWorld()->GetTimeSeconds();
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVerifyRearm, FAutomationTestBase*, Test, TSharedRef<FAirState>, State);
	bool FRTSVerifyRearm::Update()
	{
		const ARTSUnit* Jet = State->Jet.Get();
		if (!Jet)
		{
			Test->AddError(TEXT("The jet disappeared"));
			return true;
		}
		const URTSAircraftData* JetData = CastChecked<URTSAircraftData>(Jet->GetUnitData());
		const double Waited = Jet->GetWorld()->GetTimeSeconds() - State->LandedAtWorldTime;
		if (Waited < JetData->RearmSeconds + 0.5)
		{
			return false;
		}
		Test->TestEqual(TEXT("The jet rearmed to a full load on its pad"), Jet->GetWeaponComponent()->GetAmmo(), JetData->AmmoPerSortie);
		Test->TestEqual(TEXT("And stays parked"), Jet->GetAircraftComponent()->GetState(), ERTSAircraftState::Parked);
		Test->TestFalse(TEXT("No longer rearming"), Jet->GetAircraftComponent()->IsRearming());
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAirSortieSmokeTest, "WorldDivide.Smoke.AirSortie", RTSTests::TestFlags)

	bool FRTSAirSortieSmokeTest::RunTest(const FString& Parameters)
	{
		if (!AutomationOpenMap(TestMap))
		{
			AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
			return false;
		}

		const TSharedRef<FAirState> State = MakeShared<FAirState>();
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForAirPlay(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSQueueJet(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSWaitForJet(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSIssueSortie(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifySortie(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSVerifyRearm(this, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

#endif
