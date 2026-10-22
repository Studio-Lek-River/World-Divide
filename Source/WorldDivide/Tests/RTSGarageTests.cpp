#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSGarageComponent.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Navigation/RTSNavAgents.h"
#include "NavigationSystem.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestGeometry.h"
#include "Tests/RTSTestPIE.h"
#include "Units/RTSGround.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"
#include "Units/RTSUnitShape.h"

/**
 * The HQ's garage (#192) on L_Bench's open ground, four HQs at the four quarter turns, each running one case at once, at a
 * fixed 30 Hz step: five builders queued come out one by one; a builder cancelled while the door opens for it leaves none
 * and the door closes, and the next one comes out the moment it completes; a unit on the lane holds the next builder in the
 * garage until it goes, and the owner's idle unit there is sent off it; an HQ destroyed with a builder on its way out sends
 * nothing more, and the builders already out go on to the rally point. Throughout, every builder appears at the garage's
 * spawn socket facing out with the bay and lane clear, and no hull is in the bay while the door is not fully open.
 */
namespace RTSGarageTests
{
	const TCHAR* BenchMap = TEXT("/Game/Maps/L_Bench");
	const TCHAR* HeadquartersPath = TEXT("/Game/Data/Buildings/DA_TestHeadquarters.DA_TestHeadquarters");
	const TCHAR* BuilderPath = TEXT("/Game/Data/Units/DA_TestBuilder.DA_TestBuilder");

	constexpr double Rate = 30.0;
	constexpr double PlayTimeout = 60.0;
	/** Game seconds each case may take: five builders of 10 s each and their drives out. */
	constexpr double CaseTimeout = 150.0;
	/** A team with no player, for the unit that stands in the way. */
	constexpr int32 StrangerTeam = 7;
	/** Where a builder appears, from the spawn socket; its yaw from the HQ's. */
	constexpr float SpawnTolerance = 50.f;
	constexpr float YawTolerance = 1.f;
	/** Overlaps of hulls shallower than this are the avoidance's touch, not two units in one spot. */
	constexpr float OverlapTolerance = 5.f;
	/** A vehicle arrives near its rally point, crowded by others there. */
	constexpr float RallyReach = 1500.f;
	/** The owner's unit on the lane moves at least a hull's width to let a builder out. */
	constexpr float MadeWay = 300.f;

	struct FCase
	{
		FString Name;
		float Yaw = 0.f;
		FVector2D Center = FVector2D::ZeroVector;
		TWeakObjectPtr<ARTSBuilding> Building;
		/** Every builder the HQ sent out, in the order they appeared. */
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		int32 Step = 0;
		double StepStart = 0.0;
		bool bDone = false;
		TWeakObjectPtr<ARTSUnit> Blocker;
		FVector2D Rally = FVector2D::ZeroVector;
		FVector BlockedAt = FVector::ZeroVector;
		/** The game second the last queued builder completed, and whether one was seen appearing in that frame. */
		double CompletedAt = -1.0;
		TFunction<void(FCase&, double)> Run;
	};

	struct FState
	{
		double PhaseStart = 0.0;
		double CaseStart = 0.0;
		TArray<FCase> Cases;
		TWeakObjectPtr<ARTSPlayerState> Player;
		URTSUnitData* Builder = nullptr;
		URTSBuildingData* Headquarters = nullptr;
		FAutomationTestBase* Test = nullptr;
	};

	/**
	 * Every agent's navmesh is there and none is rebuilding. Not RTSTests::IsNavigationReady: L_Bench's far start spot has its
	 * real-size HQ reaching the map's edge, where that looks for navmesh beside it.
	 */
	bool IsNavigationReady(UWorld& World)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!NavSystem || NavSystem->IsNavigationBuildInProgress())
		{
			return false;
		}
		for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
		{
			if (!RTSNavAgents::GetNavMesh(World, Agent))
			{
				return false;
			}
		}
		return true;
	}

	ARTSPlayerController* GetController()
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		return World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	FBox2D GetBayBox(const ARTSBuilding& Building)
	{
		const URTSBuildingData* Data = Building.GetBuildingData();
		for (const FRTSBuildingCollisionBox& Box : Data->CollisionBoxes)
		{
			if (Box.bGarage)
			{
				const FVector Min = Building.GetActorTransform().TransformPosition(Box.Min * Data->MeshScale);
				const FVector Max = Building.GetActorTransform().TransformPosition(Box.Max * Data->MeshScale);
				return FBox2D(FVector2D(FVector::Min(Min, Max)), FVector2D(FVector::Max(Min, Max)));
			}
		}
		return FBox2D(ForceInit);
	}

	ARTSUnit* SpawnUnit(UWorld& World, URTSUnitData& Data, int32 TeamId, const FVector& Location, float Yaw)
	{
		FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(&Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	/** A spot outside the HQ on its garage's lane, Out cm past the footprint's front edge, on the ground. */
	FVector GetLaneSpot(const ARTSBuilding& Building, float Out)
	{
		const URTSBuildingData* Data = Building.GetBuildingData();
		const FVector Local(RTSBuildPlacement::GetFootprintExtent(Data->Footprint).X + Out, RTSBuildPlacement::GetExitLaneOffset(*Data), 0.f);
		FVector Spot = Building.GetActorTransform().TransformPosition(Local);
		float Ground = Spot.Z;
		RTSGround::Trace(*Building.GetWorld(), Spot, 1000.f, 1000.f, Ground);
		Spot.Z = Ground;
		return Spot;
	}

	void Enqueue(FState& State, FCase& Case, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			State.Test->TestTrue(*FString::Printf(TEXT("%s: builder %d queues"), *Case.Name, Index + 1),
				Case.Building->GetProductionComponent()->Enqueue(*State.Player, State.Builder));
		}
	}

	void NextStep(FCase& Case, double Now)
	{
		++Case.Step;
		Case.StepStart = Now;
	}

	/** Records the builders the HQ sent out since the last frame and checks where each appeared. */
	void Watch(FState& State, FCase& Case, UWorld& World, double Now)
	{
		ARTSBuilding* Building = Case.Building.Get();
		FAutomationTestBase& Test = *State.Test;
		const FBox2D Bay = Building ? GetBayBox(*Building) : FBox2D(ForceInit);
		// The garage drives each one out from the frame it appears.
		if (ARTSUnit* Unit = Building ? Building->GetGarageComponent()->GetExitingUnit() : nullptr; Unit && !Case.Units.Contains(Unit))
		{
			Case.Units.Add(Unit);
			const FTransform Spawn = Building->GetGarageComponent()->GetSpawnTransform();
			Test.TestTrue(*FString::Printf(TEXT("%s: builder %d appears at the garage's spawn socket (%.0f cm off)"), *Case.Name, Case.Units.Num(),
				FVector::Dist2D(Unit->GetActorLocation(), Spawn.GetLocation())),
				FVector::Dist2D(Unit->GetActorLocation(), Spawn.GetLocation()) <= SpawnTolerance);
			Test.TestTrue(*FString::Printf(TEXT("%s: builder %d faces out of the door"), *Case.Name, Case.Units.Num()),
				FMath::Abs(FMath::FindDeltaAngleDegrees(Unit->GetActorRotation().Yaw, Building->GetActorRotation().Yaw)) <= YawTolerance);
			// Nothing else in the bay or on its lane as it appears.
			Test.TestFalse(*FString::Printf(TEXT("%s: the lane is clear as builder %d appears"), *Case.Name, Case.Units.Num()),
				Building->GetGarageComponent()->IsLaneOccupied(Unit));
		}
		if (!Building)
		{
			return;
		}
		// The door never comes down on a hull: none in the bay unless it is fully open.
		const URTSGarageComponent* Garage = Building->GetGarageComponent();
		if (Garage->GetOpenness() < 1.f)
		{
			for (TActorIterator<ARTSUnit> It(&World); It; ++It)
			{
				const float Gap = RTSTests::GetHullGap(It->GetFootprint(), Bay);
				if (Gap < -OverlapTolerance)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s is in the bay while the door is %.0f %% open"), *Case.Name, *It->GetName(),
						100.f * Garage->GetOpenness()));
				}
			}
		}
		// Nothing stands where the builder driving out is: it waits for its lane rather than pushing through.
		if (const ARTSUnit* Exiting = Garage->GetExitingUnit())
		{
			const FRTSFootprint Out = Exiting->GetFootprint();
			for (TActorIterator<ARTSUnit> It(&World); It; ++It)
			{
				if (*It == Exiting)
				{
					continue;
				}
				const FRTSFootprint Other = It->GetFootprint();
				FVector2D OnOut;
				FVector2D OnOther;
				RTSUnitShape::GetClosestSegmentPoints(FVector2D(Out.Center - Out.Axis * Out.HalfSegment), FVector2D(Out.Center + Out.Axis * Out.HalfSegment),
					FVector2D(Other.Center - Other.Axis * Other.HalfSegment), FVector2D(Other.Center + Other.Axis * Other.HalfSegment), OnOut, OnOther);
				const float Depth = Out.Radius + Other.Radius - FVector2D::Distance(OnOut, OnOther);
				if (Depth > OverlapTolerance)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s overlaps the builder driving out by %.0f cm"), *Case.Name, *It->GetName(), Depth));
				}
			}
		}
	}

	/** Five builders queued at once: five come out, one at a time, and the door closes behind the last. */
	void RunFiveQueued(FState& State, FCase& Case, double Now)
	{
		const ARTSBuilding* Building = Case.Building.Get();
		const URTSGarageComponent* Garage = Building->GetGarageComponent();
		switch (Case.Step)
		{
		case 0:
			Enqueue(State, Case, 5);
			NextStep(Case, Now);
			break;
		case 1:
			if (Case.Units.Num() >= 5 && !Garage->GetExitingUnit() && Garage->GetDoorPhase() == ERTSGarageDoor::Closed)
			{
				NextStep(Case, Now);
			}
			break;
		case 2:
			// A while longer: nothing more comes out.
			if (Now - Case.StepStart >= 3.0)
			{
				State.Test->TestEqual(*FString::Printf(TEXT("%s: exactly five builders"), *Case.Name), Case.Units.Num(), 5);
				Case.bDone = true;
			}
			break;
		}
	}

	/**
	 * A builder cancelled while the door opens early for it leaves none, and the door closes again; the next one queued comes
	 * out the frame it completes, the door already open for it.
	 */
	void RunCancel(FState& State, FCase& Case, double Now)
	{
		ARTSBuilding* Building = Case.Building.Get();
		URTSProductionComponent* Production = Building->GetProductionComponent();
		const URTSGarageComponent* Garage = Building->GetGarageComponent();
		FAutomationTestBase& Test = *State.Test;
		switch (Case.Step)
		{
		case 0:
			Enqueue(State, Case, 1);
			NextStep(Case, Now);
			break;
		case 1:
			if (Garage->GetDoorPhase() == ERTSGarageDoor::Opening)
			{
				Test.TestTrue(*FString::Printf(TEXT("%s: the door opens before the builder completes"), *Case.Name), Production->GetQueue().Num() == 1);
				Test.TestTrue(*FString::Printf(TEXT("%s: the builder cancels"), *Case.Name), Production->CancelLast(*State.Player, State.Builder));
				NextStep(Case, Now);
			}
			break;
		case 2:
			if (Garage->GetDoorPhase() == ERTSGarageDoor::Closed && Now - Case.StepStart >= 12.0)
			{
				Test.TestEqual(*FString::Printf(TEXT("%s: no builder for the cancelled one"), *Case.Name), Case.Units.Num(), 0);
				Enqueue(State, Case, 1);
				NextStep(Case, Now);
			}
			break;
		case 3:
			if (Production->GetQueue().IsEmpty() && Case.CompletedAt < 0.0)
			{
				Case.CompletedAt = Now;
				Test.TestEqual(*FString::Printf(TEXT("%s: the builder comes out the frame it completes, the door open for it"), *Case.Name),
					Case.Units.Num(), 1);
			}
			if (Case.Units.Num() == 1 && !Garage->GetExitingUnit() && Garage->GetDoorPhase() == ERTSGarageDoor::Closed)
			{
				NextStep(Case, Now);
			}
			break;
		case 4:
			if (Now - Case.StepStart >= 3.0)
			{
				Test.TestEqual(*FString::Printf(TEXT("%s: exactly one builder"), *Case.Name), Case.Units.Num(), 1);
				Case.bDone = true;
			}
			break;
		}
	}

	/**
	 * A stranger's unit on the lane holds the completed builder in the garage, the door open, until it goes; then the
	 * owner's own idle unit there is sent off the lane, and the next builder comes out without waiting for anyone.
	 */
	void RunBlocked(FState& State, FCase& Case, double Now)
	{
		ARTSBuilding* Building = Case.Building.Get();
		URTSProductionComponent* Production = Building->GetProductionComponent();
		const URTSGarageComponent* Garage = Building->GetGarageComponent();
		FAutomationTestBase& Test = *State.Test;
		UWorld* World = Building->GetWorld();
		switch (Case.Step)
		{
		case 0:
			Case.Blocker = SpawnUnit(*World, *State.Builder, StrangerTeam, GetLaneSpot(*Building, 600.f), Building->GetActorRotation().Yaw);
			Enqueue(State, Case, 1);
			NextStep(Case, Now);
			break;
		case 1:
			if (Production->GetQueue().IsEmpty())
			{
				Case.CompletedAt = Now;
				NextStep(Case, Now);
			}
			break;
		case 2:
			// Held for as long as the stranger stands there.
			if (Now - Case.StepStart >= 5.0)
			{
				Test.TestEqual(*FString::Printf(TEXT("%s: the builder waits in the garage while the lane is blocked"), *Case.Name), Case.Units.Num(), 0);
				Test.TestEqual(*FString::Printf(TEXT("%s: one builder waiting"), *Case.Name), Garage->GetPendingCount(), 1);
				Test.TestEqual(*FString::Printf(TEXT("%s: the door stays open meanwhile"), *Case.Name), Garage->GetDoorPhase(), ERTSGarageDoor::Open);
				if (Case.Blocker.IsValid())
				{
					Case.Blocker->Destroy();
				}
				NextStep(Case, Now);
			}
			break;
		case 3:
			if (Case.Units.Num() == 1 && !Garage->GetExitingUnit() && Garage->GetDoorPhase() == ERTSGarageDoor::Closed)
			{
				// Now the owner's own unit, idle on the lane.
				Case.BlockedAt = GetLaneSpot(*Building, 600.f);
				Case.Blocker = SpawnUnit(*World, *State.Builder, Building->GetTeamId(), Case.BlockedAt, Building->GetActorRotation().Yaw);
				Enqueue(State, Case, 1);
				NextStep(Case, Now);
			}
			break;
		case 4:
			if (Case.Units.Num() == 2 && !Garage->GetExitingUnit() && Garage->GetDoorPhase() == ERTSGarageDoor::Closed)
			{
				const float Moved = Case.Blocker.IsValid() ? FVector::Dist2D(Case.Blocker->GetActorLocation(), Case.BlockedAt) : 0.f;
				Test.TestTrue(*FString::Printf(TEXT("%s: the owner's idle unit made way (%.0f cm)"), *Case.Name, Moved), Moved >= MadeWay);
				Case.bDone = true;
			}
			break;
		}
	}

	/**
	 * Three builders queued, the HQ destroyed as the second drives out: no third comes out, and the two already out reach
	 * the rally point.
	 */
	void RunDestroyed(FState& State, FCase& Case, double Now)
	{
		ARTSBuilding* Building = Case.Building.Get();
		FAutomationTestBase& Test = *State.Test;
		switch (Case.Step)
		{
		case 0:
			Enqueue(State, Case, 3);
			NextStep(Case, Now);
			break;
		case 1:
			if (Building && Case.Units.Num() == 2 && Building->GetGarageComponent()->GetExitingUnit())
			{
				// The rally point, kept for after the HQ is gone.
				Case.Rally = FVector2D(Building->GetProductionComponent()->GetRallyPoint());
				Building->Destroy();
				NextStep(Case, Now);
			}
			break;
		case 2:
			if (Now - Case.StepStart >= 20.0)
			{
				Test.TestEqual(*FString::Printf(TEXT("%s: nothing comes out after the HQ is destroyed"), *Case.Name), Case.Units.Num(), 2);
				for (int32 Index = 0; Index < Case.Units.Num(); ++Index)
				{
					const ARTSUnit* Unit = Case.Units[Index].Get();
					if (!Test.TestNotNull(*FString::Printf(TEXT("%s: builder %d is still there"), *Case.Name, Index + 1), Unit))
					{
						continue;
					}
					Test.TestFalse(*FString::Printf(TEXT("%s: builder %d drives on its own again"), *Case.Name, Index + 1),
						Unit->GetUnitMovement()->IsDrivingThroughNavObstacles());
					const float ToRally = FVector2D::Distance(FVector2D(Unit->GetActorLocation()), Case.Rally);
					Test.TestTrue(*FString::Printf(TEXT("%s: builder %d reached the rally point (%.0f cm off)"), *Case.Name, Index + 1, ToRally),
						ToRally <= RallyReach);
				}
				Case.bDone = true;
			}
			break;
		}
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRTSGarageWaitForPlay, TSharedRef<RTSGarageTests::FState>, State);
bool FRTSGarageWaitForPlay::Update()
{
	using namespace RTSGarageTests;
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	ARTSPlayerController* Controller = GetController();
	if (World && World->HasBegunPlay() && Controller && Controller->GetTeamId() != INDEX_NONE && IsNavigationReady(*World))
	{
		RTSTests::ApplyTestFixedStep(Rate);
		State->Player = Controller->GetPlayerState<ARTSPlayerState>();
		if (!State->Player.IsValid())
		{
			State->Test->AddError(TEXT("No player state"));
			return true;
		}
		State->Player->AddCredits(100000);
		for (FCase& Case : State->Cases)
		{
			FVector Location(Case.Center, 0.f);
			float Ground = 0.f;
			if (RTSGround::Trace(*World, Location, 5000.f, 5000.f, Ground))
			{
				Location.Z = Ground;
			}
			const FTransform Transform(FRotator(0.f, Case.Yaw, 0.f), Location);
			ARTSBuilding* Building = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Building->InitFromData(State->Headquarters, Controller->GetTeamId());
			Building->FinishSpawning(Transform);
			Building->UpdateConstruction(State->Headquarters->BuildTime);
			Case.Building = Building;
		}
		State->PhaseStart = 0.0;
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > PlayTimeout)
	{
		State->Test->AddError(FString::Printf(TEXT("PIE did not start or the navmeshes were not built in time (world %d, begun %d, controller %d, team %d, navigation %d)"),
			World != nullptr, World && World->HasBegunPlay(), Controller != nullptr, Controller ? Controller->GetTeamId() : -2,
			World && IsNavigationReady(*World)));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRTSGarageRunCases, TSharedRef<RTSGarageTests::FState>, State);
bool FRTSGarageRunCases::Update()
{
	using namespace RTSGarageTests;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	if (!World || !State->Player.IsValid())
	{
		State->Test->AddError(TEXT("The PIE world went away"));
		return true;
	}
	const double Now = World->GetTimeSeconds();
	// The HQs' navmesh holes first, as a building placed in a match has them before anything comes out.
	if (State->CaseStart == 0.0)
	{
		for (const FCase& Case : State->Cases)
		{
			if (!Case.Building.IsValid() || Case.Building->IsNavCutPending() || !IsNavigationReady(*World))
			{
				return false;
			}
		}
		State->CaseStart = Now;
		for (FCase& Case : State->Cases)
		{
			Case.StepStart = Now;
		}
	}

	bool bAllDone = true;
	for (FCase& Case : State->Cases)
	{
		if (Case.bDone)
		{
			continue;
		}
		bAllDone = false;
		Watch(*State, Case, *World, Now);
		if (!Case.Building.IsValid() && Case.Step == 0)
		{
			State->Test->AddError(FString::Printf(TEXT("%s: the HQ is gone"), *Case.Name));
			Case.bDone = true;
			continue;
		}
		Case.Run(Case, Now);
		if (!Case.bDone && Now - State->CaseStart > CaseTimeout)
		{
			State->Test->AddError(FString::Printf(TEXT("%s: not over after %.0f game seconds, at step %d, %d builders out"), *Case.Name,
				CaseTimeout, Case.Step, Case.Units.Num()));
			Case.bDone = true;
		}
	}
	return bAllDone;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHQGarageTest, "WorldDivide.Buildings.HQGarage", RTSTests::TestFlags)

bool FRTSHQGarageTest::RunTest(const FString& Parameters)
{
	using namespace RTSGarageTests;
	TSharedRef<FState> State = MakeShared<FState>();
	State->Test = this;
	State->Headquarters = LoadObject<URTSBuildingData>(nullptr, HeadquartersPath);
	State->Builder = LoadObject<URTSUnitData>(nullptr, BuilderPath);
	if (!TestNotNull(TEXT("The HQ's data"), State->Headquarters) || !TestNotNull(TEXT("The builder's data"), State->Builder)
		|| !TestTrue(TEXT("The HQ has a garage"), State->Headquarters->HasGarage()))
	{
		return false;
	}
	if (!AutomationOpenMap(BenchMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), BenchMap));
		return false;
	}

	// Each HQ turned a quarter further, around the middle of the map, its lane pointing away from the others'.
	auto Add = [&State](const TCHAR* Name, float Yaw, const FVector2D& Center, TFunction<void(FState&, FCase&, double)> Run)
	{
		FCase& Case = State->Cases.AddDefaulted_GetRef();
		Case.Name = Name;
		Case.Yaw = Yaw;
		Case.Center = Center;
		FState* Owner = &State.Get();
		Case.Run = [Owner, Run](FCase& Self, double Now) { Run(*Owner, Self, Now); };
	};
	Add(TEXT("Five queued (0 deg)"), 0.f, FVector2D(-6000.f, -6000.f), &RunFiveQueued);
	Add(TEXT("Cancel (90 deg)"), 90.f, FVector2D(6000.f, -6000.f), &RunCancel);
	Add(TEXT("Blocked lane (180 deg)"), 180.f, FVector2D(6000.f, 6000.f), &RunBlocked);
	Add(TEXT("Destroyed (270 deg)"), 270.f, FVector2D(-6000.f, 6000.f), &RunDestroyed);

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSGarageWaitForPlay(State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSGarageRunCases(State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
