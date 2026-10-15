#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Commands/RTSCommand.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
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
#include "Tests/RTSTestWorld.h"
#include "UObject/StrongObjectPtr.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

/**
 * Navigation by hull size on L_TestMap. Units walk the navmesh of the narrowest agent class wide enough for their hull
 * (RTSNavAgents), and RTSHullClearance keeps the rest of the hull, its length and corners, out of obstacles. Besides the
 * placeholders and the M20 the tests drive two hulls no agent was tuned for, engine cubes stretched to them: a long
 * vehicle, longer and narrower than any placeholder, which walks the vehicle navmesh, and a narrow one, which fits the
 * infantry navmesh but is several times as long as wide.
 */
namespace RTSNavigationTests
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	const TCHAR* PowerPlantPath = TEXT("/Game/Data/Buildings/DA_TestPowerPlant.DA_TestPowerPlant");
	const TCHAR* VehicleFactoryPath = TEXT("/Game/Data/Buildings/DA_TestVehicleFactory.DA_TestVehicleFactory");
	const TCHAR* BarracksPath = TEXT("/Game/Data/Buildings/DA_TestBarracks.DA_TestBarracks");
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	constexpr double NavTimeout = 30.0;

	/** Hulls the agents were not tuned for: length, width and height in cm. */
	const FVector LongVehicleSize(1000.f, 160.f, 250.f);
	const FVector NarrowVehicleSize(450.f, 70.f, 150.f);

	/** A hull may touch an obstacle by this much at most: the hull box against the obstacle's collision box. */
	constexpr float OverlapTolerance = 2.f;
	/** A hull ordered into an obstacle stops at most this far from it. */
	constexpr float StopGap = 50.f;

	/** Game seconds a drive may take before it counts as stuck, and the time it stands once every order is done. */
	constexpr double DriveTimeout = 40.0;
	constexpr double SettleSeconds = 0.5;

	/**
	 * The gap test's walls of power plants along x = 0: north of the bases one with a 3 m gap from y = 6800 to 7100, south
	 * of them one with a 6 m gap from y = -7300 to -6700. Going around either end is far longer than going through.
	 */
	const float NarrowGapPlantYs[] = { 5400.f, 5800.f, 6200.f, 6600.f, 7300.f, 7700.f, 8100.f, 8500.f };
	constexpr float NarrowGapMinY = 6800.f;
	constexpr float NarrowGapMaxY = 7100.f;
	const float WideGapPlantYs[] = { -8700.f, -8300.f, -7900.f, -7500.f, -6500.f, -6100.f, -5700.f, -5300.f };
	constexpr float WideGapMinY = -7300.f;
	constexpr float WideGapMaxY = -6700.f;
	constexpr float GateStartX = -2000.f;
	constexpr float GateTargetX = 1800.f;
	/** A unit counts as arrived this close to its target; units sent together to one point crowd around it. */
	constexpr float ArrivalRadius = 900.f;

	/** The vehicle factory the hull test drives at, where rts.Debug.UnitShots puts one, its exit facing east. */
	const FVector FactorySpot(-2000.f, -6500.f, 0.f);

	/** A placeholder hull for a size no unit has: an engine cube stretched to Size. */
	URTSUnitData* MakeBoxUnit(const TCHAR* Name, const FVector& Size, float Speed, float TurnRate)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), URTSUnitData::StaticClass(), Name));
		Data->Mesh = LoadObject<UStaticMesh>(nullptr, CubePath);
		Data->MeshScale = Size / 100.f;
		Data->SelectionRadius = 0.5f * Size.Y;
		Data->MaxHealth = 300.f;
		Data->MoveSpeed = Speed;
		Data->TurnRate = TurnRate;
		return Data;
	}

	/** One unit of a drive: what it is, where it starts and which way it faces. */
	struct FUnitStart
	{
		URTSUnitData* Data = nullptr;
		FVector Location = FVector::ZeroVector;
		float Yaw = 0.f;
		/** Where it is sent, relative to the drive's target, as a formation would spread it. */
		FVector TargetOffset = FVector::ZeroVector;
	};

	enum class EGate : uint8
	{
		None,
		/** Every unit crosses x = 0 between GateMinY and GateMaxY. */
		Through,
		/** No unit crosses x = 0 there; each goes around. */
		Around,
	};

	/** Units sent to one target, each on its own order, and what they must do on the way. */
	struct FDrive
	{
		FString Name;
		TArray<FUnitStart> Units;
		FVector Target = FVector::ZeroVector;
		/** The target lies in an obstacle: each unit must stop with its hull within StopGap of one. */
		bool bStopAtObstacle = false;
		EGate Gate = EGate::None;
		float GateMinY = 0.f;
		float GateMaxY = 0.f;
	};

	struct FNavState
	{
		/** Keeps the box units' data alive. */
		TArray<TStrongObjectPtr<URTSUnitData>> BoxUnits;
		URTSUnitData* M20 = nullptr;
		URTSUnitData* Tank = nullptr;
		URTSUnitData* Infantry = nullptr;
		URTSUnitData* LongVehicle = nullptr;
		URTSUnitData* NarrowVehicle = nullptr;

		/** Buildings the test places, and the navmesh holes it waits for. */
		TArray<FVector> PlacedSpots;
		TArray<TWeakObjectPtr<ARTSBuilding>> Placed;
		double PhaseStart = 0.0;

		TArray<FDrive> Drives;
		int32 DriveIndex = -1;
		double DriveStart = 0.0;
		double SettleStart = -1.0;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		TArray<FVector> LastLocations;
		TArray<TOptional<float>> CrossingYs;
		TArray<float> MinGaps;
		TArray<FString> MinGapObstacles;

		/** The obstacles' boxes seen from above: the map's walls and blocks and every building. */
		TArray<FBox2D> Obstacles;
		TArray<FString> ObstacleNames;

		bool LoadUnits(FAutomationTestBase& Test)
		{
			M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
			Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
			Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
			LongVehicle = MakeBoxUnit(TEXT("NavTestLongVehicle"), LongVehicleSize, 450.f, 90.f);
			NarrowVehicle = MakeBoxUnit(TEXT("NavTestNarrowVehicle"), NarrowVehicleSize, 500.f, 180.f);
			BoxUnits.Emplace(LongVehicle);
			BoxUnits.Emplace(NarrowVehicle);
			if (!M20 || !Tank || !Infantry || !LongVehicle->Mesh)
			{
				Test.AddError(TEXT("Could not load the unit data"));
				return false;
			}
			return true;
		}
	};

	ARTSPlayerController* GetController(FAutomationTestBase& Test)
	{
		UWorld* World = AutomationCommon::GetAnyGameWorld();
		ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller)
		{
			Test.AddError(TEXT("No ARTSPlayerController in the PIE world"));
		}
		return Controller;
	}

	ARTSUnit* SpawnUnit(UWorld& World, URTSUnitData* Data, int32 TeamId, const FVector& Location, float Yaw)
	{
		FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(
			ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	FString DescribeUnit(const URTSUnitData& Data)
	{
		const FRTSUnitShape Shape = RTSUnitShape::FromData(Data);
		return FString::Printf(TEXT("%s (%.0f x %.0f cm, %s)"), *Data.GetName(), 2.f * Shape.GetHalfLength(), 2.f * Shape.GetHalfWidth(),
			*RTSNavAgents::SelectAgent(Shape).Name.ToString());
	}

	/** The map's walls and blocks and every building, as boxes seen from above. */
	void GatherObstacles(UWorld& World, FNavState& State)
	{
		State.Obstacles.Reset();
		State.ObstacleNames.Reset();
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			const FString Label = It->GetActorLabel();
			if (Label.StartsWith(TEXT("Wall_")) || Label.StartsWith(TEXT("Block_")))
			{
				const FBox Box = It->GetComponentsBoundingBox();
				State.Obstacles.Add(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)));
				State.ObstacleNames.Add(Label);
			}
		}
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			const FVector Extent = RTSBuildPlacement::GetFootprintExtent(It->GetFootprint());
			const FVector2D Center(It->GetActorLocation());
			State.Obstacles.Add(FBox2D(Center - FVector2D(Extent), Center + FVector2D(Extent)));
			State.ObstacleNames.Add(It->GetName());
		}
	}

	/** The smallest gap between Unit's hull and any obstacle, and which one. */
	float GetObstacleGap(const ARTSUnit& Unit, const FNavState& State, FString* OutObstacle = nullptr)
	{
		const FRTSFootprint Footprint = Unit.GetFootprint();
		float Gap = FLT_MAX;
		for (int32 Index = 0; Index < State.Obstacles.Num(); ++Index)
		{
			const float ObstacleGap = RTSTests::GetHullGap(Footprint, State.Obstacles[Index]);
			if (ObstacleGap < Gap)
			{
				Gap = ObstacleGap;
				if (OutObstacle)
				{
					*OutObstacle = State.ObstacleNames[Index];
				}
			}
		}
		return Gap;
	}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavWaitForNavigation, FAutomationTestBase*, Test, TSharedRef<FNavState>, State);
bool FRTSNavWaitForNavigation::Update()
{
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	const ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const bool bPlaying = World && World->HasBegunPlay() && Controller && Controller->GetTeamId() != INDEX_NONE
		&& Controller->GetFaction() && Controller->GetCredits() > 0;
	if (bPlaying && RTSTests::IsNavigationReady(*World))
	{
		RTSTests::ApplyTestTimeDilation(*World);
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("PIE did not start or the navmeshes were not built in time"));
		return true;
	}
	return false;
}

/**
 * Places a building of BuildingPath for the player at each spot where placement allows it, as the server does once a
 * builder arrives, without the credits and prerequisites a player's order needs.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSNavPlaceBuildings, FAutomationTestBase*, Test, TSharedRef<FNavState>, State, FString, BuildingPath);
bool FRTSNavPlaceBuildings::Update()
{
	ARTSPlayerController* Controller = GetController(*Test);
	URTSBuildingData* Data = LoadObject<URTSBuildingData>(nullptr, *BuildingPath);
	if (!Controller || !Data)
	{
		Test->AddError(TEXT("No controller or building data"));
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	for (const FVector& Spot : State->PlacedSpots)
	{
		FVector Location;
		ARTSBuilding* Building = nullptr;
		if (Test->TestTrue(*FString::Printf(TEXT("%s may stand at %s"), *Data->GetName(), *Spot.ToCompactString()),
			RTSBuildPlacement::CanPlace(World, *Data, Spot, Location)))
		{
			const FTransform Transform(Location);
			Building = World.SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Building->InitFromData(Data, Controller->GetTeamId());
			Building->FinishSpawning(Transform);
		}
		State->Placed.Add(Building);
	}
	State->PlacedSpots.Reset();
	State->PhaseStart = FPlatformTime::Seconds();
	return true;
}

/** Waits until every placed building's hole is cut in every agent's navmesh. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavWaitForHoles, FAutomationTestBase*, Test, TSharedRef<FNavState>, State);
bool FRTSNavWaitForHoles::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	UNavigationSystemV1* NavSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSystem)
	{
		Test->AddError(TEXT("No navigation system"));
		return true;
	}
	bool bCut = !NavSystem->IsNavigationBuildInProgress();
	for (const TWeakObjectPtr<ARTSBuilding>& Building : State->Placed)
	{
		for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
		{
			const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*World, Agent);
			FNavLocation Projected;
			bCut &= Building.IsValid() && NavMesh && !NavMesh->ProjectPoint(Building->GetActorLocation(), Projected, FVector(10.f, 10.f, 250.f));
		}
	}
	if (bCut)
	{
		// The dynamic rebuild of every agent's tiles under the new buildings, in real time (plan section 7).
		Test->AddInfo(FString::Printf(TEXT("%d buildings cut into %d navmeshes in %.2f s"), State->Placed.Num(), RTSNavAgents::GetAgents().Num(),
			FPlatformTime::Seconds() - State->PhaseStart));
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > NavTimeout)
	{
		Test->AddError(TEXT("The buildings never cut the navmeshes"));
		return true;
	}
	return false;
}

/** Runs State's drives one after the other and checks each as it ends. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavRunDrives, FAutomationTestBase*, Test, TSharedRef<FNavState>, State);
bool FRTSNavRunDrives::Update()
{
	ARTSPlayerController* Controller = GetController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	const double Now = World.GetTimeSeconds();

	if (State->DriveIndex >= 0)
	{
		const FDrive& Drive = State->Drives[State->DriveIndex];
		bool bDone = true;
		for (int32 Index = 0; Index < State->Units.Num(); ++Index)
		{
			const ARTSUnit* Unit = State->Units[Index].Get();
			if (!Unit)
			{
				continue;
			}
			const FVector Location = Unit->GetActorLocation();
			const FVector& Last = State->LastLocations[Index];
			if (!State->CrossingYs[Index] && Last.X < 0.f && Location.X >= 0.f)
			{
				State->CrossingYs[Index] = FMath::Lerp(Last.Y, Location.Y, static_cast<float>(-Last.X / (Location.X - Last.X)));
			}
			State->LastLocations[Index] = Location;
			FString Obstacle;
			const float Gap = GetObstacleGap(*Unit, *State, &Obstacle);
			if (Gap < State->MinGaps[Index])
			{
				State->MinGaps[Index] = Gap;
				State->MinGapObstacles[Index] = Obstacle;
			}
			bDone &= Unit->GetQueuedCommandCount() == 0;
		}
		if (bDone && State->SettleStart < 0.0)
		{
			State->SettleStart = Now;
		}
		const bool bTimedOut = Now - State->DriveStart > DriveTimeout;
		if (!bTimedOut && (State->SettleStart < 0.0 || Now - State->SettleStart < SettleSeconds))
		{
			return false;
		}

		for (int32 Index = 0; Index < State->Units.Num(); ++Index)
		{
			ARTSUnit* Unit = State->Units[Index].Get();
			const FString What = FString::Printf(TEXT("%s: %s"), *Drive.Name, *DescribeUnit(*Drive.Units[Index].Data));
			if (!Test->TestNotNull(*FString::Printf(TEXT("%s is alive"), *What), Unit))
			{
				continue;
			}
			Test->TestEqual(*FString::Printf(TEXT("%s finished its order"), *What), Unit->GetQueuedCommandCount(), 0);
			Test->TestTrue(*FString::Printf(TEXT("%s never overlapped an obstacle (closest %.1f cm, %s)"), *What, State->MinGaps[Index],
				*State->MinGapObstacles[Index]), State->MinGaps[Index] >= -OverlapTolerance);
			const float FinalGap = GetObstacleGap(*Unit, *State);
			if (Drive.bStopAtObstacle)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s stopped within %.0f cm of the obstacle (%.1f cm)"), *What, StopGap, FinalGap),
					FinalGap >= -OverlapTolerance && FinalGap <= StopGap);
			}
			else
			{
				Test->TestTrue(*FString::Printf(TEXT("%s arrived (%.0f cm from the target)"), *What, FVector::Dist2D(Unit->GetActorLocation(), Drive.Target + Drive.Units[Index].TargetOffset)),
					FVector::Dist2D(Unit->GetActorLocation(), Drive.Target + Drive.Units[Index].TargetOffset) <= ArrivalRadius);
			}
			const TOptional<float>& Crossing = State->CrossingYs[Index];
			const bool bInGate = Crossing && *Crossing >= Drive.GateMinY && *Crossing <= Drive.GateMaxY;
			const FString Crossed = Crossing ? FString::Printf(TEXT("y = %.0f"), *Crossing) : FString(TEXT("nowhere"));
			if (Drive.Gate == EGate::Through)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s went through the gap (crossed at %s)"), *What, *Crossed), bInGate);
			}
			else if (Drive.Gate == EGate::Around)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s went around the gap (crossed at %s)"), *What, *Crossed), Crossing.IsSet() && !bInGate);
			}
			Test->AddInfo(FString::Printf(TEXT("%s: closest %.1f cm to %s, stopped %.1f cm from an obstacle, crossed x = 0 at %s, %.1f s"),
				*What, State->MinGaps[Index], *State->MinGapObstacles[Index], FinalGap, *Crossed, Now - State->DriveStart));
			Unit->Destroy();
		}
		State->Units.Reset();
	}

	++State->DriveIndex;
	if (!State->Drives.IsValidIndex(State->DriveIndex))
	{
		return true;
	}
	const FDrive& Next = State->Drives[State->DriveIndex];
	GatherObstacles(World, *State);
	State->LastLocations.Reset();
	State->CrossingYs.Reset();
	State->MinGaps.Reset();
	State->MinGapObstacles.Reset();
	for (const FUnitStart& Start : Next.Units)
	{
		ARTSUnit* Unit = SpawnUnit(World, Start.Data, Controller->GetTeamId(), Start.Location, Start.Yaw);
		State->Units.Add(Unit);
		State->LastLocations.Add(Unit->GetActorLocation());
		State->CrossingYs.AddDefaulted();
		State->MinGaps.Add(FLT_MAX);
		State->MinGapObstacles.AddDefaulted();
	}
	for (int32 Index = 0; Index < State->Units.Num(); ++Index)
	{
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = Next.Target + Next.Units[Index].TargetOffset;
		Controller->Server_IssueCommand({ State->Units[Index].Get() }, Move);
	}
	State->DriveStart = Now;
	State->SettleStart = -1.0;
	return false;
}

/**
 * GetClearance against the true distance between hull and obstacle boxes, around Block_North and Wall_West, for every test
 * hull at several headings and distances: it must flag every overlap and no hull standing StopGap or more away.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavMeasureClearance, FAutomationTestBase*, Test, TSharedRef<FNavState>, State);
bool FRTSNavMeasureClearance::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	if (!World || !State->LoadUnits(*Test))
	{
		return true;
	}
	GatherObstacles(*World, *State);

	const float Gaps[] = { -40.f, -10.f, 0.f, 10.f, 25.f, StopGap, 100.f, 150.f };
	const float Headings[] = { 0.f, 30.f, 45.f, 90.f, 135.f };
	int32 Checked = 0;
	int32 Missed = 0;
	int32 Wasted = 0;
	for (URTSUnitData* Data : { State->M20, State->Tank, State->Infantry, State->LongVehicle, State->NarrowVehicle })
	{
		const FRTSUnitShape Shape = RTSUnitShape::FromData(*Data);
		const FNavDataConfig& Agent = RTSNavAgents::SelectAgent(Shape);
		const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*World, Agent);
		if (!Test->TestNotNull(*FString::Printf(TEXT("%s has a navmesh"), *Data->GetName()), NavMesh))
		{
			continue;
		}
		// Recast grows box obstacles by the agent radius square, not round, so beside a corner the clearance may be this much
		// more careful than it needs to be.
		const float CornerAllowance = (UE_SQRT_2 - 1.f) * Agent.AgentRadius;
		float WorstMiss = 0.f;
		float WidestFlagged = 0.f;
		for (int32 ObstacleIndex = 0; ObstacleIndex < State->Obstacles.Num(); ++ObstacleIndex)
		{
			const FString& Name = State->ObstacleNames[ObstacleIndex];
			if (Name != TEXT("Block_North") && Name != TEXT("Wall_West"))
			{
				continue;
			}
			const FBox2D& Box = State->Obstacles[ObstacleIndex];
			for (float Around = 0.f; Around < 360.f; Around += 15.f)
			{
				const FVector2D Direction(FMath::Cos(FMath::DegreesToRadians(Around)), FMath::Sin(FMath::DegreesToRadians(Around)));
				for (const float Heading : Headings)
				{
					const FVector2D Facing(FMath::Cos(FMath::DegreesToRadians(Heading)), FMath::Sin(FMath::DegreesToRadians(Heading)));
					auto FootprintAt = [&](float Distance)
					{
						const FVector2D Center = Box.GetCenter() + Direction * Distance;
						return RTSHullClearance::MakeFootprint(Shape, FVector(Center, 0.f), Facing);
					};
					for (const float Gap : Gaps)
					{
						// Out along Direction until the hull stands Gap from the box.
						float Near = 0.f;
						float Far = 5000.f;
						for (int32 Step = 0; Step < 40; ++Step)
						{
							const float Middle = 0.5f * (Near + Far);
							(RTSTests::GetHullGap(FootprintAt(Middle), Box) < Gap ? Near : Far) = Middle;
						}
						const FRTSFootprint Footprint = FootprintAt(Far);
						const float TrueGap = RTSTests::GetHullGap(Footprint, Box);
						// A small hull cannot reach a deep overlap with a thin wall: it fits inside it.
						if (!FMath::IsNearlyEqual(TrueGap, Gap, 1.f))
						{
							continue;
						}
						const float Clearance = RTSHullClearance::GetClearance(*NavMesh, Footprint);
						++Checked;
						const bool bMissed = TrueGap < -OverlapTolerance && Clearance >= 0.f;
						const bool bWasted = TrueGap >= StopGap + CornerAllowance && Clearance < 0.f;
						if (bMissed)
						{
							++Missed;
							WorstMiss = FMath::Min(WorstMiss, TrueGap);
						}
						if (Clearance < 0.f)
						{
							WidestFlagged = FMath::Max(WidestFlagged, TrueGap);
						}
						Wasted += bWasted ? 1 : 0;
						if ((bMissed || bWasted) && Missed + Wasted <= 30)
						{
							Test->AddInfo(FString::Printf(TEXT("%s at %s from %s facing %.0f: %.1f cm from it, clearance %.1f"),
								*Data->GetName(), *FVector2D(Footprint.Center).ToString(), *Name, Heading, TrueGap, Clearance));
						}
					}
				}
			}
		}
		Test->AddInfo(FString::Printf(TEXT("%s: deepest overlap not flagged %.1f cm, widest gap flagged %.1f cm (allowed below %.1f cm)"),
			*DescribeUnit(*Data), WorstMiss, WidestFlagged, StopGap + CornerAllowance));
	}
	Test->TestEqual(TEXT("Hulls overlapping an obstacle that the clearance misses"), Missed, 0);
	Test->TestEqual(TEXT("Hulls 50 cm plus the corner allowance or more from an obstacle that the clearance flags"), Wasted, 0);
	Test->AddInfo(FString::Printf(TEXT("%d hull positions checked"), Checked));
	return true;
}

/** CanPlace with a vehicle factory whose exit faces Block_North's west face, and a barracks on the same spot. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRTSNavCheckFactoryExit, FAutomationTestBase*, Test);
bool FRTSNavCheckFactoryExit::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	const URTSBuildingData* Factory = LoadObject<URTSBuildingData>(nullptr, VehicleFactoryPath);
	const URTSBuildingData* Barracks = LoadObject<URTSBuildingData>(nullptr, BarracksPath);
	if (!World || !Factory || !Barracks)
	{
		Test->AddError(TEXT("No PIE world or building data"));
		return true;
	}
	const URTSUnitData* Widest = RTSBuildPlacement::GetWidestExitUnit(*Factory);
	if (!Test->TestNotNull(TEXT("The vehicle factory sends units out"), Widest))
	{
		return true;
	}
	Test->AddInfo(FString::Printf(TEXT("The factory's widest unit: %s, spawned %.0f cm in front of its centre"), *DescribeUnit(*Widest),
		RTSBuildPlacement::GetExitDistance(*Factory, *Widest)));

	// Block_North's west face is at x = 600: a factory centred at x = 0 facing east puts its exit within the vehicle
	// navmesh's erosion of it, one at x = -1000 well clear.
	FVector Location;
	Test->TestFalse(TEXT("A vehicle factory whose exit faces a block too closely is refused"),
		RTSBuildPlacement::CanPlace(*World, *Factory, FVector(0.f, 2500.f, 0.f), Location));
	Test->TestTrue(TEXT("The same factory turned to face away from the block is accepted"),
		RTSBuildPlacement::CanPlace(*World, *Factory, FVector(0.f, 2500.f, 0.f), Location, nullptr, 2));
	Test->TestTrue(TEXT("A vehicle factory with room in front of its exit is accepted"),
		RTSBuildPlacement::CanPlace(*World, *Factory, FVector(-1000.f, 2500.f, 0.f), Location));
	Test->TestTrue(TEXT("A barracks, whose infantry fit, is accepted where the factory is refused"),
		RTSBuildPlacement::CanPlace(*World, *Barracks, FVector(0.f, 2500.f, 0.f), Location));
	return true;
}

}

/** Every unit walks the narrowest agent class at least as wide as its hull; a size nobody configured needs no code. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavAgentSelectionTest, "WorldDivide.Navigation.AgentSelection", RTSTests::TestFlags)

bool FRTSNavAgentSelectionTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TArray<FNavDataConfig>& Agents = RTSNavAgents::GetAgents();
	TestTrue(TEXT("At least an infantry and a vehicle class"), Agents.Num() >= 2);
	for (int32 Index = 1; Index < Agents.Num(); ++Index)
	{
		TestTrue(TEXT("Classes run from narrow to wide"), Agents[Index - 1].AgentRadius < Agents[Index].AgentRadius);
	}
	if (Agents.Num() < 2)
	{
		return false;
	}
	TestEqual(TEXT("The narrowest class is the infantry's"), Agents[0].Name, FName(TEXT("Infantry")));
	TestEqual(TEXT("Infantry radius"), Agents[0].AgentRadius, 40.f);
	TestEqual(TEXT("Infantry height"), Agents[0].AgentHeight, 180.f);
	const FNavDataConfig* Vehicle = Agents.FindByPredicate([](const FNavDataConfig& Agent) { return Agent.Name == TEXT("Vehicle"); });
	if (TestNotNull(TEXT("A vehicle class"), Vehicle))
	{
		TestEqual(TEXT("Vehicle radius"), Vehicle->AgentRadius, 190.f);
		TestEqual(TEXT("Vehicle height"), Vehicle->AgentHeight, 350.f);
	}

	// Any width: the class covers it, and no narrower class would.
	for (float HalfWidth = 5.f; HalfWidth <= Agents.Last().AgentRadius; HalfWidth += 5.f)
	{
		const FNavDataConfig& Agent = RTSNavAgents::SelectAgent(HalfWidth);
		const int32 Index = Agents.IndexOfByPredicate([&Agent](const FNavDataConfig& Other) { return Other.Name == Agent.Name; });
		TestTrue(*FString::Printf(TEXT("A hull %.0f cm wide fits its class"), 2.f * HalfWidth), Agent.AgentRadius >= HalfWidth);
		TestTrue(*FString::Printf(TEXT("A hull %.0f cm wide takes the narrowest class that fits"), 2.f * HalfWidth),
			Index == 0 || Agents[Index - 1].AgentRadius < HalfWidth);
	}

	const URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
	const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
	const URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
	const TStrongObjectPtr<URTSUnitData> LongVehicle(MakeBoxUnit(TEXT("NavTestLongVehicle"), LongVehicleSize, 450.f, 90.f));
	const TStrongObjectPtr<URTSUnitData> NarrowVehicle(MakeBoxUnit(TEXT("NavTestNarrowVehicle"), NarrowVehicleSize, 500.f, 180.f));
	if (!TestNotNull(TEXT("M20 data"), M20) || !TestNotNull(TEXT("Tank data"), Tank) || !TestNotNull(TEXT("Infantry data"), Infantry))
	{
		return false;
	}
	TestEqual(TEXT("Infantry walks the infantry navmesh"), RTSNavAgents::SelectAgent(*Infantry).Name, FName(TEXT("Infantry")));
	TestEqual(TEXT("The narrow vehicle walks the infantry navmesh"), RTSNavAgents::SelectAgent(*NarrowVehicle).Name, FName(TEXT("Infantry")));
	TestEqual(TEXT("The M20 walks the vehicle navmesh"), RTSNavAgents::SelectAgent(*M20).Name, FName(TEXT("Vehicle")));
	TestEqual(TEXT("The placeholder tank walks the vehicle navmesh"), RTSNavAgents::SelectAgent(*Tank).Name, FName(TEXT("Vehicle")));
	TestEqual(TEXT("The long vehicle walks the vehicle navmesh"), RTSNavAgents::SelectAgent(*LongVehicle).Name, FName(TEXT("Vehicle")));
	for (const URTSUnitData* Data : TArray<const URTSUnitData*>{ Infantry, Tank, M20, LongVehicle.Get(), NarrowVehicle.Get() })
	{
		AddInfo(DescribeUnit(*Data));
	}

	// A spawned unit paths as its class, not as the engine would size it from its collision box.
	RTSTests::FRTSTestWorld TestWorld;
	for (URTSUnitData* Data : { const_cast<URTSUnitData*>(Infantry), NarrowVehicle.Get(), const_cast<URTSUnitData*>(M20), LongVehicle.Get() })
	{
		const ARTSUnit* Unit = TestWorld.SpawnUnit(Data, 0, FVector::ZeroVector);
		const FNavAgentProperties& Props = Unit->GetNavAgentPropertiesRef();
		const FNavDataConfig& Agent = RTSNavAgents::SelectAgent(*Data);
		TestEqual(*FString::Printf(TEXT("%s paths with its class's radius"), *Data->GetName()), Props.AgentRadius, Agent.AgentRadius);
		TestEqual(*FString::Printf(TEXT("%s paths with its class's height"), *Data->GetName()), Props.AgentHeight, Agent.AgentHeight);
	}
	return true;
}

/** Infantry and a narrow vehicle take a 3 m gap that the M20 and the tank go around; vehicles take a 6 m gap, all at once too. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavVehicleAgentTest, "WorldDivide.Navigation.VehicleAgent", RTSTests::TestFlags)

bool FRTSNavVehicleAgentTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!State->LoadUnits(*this))
	{
		return false;
	}
	for (const float Y : NarrowGapPlantYs)
	{
		State->PlacedSpots.Add(FVector(0.f, Y, 0.f));
	}
	for (const float Y : WideGapPlantYs)
	{
		State->PlacedSpots.Add(FVector(0.f, Y, 0.f));
	}

	const float NarrowY = 0.5f * (NarrowGapMinY + NarrowGapMaxY);
	const float WideY = 0.5f * (WideGapMinY + WideGapMaxY);
	auto Gate = [&](const FString& Name, TArray<FUnitStart> Units, float Y, EGate Expected, float MinY, float MaxY)
	{
		FDrive Drive;
		Drive.Name = Name;
		Drive.Units = MoveTemp(Units);
		Drive.Target = FVector(GateTargetX, Y, 0.f);
		Drive.Gate = Expected;
		Drive.GateMinY = MinY;
		Drive.GateMaxY = MaxY;
		State->Drives.Add(Drive);
	};
	const FVector NarrowStart(GateStartX, NarrowY, 0.f);
	const FVector WideStart(GateStartX, WideY, 0.f);
	Gate(TEXT("3 m gap"), { { State->Infantry, NarrowStart, 0.f } }, NarrowY, EGate::Through, NarrowGapMinY, NarrowGapMaxY);
	Gate(TEXT("3 m gap"), { { State->NarrowVehicle, NarrowStart, 0.f } }, NarrowY, EGate::Through, NarrowGapMinY, NarrowGapMaxY);
	Gate(TEXT("3 m gap"), { { State->M20, NarrowStart, 0.f } }, NarrowY, EGate::Around, NarrowGapMinY, NarrowGapMaxY);
	Gate(TEXT("3 m gap"), { { State->Tank, NarrowStart, 0.f } }, NarrowY, EGate::Around, NarrowGapMinY, NarrowGapMaxY);
	Gate(TEXT("6 m gap"), { { State->M20, WideStart, 0.f } }, WideY, EGate::Through, WideGapMinY, WideGapMaxY);
	Gate(TEXT("6 m gap"), { { State->Tank, WideStart, 0.f } }, WideY, EGate::Through, WideGapMinY, WideGapMaxY);
	Gate(TEXT("6 m gap"), { { State->LongVehicle, WideStart, 0.f } }, WideY, EGate::Through, WideGapMinY, WideGapMaxY);
	// Every size at once, each on its own order to its own spot beyond the gap, as a formation would send them.
	Gate(TEXT("6 m gap, mixed"), {
		{ State->M20, WideStart, 0.f, FVector(0.f, 0.f, 0.f) },
		{ State->Tank, WideStart + FVector(-900.f, 400.f, 0.f), 0.f, FVector(-800.f, 400.f, 0.f) },
		{ State->LongVehicle, WideStart + FVector(-1500.f, -300.f, 0.f), 0.f, FVector(-1500.f, -300.f, 0.f) },
		{ State->NarrowVehicle, WideStart + FVector(-900.f, -400.f, 0.f), 0.f, FVector(800.f, -400.f, 0.f) },
		{ State->Infantry, WideStart + FVector(-2100.f, 200.f, 0.f), 0.f, FVector(800.f, 300.f, 0.f) },
	}, WideY, EGate::Through, WideGapMinY, WideGapMaxY);

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavPlaceBuildings(this, State, PowerPlantPath));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForHoles(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/** The clearance flags every overlapping hull and no hull standing 50 cm or more from an obstacle, for every test hull. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavHullClearanceMeasureTest, "WorldDivide.Navigation.HullClearance.Measure", RTSTests::TestFlags)

bool FRTSNavHullClearanceMeasureTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavMeasureClearance(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/**
 * The M20, the placeholder tank, the long vehicle and the narrow one each drive head on into, along and around a corner of
 * Block_North, Wall_West and a vehicle factory: their hulls never overlap them, and stop within 50 cm of the ones they
 * were ordered into.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavHullClearanceTest, "WorldDivide.Navigation.HullClearance.Drive", RTSTests::TestFlags)

bool FRTSNavHullClearanceTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!State->LoadUnits(*this))
	{
		return false;
	}
	State->PlacedSpots.Add(FactorySpot);

	// Block_North spans x 600 to 1400 and y 2100 to 2900, Wall_West x -2600 to -2400 and y -1000 to 1000, the factory
	// 4 x 4 cells around FactorySpot. Each is far enough from the others for three units to drive at once, one at each, and
	// four rounds take every unit to every obstacle.
	struct FCourse
	{
		FVector Start;
		float Yaw;
		FVector Target;
	};
	const FCourse HeadOn[] = {
		{ FVector(1000.f, 4500.f, 0.f), -90.f, FVector(1000.f, 2500.f, 0.f) },
		{ FVector(-900.f, 0.f, 0.f), 180.f, FVector(-2500.f, 0.f, 0.f) },
		{ FactorySpot + FVector(0.f, 1700.f, 0.f), -90.f, FactorySpot },
	};
	const FCourse Along[] = {
		{ FVector(-600.f, 3150.f, 0.f), 0.f, FVector(2600.f, 3150.f, 0.f) },
		{ FVector(-2150.f, -1600.f, 0.f), 90.f, FVector(-2150.f, 1600.f, 0.f) },
		{ FactorySpot + FVector(-1300.f, 450.f, 0.f), 0.f, FactorySpot + FVector(1300.f, 450.f, 0.f) },
	};
	const FCourse Around[] = {
		{ FVector(200.f, 3500.f, 0.f), 0.f, FVector(1900.f, 1600.f, 0.f) },
		{ FVector(-1800.f, 600.f, 0.f), 90.f, FVector(-3300.f, 600.f, 0.f) },
		{ FactorySpot + FVector(-800.f, 700.f, 0.f), 0.f, FactorySpot + FVector(700.f, -900.f, 0.f) },
	};
	URTSUnitData* const Hulls[] = { State->M20, State->Tank, State->LongVehicle, State->NarrowVehicle };
	auto AddRounds = [&](const FString& Name, const FCourse (&Courses)[3], bool bStopAtObstacle)
	{
		for (int32 Round = 0; Round < UE_ARRAY_COUNT(Hulls); ++Round)
		{
			FDrive Drive;
			Drive.Name = FString::Printf(TEXT("%s, round %d"), *Name, Round + 1);
			Drive.bStopAtObstacle = bStopAtObstacle;
			for (int32 Course = 0; Course < UE_ARRAY_COUNT(Courses); ++Course)
			{
				// Each target on its own: the drive's target stays at the origin.
				const FCourse& Picked = Courses[Course];
				Drive.Units.Add({ Hulls[(Round + Course) % UE_ARRAY_COUNT(Hulls)], Picked.Start, Picked.Yaw, Picked.Target });
			}
			State->Drives.Add(Drive);
		}
	};
	AddRounds(TEXT("Head on into Block_North, Wall_West and the factory"), HeadOn, true);
	AddRounds(TEXT("Along Block_North, Wall_West and the factory"), Along, false);
	AddRounds(TEXT("Around Block_North, Wall_West and the factory"), Around, false);

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavPlaceBuildings(this, State, VehicleFactoryPath));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForHoles(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/** A production building is refused where its vehicles could not leave its exit; an infantry building on the same spot is not. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavFactoryExitTest, "WorldDivide.Navigation.FactoryExit", RTSTests::TestFlags)

bool FRTSNavFactoryExitTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavCheckFactoryExit(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
