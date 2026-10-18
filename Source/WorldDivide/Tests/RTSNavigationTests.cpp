#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSWeaponComponent.h"
#include "AIController.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSHullClearance.h"
#include "Navigation/RTSMoveDestination.h"
#include "Navigation/RTSMoveRequestSubsystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/RTSNavAgents.h"
#include "Navigation/RTSPathSearch.h"
#include "NavigationSystem.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestGeometry.h"
#include "Tests/RTSTestPIE.h"
#include "Tests/RTSTestScale.h"
#include "Tests/RTSTestWorld.h"
#include "UObject/StrongObjectPtr.h"
#include "Units/RTSBarrelClearance.h"
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

	/** The placeholders whose hulls the long vehicle must outgrow. */
	const TCHAR* PlaceholderPaths[] = {
		TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank"),
		TEXT("/Game/Data/Units/DA_TestAbrams.DA_TestAbrams"),
		TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"),
		TEXT("/Game/Data/Units/DA_TestHarvester.DA_TestHarvester"),
		TEXT("/Game/Data/Units/DA_TestFlakTruck.DA_TestFlakTruck"),
		TEXT("/Game/Data/Units/DA_TestBuilder.DA_TestBuilder"),
	};

	/**
	 * Hulls the agents were not tuned for: length, width and height in cm. The long vehicle is a tenth longer than the longest
	 * placeholder and narrower than any vehicle, still too wide for the infantry navmesh.
	 */
	FVector GetLongVehicleSize()
	{
		float Longest = 0.f;
		for (const TCHAR* Path : PlaceholderPaths)
		{
			if (const URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Path))
			{
				Longest = FMath::Max(Longest, RTSTests::GetHullSize(*Data).X);
			}
		}
		return FVector(FMath::RoundToFloat(1.1f * Longest), 160.f, 250.f);
	}
	const FVector NarrowVehicleSize(450.f, 70.f, 150.f);

	/** A hull may touch an obstacle by this much at most: the hull box against the obstacle's collision box. */
	constexpr float OverlapTolerance = 2.f;
	/** A hull ordered into an obstacle stops at most this far from it. */
	constexpr float StopGap = 50.f;

	/** Faster than this along or against its heading (cm/s) a unit drives forward or reverses; a switch counts as a reversal. */
	constexpr float ReversalSpeed = 30.f;
	/** A unit may back off and set off again (UpdateBlockedMove) but not keep switching. */
	constexpr int32 MaxReversals = 2;
	/** A unit whose order is refused stays within this of where it stood. */
	constexpr float RefusedDrift = 50.f;

	/** Game seconds a drive may take before it counts as stuck, and the time it stands once every order is done. */
	constexpr double DriveTimeout = 40.0;
	constexpr double SettleSeconds = 0.5;

	/**
	 * Where the tests put things on L_TestMap, from the map and the game's data (InitLayout, called once the map is open), so
	 * it moves with the scale (#136). Offsets around obstacles are in lengths of the long vehicle, the longest hull driven.
	 */
	FVector LongVehicleSize = FVector::ZeroVector;

	/**
	 * The gap test's walls of four power plants on either side of a gap, along x = 0: on the strip north of the bases one
	 * with a 3 m gap, on the strip south of them one with a 6 m gap. Going around either end is far longer than going through.
	 */
	constexpr float NarrowGapWidth = 300.f;
	constexpr float WideGapWidth = 600.f;
	float NarrowGapPlantYs[8];
	float NarrowGapMinY = 0.f;
	float NarrowGapMaxY = 0.f;
	float WideGapPlantYs[8];
	float WideGapMinY = 0.f;
	float WideGapMaxY = 0.f;
	float GateStartX = 0.f;
	float GateTargetX = 0.f;
	/** A unit counts as arrived this close to its target; units sent together to one point crowd around it: a long hull. */
	float ArrivalRadius = 0.f;

	/** The vehicle factory the hull test drives at, where rts.Debug.UnitShots puts one, its exit facing east. */
	const FVector FactorySpot(-6000.f, -19500.f, 0.f);

	/** The map's obstacles the hull test drives at, seen from above. */
	FBox2D BlockBox(ForceInit);
	FBox2D WallBox(ForceInit);

	/** Plants side by side along x = 0 around a gap of Width centred near CenterY, from south to north, and the gap's edges. */
	void LayGapWall(float CenterY, float Width, float PlantHalf, float (&OutYs)[8], float& OutMinY, float& OutMaxY)
	{
		// Even footprints centre on a grid line: the first plant south of the gap is snapped, the rest follow at a plant's pitch.
		const float South = FMath::RoundToFloat((CenterY - 0.5f * Width - PlantHalf) / RTSBuildPlacement::GridSize) * RTSBuildPlacement::GridSize;
		const float North = South + 2.f * PlantHalf + Width;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutYs[3 - Index] = South - Index * 2.f * PlantHalf;
			OutYs[4 + Index] = North + Index * 2.f * PlantHalf;
		}
		OutMinY = South + PlantHalf;
		OutMaxY = North - PlantHalf;
	}

	/** Lays the spots out from the map in World and the test data. False, with the reason in OutError, when something is missing. */
	bool InitLayout(const UWorld& World, FString& OutError)
	{
		const URTSBuildingData* Plant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		const float HalfSize = RTSTests::GetMapHalfSize(World);
		BlockBox = RTSTests::GetLabelledBox(World, TEXT("Block_North"));
		WallBox = RTSTests::GetLabelledBox(World, TEXT("Wall_West"));
		LongVehicleSize = GetLongVehicleSize();
		if (!Plant || HalfSize <= 0.f || !BlockBox.bIsValid || !WallBox.bIsValid || LongVehicleSize.X <= 0.f)
		{
			OutError = TEXT("Missing test data, or L_TestMap lacks its NavMeshBoundsVolume, Block_North or Wall_West");
			return false;
		}
		const float PlantHalf = RTSTests::GetFootprintExtent(*Plant).Y;
		// The strips north and south of the bases: midway between Block_North (or Block_South) and the ground's edge.
		const float StripY = 0.5f * (BlockBox.Max.Y + HalfSize);
		LayGapWall(StripY, NarrowGapWidth, PlantHalf, NarrowGapPlantYs, NarrowGapMinY, NarrowGapMaxY);
		LayGapWall(-StripY, WideGapWidth, PlantHalf, WideGapPlantYs, WideGapMinY, WideGapMaxY);
		GateStartX = -(RTSTests::GetFootprintExtent(*Plant).X + 2.f * LongVehicleSize.X);
		GateTargetX = -GateStartX;
		ArrivalRadius = LongVehicleSize.X;
		return true;
	}

	/** InitLayout on the editor's copy of the map AutomationOpenMap opened, before the play session starts. */
	bool InitLayoutFromEditor(FAutomationTestBase& Test)
	{
		const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		FString Error = TEXT("No editor world");
		if (!World || !InitLayout(*World, Error))
		{
			Test.AddError(Error);
			return false;
		}
		return true;
	}

	/** A placeholder hull for a size no unit has: an engine cube stretched to Size, on tracks (#138) as the tanks are. */
	URTSUnitData* MakeBoxUnit(const TCHAR* Name, const FVector& Size, float Speed, float TurnRate)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), URTSUnitData::StaticClass(), Name));
		Data->Mesh = LoadObject<UStaticMesh>(nullptr, CubePath);
		Data->MeshScale = Size / 100.f;
		Data->SelectionRadius = 0.5f * Size.Y;
		Data->MaxHealth = 300.f;
		Data->MoveSpeed = Speed;
		Data->TurnRate = TurnRate;
		Data->Locomotion.Type = ERTSLocomotionType::Tracked;
		Data->Locomotion.TrackGauge = 0.75f * Size.Y;
		Data->Locomotion.ReverseSpeed = Speed / 3.f;
		Data->Locomotion.Acceleration = 300.f;
		Data->Locomotion.Braking = 900.f;
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
		/** It must stop on the side of its target it started on, not drive round to another face. */
		bool bOwnSide = false;
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

		/** Move or AttackMove. */
		ERTSCommandType Order = ERTSCommandType::Move;
		/** One order for every unit, as for a selected group, spread into a formation; each unit must end nearer the target. */
		bool bGroupOrder = false;
		/** Shift-queued after the first order, in turn; each unit must end at the last one. */
		TArray<FVector> Queued;
		/** Every order is refused: the units stay where they stand. */
		bool bRefused = false;
		/** Refusals the player must receive during the drive, one for a group refused at once (OrderRefusalCooldown). */
		int32 ExpectedRefusals = 0;
		/** Check that no unit keeps switching between driving forward and reversing. */
		bool bCheckReversals = false;
		/** With Queued: each unit must come within StopGap of an obstacle on the way, as one of the legs leads into it. */
		bool bQueuedIntoObstacle = false;
		/** Game seconds before the drive counts as stuck. */
		double Timeout = DriveTimeout;
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
		TArray<FVector> StartLocations;
		/** How often each unit switched between driving forward and reversing, and the way it drove last. */
		TArray<int32> Reversals;
		TArray<int32> LastDirections;
		int32 RefusalsAtStart = 0;
		/** Each unit's deepest barrel reach into an obstacle, checked for units with a barrel when bCheckBarrels is set. */
		bool bCheckBarrels = false;
		TArray<float> MaxBarrelDepths;
		TArray<FString> MaxBarrelObstacles;

		/** The obstacles' boxes seen from above: the map's walls and blocks and every building; and as solids, roof included. */
		TArray<FBox2D> Obstacles;
		TArray<FBox> ObstacleSolids;
		TArray<FString> ObstacleNames;

		bool LoadUnits(FAutomationTestBase& Test)
		{
			M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
			Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
			Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
			LongVehicle = MakeBoxUnit(TEXT("NavTestLongVehicle"), GetLongVehicleSize(), 450.f, 90.f);
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
		State.ObstacleSolids.Reset();
		State.ObstacleNames.Reset();
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			const FString Label = It->GetActorLabel();
			if (Label.StartsWith(TEXT("Wall_")) || Label.StartsWith(TEXT("Block_")))
			{
				const FBox Box = It->GetComponentsBoundingBox();
				State.Obstacles.Add(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)));
				State.ObstacleSolids.Add(Box);
				State.ObstacleNames.Add(Label);
			}
		}
		for (TActorIterator<ARTSBuilding> It(&World); It; ++It)
		{
			const FVector Extent = RTSBuildPlacement::GetFootprintExtent(It->GetFootprint());
			const FVector2D Center(It->GetActorLocation());
			State.Obstacles.Add(FBox2D(Center - FVector2D(Extent), Center + FVector2D(Extent)));
			// Its collision box: the footprint up to the roof.
			State.ObstacleSolids.Add(It->GetComponentsBoundingBox(/*bNonColliding*/ false));
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

	/** How deep Unit's barrel reaches into any obstacle, and which one; zero without a barrel or when it stays out. */
	float GetBarrelDepth(const ARTSUnit& Unit, const FNavState& State, FString* OutObstacle = nullptr)
	{
		FVector Start;
		FVector End;
		if (!Unit.GetShape().Barrel || !Unit.GetBarrelSegment(Start, End))
		{
			return 0.f;
		}
		float Deepest = 0.f;
		for (int32 Index = 0; Index < State.ObstacleSolids.Num(); ++Index)
		{
			const float Depth = RTSBarrelClearance::GetPenetration(Start, End, Unit.GetShape().Barrel->Radius, State.ObstacleSolids[Index]);
			if (Depth > Deepest)
			{
				Deepest = Depth;
				if (OutObstacle)
				{
					*OutObstacle = State.ObstacleNames[Index];
				}
			}
		}
		return Deepest;
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
			const float Forward = FVector::DotProduct(Unit->GetVelocity(), Unit->GetActorForwardVector());
			const int32 Direction = Forward > ReversalSpeed ? 1 : Forward < -ReversalSpeed ? -1 : 0;
			if (Direction != 0)
			{
				State->Reversals[Index] += State->LastDirections[Index] == -Direction ? 1 : 0;
				State->LastDirections[Index] = Direction;
			}
			FString Obstacle;
			const float Gap = GetObstacleGap(*Unit, *State, &Obstacle);
			if (Gap < State->MinGaps[Index])
			{
				State->MinGaps[Index] = Gap;
				State->MinGapObstacles[Index] = Obstacle;
			}
			if (State->bCheckBarrels)
			{
				const float Depth = GetBarrelDepth(*Unit, *State, &Obstacle);
				if (Depth > State->MaxBarrelDepths[Index])
				{
					State->MaxBarrelDepths[Index] = Depth;
					State->MaxBarrelObstacles[Index] = Obstacle;
				}
			}
			bDone &= Unit->GetQueuedCommandCount() == 0;
		}
		if (bDone && State->SettleStart < 0.0)
		{
			State->SettleStart = Now;
		}
		const bool bTimedOut = Now - State->DriveStart > Drive.Timeout;
		// After a refusal the next drive waits out the cooldown, so its own refusals reach the player.
		const double Settle = Drive.ExpectedRefusals > 0 ? FMath::Max(SettleSeconds, double(ARTSPlayerController::OrderRefusalCooldown)) : SettleSeconds;
		if (!bTimedOut && (State->SettleStart < 0.0 || Now - State->SettleStart < Settle))
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
			if (State->bCheckBarrels && Unit->GetShape().Barrel)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s never reached its barrel into an obstacle (deepest %.1f cm, %s)"), *What,
					State->MaxBarrelDepths[Index], *State->MaxBarrelObstacles[Index]), State->MaxBarrelDepths[Index] <= OverlapTolerance);
				Test->AddInfo(FString::Printf(TEXT("%s: barrel at most %.1f cm into %s, turret ended at %.0f degrees, gun at %.0f"), *What,
					State->MaxBarrelDepths[Index], *State->MaxBarrelObstacles[Index], Unit->GetTurretYaw(), Unit->GetGunPitch()));
			}
			const float FinalGap = GetObstacleGap(*Unit, *State);
			const FVector Final = Unit->GetActorLocation();
			const FVector& Start = State->StartLocations[Index];
			const FVector Target = Drive.Queued.IsEmpty() ? Drive.Target + Drive.Units[Index].TargetOffset : Drive.Queued.Last();
			if (Drive.bCheckReversals)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s did not keep switching between forward and reverse (%d switches)"), *What,
					State->Reversals[Index]), State->Reversals[Index] <= MaxReversals);
			}
			if (Drive.Units[Index].bOwnSide)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s stayed on its own side of the target"), *What),
					FVector::DotProduct((Final - Target).GetSafeNormal2D(), (Start - Target).GetSafeNormal2D()) > 0.5f);
			}
			if (Drive.bQueuedIntoObstacle)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s drove up to the obstacle on its way (closest %.1f cm)"), *What, State->MinGaps[Index]),
					State->MinGaps[Index] <= StopGap);
			}
			if (Drive.bRefused)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s stood still (%.0f cm)"), *What, FVector::Dist2D(Final, Start)),
					FVector::Dist2D(Final, Start) <= RefusedDrift);
			}
			else if (Drive.bGroupOrder)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s closed in on the target (%.0f cm, from %.0f cm)"), *What, FVector::Dist2D(Final, Target),
					FVector::Dist2D(Start, Target)), FVector::Dist2D(Final, Target) < FVector::Dist2D(Start, Target) - ArrivalRadius);
			}
			else if (Drive.bStopAtObstacle)
			{
				Test->TestTrue(*FString::Printf(TEXT("%s stopped within %.0f cm of the obstacle (%.1f cm)"), *What, StopGap, FinalGap),
					FinalGap >= -OverlapTolerance && FinalGap <= StopGap);
			}
			else
			{
				Test->TestTrue(*FString::Printf(TEXT("%s arrived (%.0f cm from the target)"), *What, FVector::Dist2D(Final, Target)),
					FVector::Dist2D(Final, Target) <= ArrivalRadius);
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
			Test->AddInfo(FString::Printf(TEXT("%s: closest %.1f cm to %s, stopped at %s %.1f cm from an obstacle, crossed x = 0 at %s, %.1f s"),
				*What, State->MinGaps[Index], *State->MinGapObstacles[Index], *FVector2D(Final).ToString(), FinalGap, *Crossed, Now - State->DriveStart));
			Unit->Destroy();
		}
		State->Units.Reset();
		Test->TestEqual(*FString::Printf(TEXT("%s: refusals the player received"), *Drive.Name),
			Controller->GetOrderRefusalCount() - State->RefusalsAtStart, Drive.ExpectedRefusals);
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
	State->MaxBarrelDepths.Reset();
	State->MaxBarrelObstacles.Reset();
	State->StartLocations.Reset();
	State->Reversals.Reset();
	State->LastDirections.Reset();
	for (const FUnitStart& Start : Next.Units)
	{
		ARTSUnit* Unit = SpawnUnit(World, Start.Data, Controller->GetTeamId(), Start.Location, Start.Yaw);
		State->Units.Add(Unit);
		State->LastLocations.Add(Unit->GetActorLocation());
		State->CrossingYs.AddDefaulted();
		State->MinGaps.Add(FLT_MAX);
		State->MinGapObstacles.AddDefaulted();
		State->MaxBarrelDepths.Add(0.f);
		State->MaxBarrelObstacles.AddDefaulted();
		State->StartLocations.Add(Unit->GetActorLocation());
		State->Reversals.Add(0);
		State->LastDirections.Add(0);
	}
	State->RefusalsAtStart = Controller->GetOrderRefusalCount();
	TArray<ARTSUnit*> Group;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : State->Units)
	{
		Group.Add(Unit.Get());
	}
	for (int32 Index = 0; Index < (Next.bGroupOrder ? 1 : State->Units.Num()); ++Index)
	{
		const TArray<ARTSUnit*> Ordered = Next.bGroupOrder ? Group : TArray<ARTSUnit*>{ Group[Index] };
		FRTSCommand Move;
		Move.Type = Next.Order;
		Move.TargetLocation = Next.Target + (Next.bGroupOrder ? FVector::ZeroVector : Next.Units[Index].TargetOffset);
		Controller->Server_IssueCommand(Ordered, Move);
		for (const FVector& Queued : Next.Queued)
		{
			Move.TargetLocation = Queued;
			Move.bQueue = true;
			Controller->Server_IssueCommand(Ordered, Move);
		}
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

/**
 * Where each agent's navmesh edge lies off each face of Block_North, Wall_West and the placed vehicle factory, measured
 * from the collision face (#163): never nearer than the erosion RTSHullClearance assumes, and at most a cell further, which
 * is how far Recast's voxels may round the obstacle out.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavMeasureEdgeOffsets, FAutomationTestBase*, Test, TSharedRef<FNavState>, State);
bool FRTSNavMeasureEdgeOffsets::Update()
{
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	if (!World)
	{
		Test->AddError(TEXT("No PIE world"));
		return true;
	}
	GatherObstacles(*World, *State);

	const FVector2D Faces[] = { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f) };
	const TCHAR* FaceNames[] = { TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y") };
	int32 Measured = 0;
	int32 Expected = 0;
	for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
	{
		const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*World, Agent);
		if (!Test->TestNotNull(*FString::Printf(TEXT("%s navmesh"), *Agent.Name.ToString()), NavMesh))
		{
			continue;
		}
		const float Erosion = RTSHullClearance::GetErosion(*NavMesh);
		const float Cell = NavMesh->GetCellSize(ENavigationDataResolution::Default);
		for (int32 ObstacleIndex = 0; ObstacleIndex < State->Obstacles.Num(); ++ObstacleIndex)
		{
			const FString& Name = State->ObstacleNames[ObstacleIndex];
			if ((Name.StartsWith(TEXT("Wall_")) && Name != TEXT("Wall_West")) || (Name.StartsWith(TEXT("Block_")) && Name != TEXT("Block_North")))
			{
				continue;
			}
			const FBox2D& Box = State->Obstacles[ObstacleIndex];
			for (int32 FaceIndex = 0; FaceIndex < UE_ARRAY_COUNT(Faces); ++FaceIndex)
			{
				++Expected;
				const FVector2D Normal = Faces[FaceIndex];
				const FVector2D Face = Box.GetCenter() + Normal * Box.GetExtent();
				FNavLocation Outside;
				if (!Test->TestTrue(*FString::Printf(TEXT("Navmesh off %s's %s face"), *Name, FaceNames[FaceIndex]),
					NavMesh->ProjectPoint(FVector(Face + Normal * (Erosion + 400.f), 0.f), Outside, FVector(10.f, 10.f, 250.f))))
				{
					continue;
				}
				FVector Hit;
				const FVector Into(Face - Normal * 1.f, Outside.Location.Z);
				if (!Test->TestTrue(*FString::Printf(TEXT("%s's navmesh ends before %s's %s face"), *Agent.Name.ToString(), *Name,
					FaceNames[FaceIndex]), ARecastNavMesh::NavMeshRaycast(NavMesh, Outside.Location, Into, Hit, NavMesh->GetDefaultQueryFilter())))
				{
					continue;
				}
				const float Offset = FVector2D::DotProduct(FVector2D(Hit) - Face, Normal);
				++Measured;
				Test->AddInfo(FString::Printf(TEXT("%s navmesh: edge %.1f cm off %s's %s face (%.1f past the erosion; face at %.1f, %.2f cells into the grid)"),
					*Agent.Name.ToString(), Offset, *Name, FaceNames[FaceIndex], Offset - Erosion,
					FVector2D::DotProduct(Face, Normal), FMath::Fmod(FMath::Abs(FVector2D::DotProduct(Face, Normal)), Cell) / Cell));
				Test->TestTrue(*FString::Printf(TEXT("%s's navmesh never comes nearer %s's %s face than its erosion (%.1f cm)"),
					*Agent.Name.ToString(), *Name, FaceNames[FaceIndex], Offset), Offset >= Erosion - 1.f);
				Test->TestTrue(*FString::Printf(TEXT("%s's navmesh ends within a cell past the erosion off %s's %s face (%.1f cm)"),
					*Agent.Name.ToString(), *Name, FaceNames[FaceIndex], Offset), Offset <= Erosion + Cell + 1.f);
			}
		}
	}
	Test->TestEqual(TEXT("Every face of every obstacle measured on every navmesh"), Measured, Expected);
	Test->TestTrue(TEXT("Block_North, Wall_West and the vehicle factory among them"), Expected >= 12 * RTSNavAgents::GetAgents().Num());
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

	// Facing Block_North's west face: a factory whose exit lane reaches half way into the block is refused, one a lane and
	// the vehicle navmesh's erosion further west is accepted.
	const float FactoryHalf = RTSBuildPlacement::GetFootprintExtent(Factory->Footprint).X;
	const float ExitLane = RTSBuildPlacement::GetExitLaneLength(*Factory);
	const FVector Blocked(BlockBox.Min.X - FactoryHalf - 0.5f * ExitLane, BlockBox.GetCenter().Y, 0.f);
	const FVector Clear(BlockBox.Min.X - FactoryHalf - ExitLane - 2.f * RTSNavAgents::GetAgents().Last().AgentRadius - 1000.f,
		BlockBox.GetCenter().Y, 0.f);
	FVector Location;
	Test->TestFalse(TEXT("A vehicle factory whose exit faces a block too closely is refused"),
		RTSBuildPlacement::CanPlace(*World, *Factory, Blocked, Location));
	Test->TestTrue(TEXT("The same factory turned to face away from the block is accepted"),
		RTSBuildPlacement::CanPlace(*World, *Factory, Blocked, Location, nullptr, 2));
	Test->TestTrue(TEXT("A vehicle factory with room in front of its exit is accepted"),
		RTSBuildPlacement::CanPlace(*World, *Factory, Clear, Location));
	Test->TestTrue(TEXT("A barracks, whose infantry fit, is accepted where the factory is refused"),
		RTSBuildPlacement::CanPlace(*World, *Barracks, Blocked, Location));
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
	const TStrongObjectPtr<URTSUnitData> LongVehicle(MakeBoxUnit(TEXT("NavTestLongVehicle"), GetLongVehicleSize(), 450.f, 90.f));
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
	if (!InitLayoutFromEditor(*this) || !State->LoadUnits(*this))
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
	// Every size at once, each on its own order to its own spot beyond the gap, as a formation would send them; spread along
	// the drive in lengths of the long vehicle (L).
	const float L = LongVehicleSize.X / 1000.f;
	Gate(TEXT("6 m gap, mixed"), {
		{ State->M20, WideStart, 0.f, FVector(0.f, 0.f, 0.f) },
		{ State->Tank, WideStart + FVector(-900.f * L, 400.f, 0.f), 0.f, FVector(-800.f * L, 400.f, 0.f) },
		{ State->LongVehicle, WideStart + FVector(-1500.f * L, -300.f, 0.f), 0.f, FVector(-1500.f * L, -300.f, 0.f) },
		{ State->NarrowVehicle, WideStart + FVector(-900.f * L, -400.f, 0.f), 0.f, FVector(800.f * L, -400.f, 0.f) },
		{ State->Infantry, WideStart + FVector(-2100.f * L, 200.f, 0.f), 0.f, FVector(800.f * L, 300.f, 0.f) },
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
 * How far each navmesh's edge lies off the collision faces of Block_North, Wall_West and a vehicle factory, on all four
 * sides (#163): RTSHullClearance relies on it never being nearer than the erosion, and the gap a hull stops at on how much
 * further it may be.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavEdgeOffsetsTest, "WorldDivide.Navigation.HullClearance.EdgeOffsets", RTSTests::TestFlags)

bool FRTSNavEdgeOffsetsTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	if (!AutomationOpenMap(TestMap) || !InitLayoutFromEditor(*this))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	State->PlacedSpots.Add(FactorySpot);
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavPlaceBuildings(this, State, VehicleFactoryPath));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForHoles(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavMeasureEdgeOffsets(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSNavigationTests
{
	/**
	 * Drives each of Hulls head on into, along and around a corner of Block_North, Wall_West and a vehicle factory, and
	 * checks each drive (FRTSNavRunDrives); with bCheckBarrels also that no barrel reaches into an obstacle.
	 */
	bool QueueObstacleDrives(FAutomationTestBase& Test, const TSharedRef<FNavState>& State, bool bCheckBarrels,
		TFunctionRef<TArray<URTSUnitData*>(const FNavState&)> GetHulls)
	{
		if (!AutomationOpenMap(TestMap))
		{
			Test.AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
			return false;
		}
		const URTSBuildingData* Factory = LoadObject<URTSBuildingData>(nullptr, VehicleFactoryPath);
		if (!InitLayoutFromEditor(Test) || !State->LoadUnits(Test) || !Test.TestNotNull(TEXT("Vehicle factory data"), Factory))
		{
			return false;
		}
		State->PlacedSpots.Add(FactorySpot);
		State->bCheckBarrels = bCheckBarrels;

		// Block_North, Wall_West and the factory around FactorySpot. Each is far enough from the others for three units to drive
		// at once, one at each, and as many rounds as hulls take every unit to every obstacle. Starts and targets lie off each
		// face by lengths of the long vehicle (L, per 1000 cm), as they did for the 10 m long vehicle of before the real scale.
		const float L = LongVehicleSize.X / 1000.f;
		const FVector2D B = BlockBox.GetCenter();
		const FVector2D BH = BlockBox.GetExtent();
		const FVector2D W = WallBox.GetCenter();
		const FVector2D WH = WallBox.GetExtent();
		const FVector2D FH(RTSBuildPlacement::GetFootprintExtent(Factory->Footprint));
		struct FCourse
		{
			FVector Start;
			float Yaw;
			FVector Target;
		};
		const FCourse HeadOn[] = {
			{ FVector(B.X, B.Y + BH.Y + 1600.f * L, 0.f), -90.f, FVector(B, 0.f) },
			{ FVector(W.X + WH.X + 1500.f * L, W.Y, 0.f), 180.f, FVector(W, 0.f) },
			{ FactorySpot + FVector(0.f, FH.Y + 1500.f * L, 0.f), -90.f, FactorySpot },
		};
		// The faces facing -X and -Y, where Recast rounds a static mesh out by a cell more (#163), and the rest.
		const FCourse HeadOnNegative[] = {
			{ FVector(B.X, B.Y - BH.Y - 1600.f * L, 0.f), 90.f, FVector(B, 0.f) },
			{ FVector(W.X - WH.X - 1500.f * L, W.Y, 0.f), 0.f, FVector(W, 0.f) },
			{ FactorySpot + FVector(-FH.X - 1500.f * L, 0.f, 0.f), 0.f, FactorySpot },
		};
		// Wall_West's ends are narrower than any vehicle hull, which meets both corners at once; each is clicked near its end,
		// so the hull drives into the end face (MoveIntoObstacle.Deep sends hulls onto the middle of the wall).
		const FCourse HeadOnEnds[] = {
			{ FVector(B.X - BH.X - 1600.f * L, B.Y, 0.f), 0.f, FVector(B, 0.f) },
			{ FVector(W.X + 0.5f * WH.X, W.Y - WH.Y - 1500.f * L, 0.f), 90.f, FVector(W.X + 0.5f * WH.X, W.Y - WH.Y + 2.f * WH.X, 0.f) },
			{ FactorySpot + FVector(0.f, -FH.Y - 1500.f * L, 0.f), 90.f, FactorySpot },
		};
		const FCourse HeadOnRest[] = {
			{ FVector(B.X + BH.X + 1600.f * L, B.Y, 0.f), 180.f, FVector(B, 0.f) },
			{ FVector(W.X, W.Y + WH.Y + 1500.f * L, 0.f), -90.f, FVector(W.X, W.Y + WH.Y - 2.f * WH.X, 0.f) },
			{ FactorySpot + FVector(FH.X + 1500.f * L, 0.f, 0.f), 180.f, FactorySpot },
		};
		const FCourse Along[] = {
			{ FVector(B.X - BH.X - 1200.f * L, B.Y + BH.Y + 250.f * L, 0.f), 0.f, FVector(B.X + BH.X + 1200.f * L, B.Y + BH.Y + 250.f * L, 0.f) },
			{ FVector(W.X + WH.X + 250.f * L, W.Y - WH.Y - 600.f * L, 0.f), 90.f, FVector(W.X + WH.X + 250.f * L, W.Y + WH.Y + 600.f * L, 0.f) },
			{ FactorySpot + FVector(-FH.X - 1100.f * L, FH.Y + 250.f * L, 0.f), 0.f, FactorySpot + FVector(FH.X + 1100.f * L, FH.Y + 250.f * L, 0.f) },
		};
		const FCourse Around[] = {
			{ FVector(B.X - BH.X - 400.f * L, B.Y + BH.Y + 600.f * L, 0.f), 0.f, FVector(B.X + BH.X + 500.f * L, B.Y - BH.Y - 500.f * L, 0.f) },
			{ FVector(W.X + WH.X + 600.f * L, W.Y + 0.6f * WH.Y, 0.f), 90.f, FVector(W.X - WH.X - 800.f * L, W.Y + 0.6f * WH.Y, 0.f) },
			{ FactorySpot + FVector(-FH.X - 600.f * L, FH.Y + 500.f * L, 0.f), 0.f, FactorySpot + FVector(FH.X + 500.f * L, -FH.Y - 700.f * L, 0.f) },
		};
		// Pivots beside a corner (#138, #163): each hull stands alongside a face near its end, its centre as far from the face
		// and past the corner as the long vehicle's half diagonal and a little, facing away from the corner, and is sent round
		// it behind itself. It turns on the spot there, its hull's corners sweeping past the obstacle's, and again at the
		// corner of its path.
		const float Pivot = 0.5f * FVector2D(LongVehicleSize.X, LongVehicleSize.Y).Size() + 60.f;
		const FCourse PivotCorners[] = {
			{ FVector(B.X - BH.X + Pivot, B.Y + BH.Y + Pivot, 0.f), 0.f, FVector(B.X - BH.X - 800.f * L, B.Y, 0.f) },
			{ FVector(W.X + WH.X + Pivot, W.Y - WH.Y + Pivot, 0.f), 90.f, FVector(W.X - WH.X - 800.f * L, W.Y - WH.Y - 500.f * L, 0.f) },
			{ FactorySpot + FVector(FH.X + Pivot, FH.Y - Pivot, 0.f), -90.f, FactorySpot + FVector(-0.5f * FH.X, FH.Y + 800.f * L, 0.f) },
		};
		const FCourse PivotOtherCorners[] = {
			{ FVector(B.X + BH.X - Pivot, B.Y - BH.Y - Pivot, 0.f), 180.f, FVector(B.X + BH.X + 800.f * L, B.Y, 0.f) },
			{ FVector(W.X - WH.X - Pivot, W.Y + WH.Y - Pivot, 0.f), -90.f, FVector(W.X + WH.X + 800.f * L, W.Y + WH.Y + 500.f * L, 0.f) },
			{ FactorySpot + FVector(-FH.X - Pivot, -FH.Y + Pivot, 0.f), 90.f, FactorySpot + FVector(0.5f * FH.X, -FH.Y - 800.f * L, 0.f) },
		};
		const TArray<URTSUnitData*> Hulls = GetHulls(*State);
		auto AddRounds = [&](const FString& Name, const FCourse (&Courses)[3], bool bStopAtObstacle)
		{
			for (int32 Round = 0; Round < Hulls.Num(); ++Round)
			{
				FDrive Drive;
				Drive.Name = FString::Printf(TEXT("%s, round %d"), *Name, Round + 1);
				Drive.bStopAtObstacle = bStopAtObstacle;
				for (int32 Course = 0; Course < UE_ARRAY_COUNT(Courses); ++Course)
				{
					// Each target on its own: the drive's target stays at the origin.
					const FCourse& Picked = Courses[Course];
					Drive.Units.Add({ Hulls[(Round + Course) % Hulls.Num()], Picked.Start, Picked.Yaw, Picked.Target });
				}
				State->Drives.Add(Drive);
			}
		};
		AddRounds(TEXT("Head on into Block_North +Y, Wall_West +X and the factory +Y"), HeadOn, true);
		AddRounds(TEXT("Head on into Block_North -Y, Wall_West -X and the factory -X"), HeadOnNegative, true);
		AddRounds(TEXT("Head on into Block_North -X, Wall_West's south end and the factory -Y"), HeadOnEnds, true);
		AddRounds(TEXT("Head on into Block_North +X, Wall_West's north end and the factory +X"), HeadOnRest, true);
		AddRounds(TEXT("Along Block_North, Wall_West and the factory"), Along, false);
		AddRounds(TEXT("Around Block_North, Wall_West and the factory"), Around, false);
		AddRounds(TEXT("Pivoting at Block_North's north-west corner, Wall_West's south end and the factory's north-east corner"), PivotCorners, false);
		AddRounds(TEXT("Pivoting at Block_North's south-east corner, Wall_West's north end and the factory's south-west corner"), PivotOtherCorners, false);

		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(&Test, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavPlaceBuildings(&Test, State, VehicleFactoryPath));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForHoles(&Test, State));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(&Test, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
		return true;
	}
}

/**
 * The M20, the placeholder tank, the long vehicle and the narrow one, all on tracks, each drive head on into, along and
 * around a corner of Block_North, Wall_West and a vehicle factory, and pivot beside their corners and both ends of the
 * wall: their hulls never overlap them, and stop within 50 cm of the ones they were ordered into.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavHullClearanceTest, "WorldDivide.Navigation.HullClearance.Drive", RTSTests::TestFlags)

bool FRTSNavHullClearanceTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	return QueueObstacleDrives(*this, MakeShared<FNavState>(), false, [](const FNavState& State)
	{
		return TArray<URTSUnitData*>{ State.M20, State.Tank, State.LongVehicle, State.NarrowVehicle };
	});
}

/**
 * The M20, its turret forward as it sets off, and the placeholder tank drive the same courses (#162): besides their hulls'
 * clearance, the M20's barrel never reaches into Block_North, Wall_West or the factory, head on, alongside or round a
 * corner, turning or rising out of the way instead. The tank's gun is part of its hull box.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavBarrelClearanceTest, "WorldDivide.Navigation.BarrelClearance.Drive", RTSTests::TestFlags)

bool FRTSNavBarrelClearanceTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	return QueueObstacleDrives(*this, MakeShared<FNavState>(), true, [](const FNavState& State)
	{
		return TArray<URTSUnitData*>{ State.M20, State.Tank };
	});
}

namespace RTSNavigationTests
{
	/** A wall the barrel cannot clear raised, and a low one it reaches over, each across an idle M20's front. */
	constexpr float OffNavmeshTallHeight = 500.f;
	constexpr float OffNavmeshLowHeight = 100.f;
	constexpr float OffNavmeshFrontGap = 20.f;
	constexpr float OffNavmeshBeyondReach = 200.f;
	constexpr float OffNavmeshHalfWidth = 300.f;
	/** The two M20s stand this far apart, south of FactorySpot. */
	constexpr float OffNavmeshSpacing = 2500.f;
	/**
	 * Game seconds for an idle unit to find the wall, at most a reach check's 1.25 s, and turn its barrel out of it, about
	 * 45 degrees at 45 a second; then how long the barrel is watched.
	 */
	constexpr double OffNavmeshFindSeconds = 4.0;
	constexpr double OffNavmeshWatchSeconds = 3.0;

	struct FOffNavmeshState
	{
		TWeakObjectPtr<ARTSUnit> Tall;
		TWeakObjectPtr<ARTSUnit> Low;
		FBox TallBox = FBox(ForceInit);
		FBox LowBox = FBox(ForceInit);
		double SpawnTime = -1.0;
		float TallDeepest = 0.f;
		float LowDeepest = 0.f;
	};

	float GetBarrelDepth(const ARTSUnit& Unit, const FBox& Box)
	{
		FVector Start;
		FVector End;
		return Unit.GetBarrelSegment(Start, End) ? RTSBarrelClearance::GetPenetration(Start, End, Unit.GetShape().Barrel->Radius, Box) : 0.f;
	}

	/** A solid engine cube filling Box that navigation leaves out: the navmesh runs on under it. */
	void SpawnOffNavmeshSolid(UWorld& World, const FBox& Box)
	{
		const FTransform Transform(FRotator::ZeroRotator, Box.GetCenter(), Box.GetSize() / 100.f);
		AStaticMeshActor* Actor = World.SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubePath));
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetCanEverAffectNavigation(false);
		Actor->FinishSpawning(Transform);
	}
}

/**
 * Spawns two idle M20s near FactorySpot, gun forward, and then across each one's front a solid box that navigation leaves
 * out: a tall one and a low one under the barrel.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavSpawnOffNavmesh, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FOffNavmeshState>, State);
bool FRTSNavSpawnOffNavmesh::Update()
{
	using namespace RTSNavigationTests;
	ARTSPlayerController* Controller = GetController(*Test);
	URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20Path);
	if (!Controller || !Test->TestNotNull(TEXT("M20 data"), Data))
	{
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	const FRTSUnitShape Shape = RTSUnitShape::FromData(*Data);
	if (!Test->TestTrue(TEXT("The M20 has a separate barrel"), Shape.Barrel.IsSet()))
	{
		return true;
	}
	const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(World, RTSNavAgents::SelectAgent(Shape));
	if (!Test->TestNotNull(TEXT("The M20's navmesh"), NavMesh))
	{
		return true;
	}
	const float Reach = Shape.Barrel->GetReach();
	auto Place = [&](const FVector& Spot, float Height, FBox& OutBox)
	{
		ARTSUnit* Unit = SpawnUnit(World, Data, Controller->GetTeamId(), Spot, 0.f);
		// Open ground: nothing in the barrel's reach, on the navmesh or off it, before the box goes up.
		FRTSFootprint Disc;
		Disc.Center = Spot;
		Disc.Radius = Reach;
		Test->TestTrue(*FString::Printf(TEXT("Open ground at %s"), *Spot.ToCompactString()),
			RTSHullClearance::GetClearance(*NavMesh, Disc) > 0.f
			&& !RTSBarrelClearance::IsAnythingInReach(World, *Shape.Barrel, Unit->GetActorTransform(), Data->GunMinPitch, Data->GunMaxPitch, 0.f));
		OutBox = FBox(Spot + FVector(Shape.Hull.Max.X + OffNavmeshFrontGap, -OffNavmeshHalfWidth, 0.f),
			Spot + FVector(Reach + OffNavmeshBeyondReach, OffNavmeshHalfWidth, Height));
		SpawnOffNavmeshSolid(World, OutBox);
		return Unit;
	};
	const FVector TallSpot = FactorySpot;
	const FVector LowSpot = FactorySpot - FVector(0.f, OffNavmeshSpacing, 0.f);
	State->Tall = Place(TallSpot, OffNavmeshTallHeight, State->TallBox);
	State->Low = Place(LowSpot, OffNavmeshLowHeight, State->LowBox);

	FVector Start;
	FVector End;
	if (State->Low->GetBarrelSegment(Start, End))
	{
		Test->TestTrue(FString::Printf(TEXT("The barrel reaches over the low box (muzzle z %.0f, box %.0f high)"), End.Z, State->LowBox.Max.Z),
			End.X > State->LowBox.Min.X && End.Z - Shape.Barrel->Radius > State->LowBox.Max.Z);
	}
	Test->TestTrue(TEXT("Parked with its gun forward, the barrel starts in the tall box"), GetBarrelDepth(*State->Tall, State->TallBox) > OverlapTolerance);
	State->SpawnTime = World.GetTimeSeconds();
	return true;
}

/** Watches both barrels once the M20s had time to find the boxes, then checks them and the navmesh under the boxes. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavWatchOffNavmesh, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FOffNavmeshState>, State);
bool FRTSNavWatchOffNavmesh::Update()
{
	using namespace RTSNavigationTests;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	ARTSUnit* Tall = State->Tall.Get();
	ARTSUnit* Low = State->Low.Get();
	if (!World || !Tall || !Low || State->SpawnTime < 0.0)
	{
		Test->AddError(TEXT("Lost the PIE world or the M20s"));
		return true;
	}
	const double Elapsed = World->GetTimeSeconds() - State->SpawnTime;
	if (Elapsed < OffNavmeshFindSeconds)
	{
		return false;
	}
	State->TallDeepest = FMath::Max(State->TallDeepest, GetBarrelDepth(*Tall, State->TallBox));
	State->LowDeepest = FMath::Max(State->LowDeepest, GetBarrelDepth(*Low, State->LowBox));
	if (Elapsed < OffNavmeshFindSeconds + OffNavmeshWatchSeconds)
	{
		return false;
	}

	// The boxes are left out of navigation: the navmesh still runs under them, so only the barrel's own checks see them.
	const ARecastNavMesh* NavMesh = RTSNavAgents::GetNavMesh(*World, Tall->GetNavAgentPropertiesRef());
	FNavLocation Projected;
	Test->TestTrue(TEXT("The navmesh runs on under the tall box"),
		NavMesh && NavMesh->ProjectPoint(State->TallBox.GetCenter() * FVector(1.f, 1.f, 0.f), Projected, FVector(10.f, 10.f, 250.f)));

	Test->TestTrue(TEXT("The tall box's M20 has no target"), Tall->GetWeaponComponent()->GetTarget() == nullptr);
	Test->TestTrue(FString::Printf(TEXT("The turret turned the barrel out of the tall box (%.0f degrees)"), Tall->GetTurretYaw()),
		FMath::Abs(Tall->GetTurretYaw()) >= 15.f);
	Test->TestTrue(FString::Printf(TEXT("The barrel stayed out of the tall box (%.1f cm at most from %.0f s on)"), State->TallDeepest, OffNavmeshFindSeconds),
		State->TallDeepest <= OverlapTolerance);

	Test->TestTrue(TEXT("The low box's M20 has no target"), Low->GetWeaponComponent()->GetTarget() == nullptr);
	Test->TestTrue(FString::Printf(TEXT("Over the low box the turret stays forward and the gun level (%.1f, %.1f degrees)"), Low->GetTurretYaw(),
		Low->GetGunPitch()), FMath::IsNearlyZero(Low->GetTurretYaw(), 0.5f) && FMath::IsNearlyZero(Low->GetGunPitch(), 0.5f));
	Test->TestTrue(FString::Printf(TEXT("The barrel stayed over the low box (%.1f cm at most)"), State->LowDeepest), State->LowDeepest <= OverlapTolerance);
	return true;
}

/**
 * A solid wall the navmesh leaves out stands across an idle M20's front (#162): its barrel turns out of it all the same,
 * found by the barrel's own overlap rather than the navmesh; over a low one it stays forward.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavBarrelOffNavmeshTest, "WorldDivide.Navigation.BarrelClearance.OffNavmesh", RTSTests::TestFlags)

bool FRTSNavBarrelOffNavmeshTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	const TSharedRef<FOffNavmeshState> OffNavmesh = MakeShared<FOffNavmeshState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavSpawnOffNavmesh(this, OffNavmesh));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWatchOffNavmesh(this, OffNavmesh));
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
	if (!InitLayoutFromEditor(*this))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavCheckFactoryExit(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSNavigationTests
{
	/**
	 * The enclosure the refusal tests use: four by four power plant cells on the strip north-west of the bases, the outer
	 * twelve a ring of plants, one of the four inside a plant too. The other three inside form a navmesh island no unit
	 * outside reaches; the plant inside is an obstacle only that island touches.
	 */
	FVector RingOrigin = FVector::ZeroVector;
	float RingCell = 0.f;

	FVector GetRingCell(int32 Column, int32 Row)
	{
		return RingOrigin + FVector(Column * RingCell, Row * RingCell, 0.f);
	}

	bool InitRing(FAutomationTestBase& Test)
	{
		const URTSBuildingData* Plant = LoadObject<URTSBuildingData>(nullptr, PowerPlantPath);
		const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!Test.TestNotNull(TEXT("Power plant data"), Plant) || !World)
		{
			return false;
		}
		RingCell = 2.f * RTSTests::GetFootprintExtent(*Plant).X;
		const float StripY = 0.5f * (BlockBox.Max.Y + RTSTests::GetMapHalfSize(*World));
		// Even footprints centre on a grid line.
		RingOrigin = FVector(FMath::GridSnap(WallBox.GetCenter().X - 2.f * RingCell, RTSBuildPlacement::GridSize),
			FMath::GridSnap(StripY, RTSBuildPlacement::GridSize), 0.f);
		return true;
	}

	TArray<FVector> GetRingSpots()
	{
		TArray<FVector> Spots;
		for (int32 Row = -1; Row <= 2; ++Row)
		{
			for (int32 Column = -1; Column <= 2; ++Column)
			{
				const bool bInside = Column >= 0 && Column <= 1 && Row >= 0 && Row <= 1;
				if (!bInside || (Column == 1 && Row == 1))
				{
					Spots.Add(GetRingCell(Column, Row));
				}
			}
		}
		return Spots;
	}

	/** Places the vehicle factory at FactorySpot, then the ring, and waits until all of them are cut into the navmeshes. */
	void QueuePlacements(FAutomationTestBase& Test, const TSharedRef<FNavState>& State)
	{
		State->PlacedSpots.Add(FactorySpot);
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavPlaceBuildings(&Test, State, VehicleFactoryPath));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
		{
			State->PlacedSpots = GetRingSpots();
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavPlaceBuildings(&Test, State, PowerPlantPath));
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForHoles(&Test, State));
	}

	/** Opens L_TestMap and lays out the obstacles and the ring; false, with an error, when something is missing. */
	bool OpenMoveDestinationMap(FAutomationTestBase& Test, const TSharedRef<FNavState>& State)
	{
		if (!AutomationOpenMap(TestMap))
		{
			Test.AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
			return false;
		}
		return InitLayoutFromEditor(Test) && State->LoadUnits(Test) && InitRing(Test)
			&& Test.TestNotNull(TEXT("Vehicle factory data"), LoadObject<URTSBuildingData>(nullptr, VehicleFactoryPath));
	}

	/** Starts PIE, waits for the navmeshes, places the factory and the ring, and runs State's drives. */
	void QueueDrives(FAutomationTestBase& Test, const TSharedRef<FNavState>& State)
	{
		ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(&Test, State));
		QueuePlacements(Test, State);
		ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(&Test, State));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}

/**
 * Move orders deep into solid obstacles (#164): the M20, the placeholder tank and infantry each sent onto the middle of the
 * 60 m Wall_West from both its ends and both its sides, deeper than the 30 m the move once looked back for navmesh, and onto
 * the centres of Block_North and a vehicle factory from every side, by move and by attack-move. Each drives to the reachable
 * spot nearest the target, stops within 50 cm of the obstacle without entering it, stays on its own side when it came from
 * one, and does not keep switching between forward and reverse.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavMoveIntoObstacleDeepTest, "WorldDivide.Navigation.MoveIntoObstacle.Deep", RTSTests::TestFlags)

bool FRTSNavMoveIntoObstacleDeepTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenMoveDestinationMap(*this, State))
	{
		return false;
	}
	const URTSBuildingData* Factory = LoadObject<URTSBuildingData>(nullptr, VehicleFactoryPath);
	const float L = LongVehicleSize.X / 1000.f;
	const FVector2D B = BlockBox.GetCenter();
	const FVector2D BH = BlockBox.GetExtent();
	const FVector2D W = WallBox.GetCenter();
	const FVector2D WH = WallBox.GetExtent();
	const FVector2D FH(RTSBuildPlacement::GetFootprintExtent(Factory->Footprint));
	struct FCourse
	{
		FVector Start;
		float Yaw;
		FVector Target;
		bool bOwnSide;
	};
	// One course at each obstacle per drive, so three units drive at once without meeting.
	const FCourse Sets[4][3] = {
		{
			{ FVector(W.X, W.Y - WH.Y - 1500.f * L, 0.f), 90.f, FVector(W, 0.f), false },
			{ FVector(B.X, B.Y + BH.Y + 1600.f * L, 0.f), -90.f, FVector(B, 0.f), true },
			{ FactorySpot + FVector(0.f, FH.Y + 1500.f * L, 0.f), -90.f, FactorySpot, true },
		},
		{
			{ FVector(W.X, W.Y + WH.Y + 1500.f * L, 0.f), -90.f, FVector(W, 0.f), false },
			{ FVector(B.X - BH.X - 1600.f * L, B.Y, 0.f), 0.f, FVector(B, 0.f), true },
			{ FactorySpot + FVector(-FH.X - 1500.f * L, 0.f, 0.f), 0.f, FactorySpot, true },
		},
		{
			{ FVector(W.X + WH.X + 1500.f * L, W.Y + 0.3f * WH.Y, 0.f), 180.f, FVector(W, 0.f), true },
			{ FVector(B.X, B.Y - BH.Y - 1600.f * L, 0.f), 90.f, FVector(B, 0.f), true },
			{ FactorySpot + FVector(0.f, -FH.Y - 1500.f * L, 0.f), 90.f, FactorySpot, true },
		},
		{
			{ FVector(W.X - WH.X - 1500.f * L, W.Y - 0.3f * WH.Y, 0.f), 0.f, FVector(W, 0.f), true },
			{ FVector(B.X + BH.X + 1600.f * L, B.Y, 0.f), 180.f, FVector(B, 0.f), true },
			{ FactorySpot + FVector(FH.X + 1500.f * L, 0.f, 0.f), 180.f, FactorySpot, true },
		},
	};
	const FString SetNames[4] = {
		TEXT("Wall_West's middle from its south end, Block_North +Y, the factory +Y"),
		TEXT("Wall_West's middle from its north end, Block_North -X, the factory -X"),
		TEXT("Wall_West's middle from its east side, Block_North -Y, the factory -Y"),
		TEXT("Wall_West's middle from its west side, Block_North +X, the factory +X"),
	};
	const TArray<URTSUnitData*> Hulls = { State->M20, State->Tank, State->Infantry };
	for (const ERTSCommandType Order : { ERTSCommandType::Move, ERTSCommandType::AttackMove })
	{
		for (int32 Set = 0; Set < UE_ARRAY_COUNT(Sets); ++Set)
		{
			for (int32 Round = 0; Round < Hulls.Num(); ++Round)
			{
				FDrive Drive;
				Drive.Name = FString::Printf(TEXT("%s into %s, round %d"), Order == ERTSCommandType::Move ? TEXT("Move") : TEXT("Attack-move"),
					*SetNames[Set], Round + 1);
				Drive.Order = Order;
				Drive.bStopAtObstacle = true;
				Drive.bCheckReversals = true;
				for (int32 Course = 0; Course < 3; ++Course)
				{
					const FCourse& Picked = Sets[Set][Course];
					FUnitStart Start{ Hulls[(Round + Course) % Hulls.Num()], Picked.Start, Picked.Yaw, Picked.Target };
					Start.bOwnSide = Picked.bOwnSide;
					Drive.Units.Add(Start);
				}
				State->Drives.Add(Drive);
			}
		}
	}
	QueueDrives(*this, State);
	return true;
}

/**
 * Move orders nothing can reach (#164): a group sent into the navmesh island inside a ring of power plants, and units each
 * sent onto the plant inside the ring, are refused. They stay where they stand, keep no order, and the player hears of it
 * once per group, not once per unit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavMoveIntoObstacleRefusedTest, "WorldDivide.Navigation.MoveIntoObstacle.Refused", RTSTests::TestFlags)

bool FRTSNavMoveIntoObstacleRefusedTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenMoveDestinationMap(*this, State))
	{
		return false;
	}
	// South of the ring, a cell and a half out from its south row.
	const float StartY = GetRingCell(0, -1).Y - 1.5f * RingCell;
	const TArray<URTSUnitData*> Hulls = { State->M20, State->Tank, State->Infantry };
	auto AddRefused = [&](const FString& Name, const FVector& Target, bool bGroupOrder)
	{
		FDrive Drive;
		Drive.Name = Name;
		Drive.Target = Target;
		Drive.bGroupOrder = bGroupOrder;
		Drive.bRefused = true;
		Drive.ExpectedRefusals = 1;
		for (int32 Index = 0; Index < Hulls.Num(); ++Index)
		{
			Drive.Units.Add({ Hulls[Index], FVector(GetRingCell(Index, 0).X, StartY, 0.f), 90.f });
		}
		State->Drives.Add(Drive);
	};
	AddRefused(TEXT("A group into the ring's island"), GetRingCell(0, 0), true);
	AddRefused(TEXT("Each unit onto the plant inside the ring"), GetRingCell(1, 1), false);
	QueueDrives(*this, State);
	return true;
}

/**
 * Groups and queues into obstacles (#164): six units selected together sent onto the middle of Wall_West, by move and by
 * attack-move, all close in on it; a tank given a Shift-queued chain, past the wall, into its middle, into the ring's island
 * and on, drives every reachable leg, skips the refused one with one refusal, and ends at the last.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavMoveIntoObstacleGroupTest, "WorldDivide.Navigation.MoveIntoObstacle.Group", RTSTests::TestFlags)

bool FRTSNavMoveIntoObstacleGroupTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenMoveDestinationMap(*this, State))
	{
		return false;
	}
	const float L = LongVehicleSize.X / 1000.f;
	const FVector2D W = WallBox.GetCenter();
	const FVector2D WH = WallBox.GetExtent();
	const TArray<URTSUnitData*> Hulls = { State->M20, State->Tank, State->Infantry, State->M20, State->Tank, State->Infantry };
	for (const ERTSCommandType Order : { ERTSCommandType::Move, ERTSCommandType::AttackMove })
	{
		FDrive Drive;
		Drive.Name = FString::Printf(TEXT("A group %s onto Wall_West's middle"), Order == ERTSCommandType::Move ? TEXT("moved") : TEXT("attack-moved"));
		Drive.Order = Order;
		Drive.Target = FVector(W, 0.f);
		Drive.bGroupOrder = true;
		Drive.bCheckReversals = true;
		for (int32 Index = 0; Index < Hulls.Num(); ++Index)
		{
			Drive.Units.Add({ Hulls[Index], FVector(W.X + WH.X + 4000.f * L, W.Y + (Index - 2.5f) * 1000.f * L, 0.f), 180.f });
		}
		State->Drives.Add(Drive);
	}

	FDrive Chain;
	Chain.Name = TEXT("A queued chain through Wall_West's middle and the ring's island");
	Chain.Target = FVector(W.X + WH.X + 2000.f * L, W.Y + 1000.f * L, 0.f);
	Chain.Queued = { FVector(W, 0.f), GetRingCell(0, 0), FVector(W.X + WH.X + 2000.f * L, W.Y - 1000.f * L, 0.f) };
	Chain.ExpectedRefusals = 1;
	Chain.bCheckReversals = true;
	Chain.bQueuedIntoObstacle = true;
	Chain.Units.Add({ State->Tank, FVector(W.X + WH.X + 3000.f * L, W.Y + 2000.f * L, 0.f), 180.f });
	State->Drives.Add(Chain);
	QueueDrives(*this, State);
	return true;
}

namespace RTSNavigationTests
{
	/** Searches each cost run makes per hull, cycling through its targets: as many as a large selection ordered at once. */
	constexpr int32 CostSearches = 50;
	/** No single search may take longer than this, in ms: a frame's share for the rare order that needs one. */
	constexpr double CostWorstMs = 5.0;
}

/**
 * Times RTSMoveDestination::FindReachable (#164) for the M20, the placeholder tank and infantry on targets deep in
 * Wall_West, Block_North, the factory and the plant inside the ring: the worst search and the total for a batch of orders.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavMeasureMoveDestination, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FNavState>, State);
bool FRTSNavMeasureMoveDestination::Update()
{
	using namespace RTSNavigationTests;
	ARTSPlayerController* Controller = GetController(*Test);
	if (!Controller)
	{
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	const FVector2D W = WallBox.GetCenter();
	const FVector2D WH = WallBox.GetExtent();
	const FVector Start(W.X, W.Y - WH.Y - 1500.f, 0.f);
	// The plant in the ring last: it alone has no reachable spot.
	const FVector Targets[] = { FVector(W, 0.f), FVector(BlockBox.GetCenter(), 0.f), FactorySpot, GetRingCell(1, 1) };
	constexpr int32 TargetCount = UE_ARRAY_COUNT(Targets);
	for (URTSUnitData* Data : { State->M20, State->Tank, State->Infantry })
	{
		ARTSUnit* Unit = SpawnUnit(World, Data, Controller->GetTeamId(), Start, 90.f);
		double Worst = 0.0;
		double Total = 0.0;
		int32 Found = 0;
		int32 Expected = 0;
		for (int32 Search = 0; Search < CostSearches; ++Search)
		{
			FVector Destination;
			const double Begin = FPlatformTime::Seconds();
			Found += RTSMoveDestination::FindReachable(*Unit, Targets[Search % TargetCount], Destination) ? 1 : 0;
			const double Ms = (FPlatformTime::Seconds() - Begin) * 1000.0;
			Worst = FMath::Max(Worst, Ms);
			Total += Ms;
			Expected += Search % TargetCount != TargetCount - 1 ? 1 : 0;
		}
		Test->AddInfo(FString::Printf(TEXT("%s: %d searches, %d found, worst %.3f ms, total %.3f ms"), *DescribeUnit(*Data),
			CostSearches, Found, Worst, Total));
		Test->TestTrue(*FString::Printf(TEXT("%s: worst search %.3f ms within %.1f ms"), *DescribeUnit(*Data), Worst, CostWorstMs),
			Worst <= CostWorstMs);
		Test->TestEqual(*FString::Printf(TEXT("%s: searches that found a spot"), *DescribeUnit(*Data)), Found, Expected);
		Unit->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavMoveIntoObstacleCostTest, "WorldDivide.Navigation.MoveIntoObstacle.Cost", RTSTests::TestFlags)

bool FRTSNavMoveIntoObstacleCostTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenMoveDestinationMap(*this, State))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	QueuePlacements(*this, State);
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavMeasureMoveDestination(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSNavigationTests
{
	const TCHAR* LongPathMap = TEXT("/Game/Maps/L_LongPath");
	const TCHAR* FlakTruckPath = TEXT("/Game/Data/Units/DA_TestFlakTruck.DA_TestFlakTruck");

	/**
	 * Where the long-path tests (#169) put things on L_LongPath, from its labelled walls: the start west of the pillar-filled
	 * pocket, the goal east of the baffle that the long way round leads past, a short trip beside the start, the middles of
	 * the walled enclosure and of the island in its moat, and the middle of Wall_Baffle.
	 */
	FVector LongPathStart = FVector::ZeroVector;
	FVector LongPathGoal = FVector::ZeroVector;
	FVector LongPathShort = FVector::ZeroVector;
	FVector EnclosureCenter = FVector::ZeroVector;
	FVector IslandCenter = FVector::ZeroVector;
	FVector BaffleCenter = FVector::ZeroVector;

	/** Game seconds a drive across L_LongPath may take: infantry walks the long way round in under two minutes. */
	constexpr double LongDriveTimeout = 240.0;

	/** The ground the labelled actors whose labels start with Prefix cover together, seen from above. */
	FBox2D GetLabelledUnion(const UWorld& World, const TCHAR* Prefix)
	{
		FBox2D Union(ForceInit);
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			if (It->GetActorLabel().StartsWith(Prefix))
			{
				const FBox Box = It->GetComponentsBoundingBox();
				Union += FBox2D(FVector2D(Box.Min), FVector2D(Box.Max));
			}
		}
		return Union;
	}

	bool InitLongPathLayout(FAutomationTestBase& Test)
	{
		const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			Test.AddError(TEXT("No editor world"));
			return false;
		}
		const float HalfSize = RTSTests::GetMapHalfSize(*World);
		const FBox2D Pocket = GetLabelledUnion(*World, TEXT("Wall_Pocket"));
		const FBox2D Baffle = RTSTests::GetLabelledBox(*World, TEXT("Wall_Baffle"));
		const FBox2D Enclosure = GetLabelledUnion(*World, TEXT("Wall_Enclosure"));
		const FBox2D Moat = GetLabelledUnion(*World, TEXT("Block_Moat"));
		LongVehicleSize = GetLongVehicleSize();
		if (HalfSize <= 0.f || !Pocket.bIsValid || !Baffle.bIsValid || !Enclosure.bIsValid || !Moat.bIsValid || LongVehicleSize.X <= 0.f)
		{
			Test.AddError(TEXT("Missing test data, or L_LongPath lacks its NavMeshBoundsVolume, pocket, baffle, enclosure or moat"));
			return false;
		}
		LongPathStart = FVector(0.5f * (-HalfSize + Pocket.Min.X), Pocket.GetCenter().Y, 0.f);
		LongPathGoal = FVector(0.5f * (Baffle.Max.X + HalfSize), Pocket.GetCenter().Y, 0.f);
		LongPathShort = LongPathStart - FVector(0.f, 0.25f * HalfSize, 0.f);
		EnclosureCenter = FVector(Enclosure.GetCenter(), 0.f);
		IslandCenter = FVector(Moat.GetCenter(), 0.f);
		BaffleCenter = FVector(Baffle.GetCenter(), 0.f);
		ArrivalRadius = LongVehicleSize.X;
		return true;
	}

	bool OpenLongPathMap(FAutomationTestBase& Test, const TSharedRef<FNavState>& State)
	{
		if (!AutomationOpenMap(LongPathMap))
		{
			Test.AddError(FString::Printf(TEXT("Could not open %s"), LongPathMap));
			return false;
		}
		return InitLongPathLayout(Test) && State->LoadUnits(Test)
			&& Test.TestNotNull(TEXT("Flak truck data"), LoadObject<URTSUnitData>(nullptr, FlakTruckPath));
	}

	/** A path query for Unit from where it stands to End on its own navmesh, with the navmesh's default filter. */
	TOptional<FPathFindingQuery> MakeQuery(const ARTSUnit& Unit, const FVector& End)
	{
		UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Unit.GetWorld());
		const ANavigationData* NavData = NavSystem ? NavSystem->GetNavDataForProps(Unit.GetNavAgentPropertiesRef(), Unit.GetNavAgentLocation()) : nullptr;
		if (!NavData)
		{
			return {};
		}
		FPathFindingQuery Query(&Unit, *NavData, Unit.GetNavAgentLocation(), End);
		Query.SetAllowPartialPaths(false);
		return Query;
	}

	/** The nodes A* visits from Query's start to its end with a node limit of Limit (TestPath counts them). */
	int32 CountSearchNodes(UNavigationSystemV1& NavSystem, const FPathFindingQuery& Query, uint32 Limit)
	{
		FPathFindingQuery Limited(Query);
		FSharedNavQueryFilter Filter = Query.QueryFilter->GetCopy();
		Filter->SetMaxSearchNodes(Limit);
		Limited.QueryFilter = Filter;
		int32 Visited = 0;
		NavSystem.TestPathSync(Limited, EPathFindingMode::Regular, &Visited);
		return Visited;
	}

	const TCHAR* DescribeOutcome(RTSPathSearch::EOutcome Outcome)
	{
		switch (Outcome)
		{
		case RTSPathSearch::EOutcome::Full: return TEXT("full");
		case RTSPathSearch::EOutcome::Unreachable: return TEXT("unreachable");
		case RTSPathSearch::EOutcome::Inconclusive: return TEXT("inconclusive");
		default: return TEXT("error");
		}
	}

	/** The hulls of the long-path drives: infantry, the tracked placeholder tank and M20, and the wheeled flak truck. */
	TArray<URTSUnitData*> GetLongPathHulls(const FNavState& State)
	{
		return { State.Infantry, State.Tank, State.M20, LoadObject<URTSUnitData>(nullptr, FlakTruckPath) };
	}

	struct FNamedSpot
	{
		const TCHAR* Name;
		FVector Location;
	};
}

/**
 * The searches of the long-path tests (#169) on L_LongPath, for each agent's navmesh: the route round the pocket runs the
 * engine's default search out of its 2048 nodes (so the map still tests what it should), RTSPathSearch finds it on its
 * second search, a short trip needs one search, and the enclosure and the island come back unreachable, proven. With the
 * raised limit cut to the default the long route is reported inconclusive, never unreachable. A move order into the middle
 * of Wall_Baffle, which only the long route reaches, finds a spot.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavCheckLongPathSearch, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FNavState>, State);
bool FRTSNavCheckLongPathSearch::Update()
{
	using namespace RTSNavigationTests;
	ARTSPlayerController* Controller = GetController(*Test);
	UNavigationSystemV1* NavSystem = Controller ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld()) : nullptr;
	if (!NavSystem)
	{
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	IConsoleVariable* MaxNodes = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Nav.MaxSearchNodes"));
	if (!Test->TestNotNull(TEXT("rts.Nav.MaxSearchNodes"), MaxNodes))
	{
		return true;
	}
	// One hull per navmesh.
	for (URTSUnitData* Data : { State->Infantry, State->Tank })
	{
		ARTSUnit* Unit = SpawnUnit(World, Data, Controller->GetTeamId(), LongPathStart, 0.f);
		const FString Name = DescribeUnit(*Data);
		auto Search = [&](const FVector& End, RTSPathSearch::FStats& Stats)
		{
			const TOptional<FPathFindingQuery> Query = MakeQuery(*Unit, End);
			return Query ? RTSPathSearch::FindFullPath(*NavSystem, *Query, &Stats) : FPathFindingResult();
		};

		const TOptional<FPathFindingQuery> LongQuery = MakeQuery(*Unit, LongPathGoal);
		if (!Test->TestTrue(*FString::Printf(TEXT("%s stands on its navmesh"), *Name), LongQuery.IsSet()))
		{
			Unit->Destroy();
			continue;
		}
		const FPathFindingResult Default = NavSystem->FindPathSync(*LongQuery);
		Test->TestTrue(*FString::Printf(TEXT("%s: the default search runs out of its %d nodes on the long route"), *Name, RECAST_MAX_SEARCH_NODES),
			Default.Path.IsValid() && Default.Path->IsPartial() && Default.Path->DidSearchReachedLimit());
		const int32 Nodes = CountSearchNodes(*NavSystem, *LongQuery, RTSPathSearch::GetMaxSearchNodes());

		RTSPathSearch::FStats Stats;
		FPathFindingResult Result = Search(LongPathGoal, Stats);
		Test->TestEqual(*FString::Printf(TEXT("%s: the long route"), *Name), FString(DescribeOutcome(Stats.Outcome)), FString(TEXT("full")));
		Test->TestEqual(*FString::Printf(TEXT("%s: searches for the long route"), *Name), Stats.Searches, 2);
		Test->TestTrue(*FString::Printf(TEXT("%s: the long route needs more than %d nodes (%d)"), *Name, RECAST_MAX_SEARCH_NODES, Nodes),
			Nodes > RECAST_MAX_SEARCH_NODES);
		const bool bEndsAtGoal = Result.Path.IsValid() && !Result.Path->GetPathPoints().IsEmpty()
			&& FVector::Dist2D(Result.Path->GetPathPoints().Last().Location, LongPathGoal) < 100.f;
		Test->TestTrue(*FString::Printf(TEXT("%s: the long route ends at the goal"), *Name), bEndsAtGoal);
		Test->AddInfo(FString::Printf(TEXT("%s: long route %.0f m, %d points, %d search nodes"), *Name,
			Result.Path.IsValid() ? Result.Path->GetLength() / 100.f : 0.f, Result.Path.IsValid() ? Result.Path->GetPathPoints().Num() : 0, Nodes));

		Search(LongPathShort, Stats);
		Test->TestEqual(*FString::Printf(TEXT("%s: the short trip"), *Name), FString(DescribeOutcome(Stats.Outcome)), FString(TEXT("full")));
		Test->TestEqual(*FString::Printf(TEXT("%s: searches for the short trip"), *Name), Stats.Searches, 1);

		for (const FNamedSpot& CutOff : { FNamedSpot{ TEXT("the enclosure"), EnclosureCenter }, FNamedSpot{ TEXT("the island"), IslandCenter } })
		{
			Result = Search(CutOff.Location, Stats);
			Test->TestEqual(*FString::Printf(TEXT("%s: %s"), *Name, CutOff.Name), FString(DescribeOutcome(Stats.Outcome)), FString(TEXT("unreachable")));
			Test->TestFalse(*FString::Printf(TEXT("%s: no path into %s"), *Name, CutOff.Name), Result.IsSuccessful());
		}

		// The limit cut to the default: the search that ran out is reported as such, with a warning, and not as unreachable.
		const int32 Configured = MaxNodes->GetInt();
		MaxNodes->Set(RECAST_MAX_SEARCH_NODES, ECVF_SetByCode);
		Test->AddExpectedMessage(TEXT("reachability unknown"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		Search(LongPathGoal, Stats);
		MaxNodes->Set(Configured, ECVF_SetByCode);
		Test->TestEqual(*FString::Printf(TEXT("%s: the long route within the default limit"), *Name), FString(DescribeOutcome(Stats.Outcome)),
			FString(TEXT("inconclusive")));

		FVector Destination;
		Test->TestTrue(*FString::Printf(TEXT("%s: a move into Wall_Baffle's middle finds a spot"), *Name),
			RTSMoveDestination::FindReachable(*Unit, BaffleCenter, Destination));
		Unit->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavLongPathSearchTest, "WorldDivide.Navigation.LongPath.Search", RTSTests::TestFlags)

bool FRTSNavLongPathSearchTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenLongPathMap(*this, State))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavCheckLongPathSearch(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/**
 * Orders across L_LongPath (#169), whose route runs the default search out of nodes: infantry, the tracked tank and M20 and
 * the wheeled flak truck side by side, each on its own move and then its own attack-move, all arrive at the far side; a
 * group of six closes in on it; a tank's Shift-queued chain, a short trip, the long route, the island and on, drives every
 * reachable leg and skips the island with one refusal. Groups sent into the enclosure and onto the island are refused: the
 * units stay put and the player hears of it once. The drive checks hulls against walls and pillars all the way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavLongPathDriveTest, "WorldDivide.Navigation.LongPath.Drive", RTSTests::TestFlags)

bool FRTSNavLongPathDriveTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenLongPathMap(*this, State))
	{
		return false;
	}
	const TArray<URTSUnitData*> Hulls = GetLongPathHulls(*State);
	const float Pitch = 1.2f * LongVehicleSize.X;
	auto SideBySide = [&](int32 Index) { return FVector(0.f, (Index - 0.5f * (Hulls.Num() - 1)) * Pitch, 0.f); };
	for (const ERTSCommandType Order : { ERTSCommandType::Move, ERTSCommandType::AttackMove })
	{
		FDrive Drive;
		Drive.Name = FString::Printf(TEXT("Each profile %s across the pocket"), Order == ERTSCommandType::Move ? TEXT("moved") : TEXT("attack-moved"));
		Drive.Order = Order;
		Drive.Target = LongPathGoal;
		Drive.bCheckReversals = true;
		Drive.Timeout = LongDriveTimeout;
		for (int32 Index = 0; Index < Hulls.Num(); ++Index)
		{
			FUnitStart Start{ Hulls[Index], LongPathStart + SideBySide(Index), 0.f };
			Start.TargetOffset = SideBySide(Index);
			Drive.Units.Add(Start);
		}
		State->Drives.Add(Drive);
	}

	FDrive Group;
	Group.Name = TEXT("A group of six moved across the pocket");
	Group.Target = LongPathGoal;
	Group.bGroupOrder = true;
	Group.Timeout = LongDriveTimeout;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Group.Units.Add({ Hulls[Index % Hulls.Num()], LongPathStart + FVector(-(Index / 3) * Pitch, (Index % 3 - 1) * Pitch, 0.f), 0.f });
	}
	State->Drives.Add(Group);

	FDrive Chain;
	Chain.Name = TEXT("A queued chain: a short trip, the long route, the island and on");
	Chain.Target = LongPathShort;
	Chain.Queued = { LongPathGoal, IslandCenter, LongPathGoal + FVector(0.f, Pitch, 0.f) };
	Chain.ExpectedRefusals = 1;
	Chain.bCheckReversals = true;
	Chain.Timeout = LongDriveTimeout;
	Chain.Units.Add({ State->Tank, LongPathStart, 0.f });
	State->Drives.Add(Chain);

	for (const FNamedSpot& CutOff : { FNamedSpot{ TEXT("the enclosure"), EnclosureCenter }, FNamedSpot{ TEXT("the island"), IslandCenter } })
	{
		FDrive Refused;
		Refused.Name = FString::Printf(TEXT("A group into %s"), CutOff.Name);
		Refused.Target = CutOff.Location;
		Refused.bGroupOrder = true;
		Refused.bRefused = true;
		Refused.ExpectedRefusals = 1;
		for (int32 Index = 0; Index < Hulls.Num(); ++Index)
		{
			Refused.Units.Add({ Hulls[Index], LongPathStart + SideBySide(Index), 0.f });
		}
		State->Drives.Add(Refused);
	}

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSNavigationTests
{
	/** Searches each long-path cost run makes per route and hull. */
	constexpr int32 LongCostSearches = 20;
	/** No single search, long or unreachable, may take longer than this, in ms: MoveIntoObstacle.Cost's budget. */
	constexpr double LongCostWorstMs = 5.0;
	/**
	 * What queuing a group's requests may add to its order's call beyond the budget and one request's overrun, in ms: the
	 * formation and a hundred queue entries.
	 */
	constexpr double LongCostQueueMs = 1.0;
	/** Game seconds a group order may take until every unit has its path. */
	constexpr double GroupResolveTimeout = 20.0;
}

/**
 * Times RTSPathSearch (#169) on L_LongPath for infantry and the tank: the short trip, the long route that needs the second
 * search, and the island that only a search of everything reachable proves cut off, with the nodes each visits. The group
 * orders follow in FRTSNavMeasureGroupOrders (#174).
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavMeasureLongPath, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FNavState>, State);
bool FRTSNavMeasureLongPath::Update()
{
	using namespace RTSNavigationTests;
	ARTSPlayerController* Controller = GetController(*Test);
	UNavigationSystemV1* NavSystem = Controller ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld()) : nullptr;
	if (!NavSystem)
	{
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	const uint32 Limit = RTSPathSearch::GetMaxSearchNodes();
	for (URTSUnitData* Data : { State->Infantry, State->Tank })
	{
		ARTSUnit* Unit = SpawnUnit(World, Data, Controller->GetTeamId(), LongPathStart, 0.f);
		const FString Name = DescribeUnit(*Data);
		for (const FNamedSpot& Route : { FNamedSpot{ TEXT("short trip"), LongPathShort }, FNamedSpot{ TEXT("long route"), LongPathGoal },
			FNamedSpot{ TEXT("island"), IslandCenter } })
		{
			const TOptional<FPathFindingQuery> Query = MakeQuery(*Unit, Route.Location);
			if (!Test->TestTrue(*FString::Printf(TEXT("%s stands on its navmesh"), *Name), Query.IsSet()))
			{
				break;
			}
			double Worst = 0.0;
			double Total = 0.0;
			RTSPathSearch::FStats Stats;
			for (int32 Search = 0; Search < LongCostSearches; ++Search)
			{
				const double Begin = FPlatformTime::Seconds();
				RTSPathSearch::FindFullPath(*NavSystem, *Query, &Stats);
				const double Ms = (FPlatformTime::Seconds() - Begin) * 1000.0;
				Worst = FMath::Max(Worst, Ms);
				Total += Ms;
			}
			const int32 Nodes = CountSearchNodes(*NavSystem, *Query, Limit);
			Test->AddInfo(FString::Printf(TEXT("%s, %s: %s after %d search(es), limit %u, %d nodes; worst %.3f ms, mean %.3f ms"), *Name,
				Route.Name, DescribeOutcome(Stats.Outcome), Stats.Searches, Stats.MaxSearchNodes, Nodes, Worst, Total / LongCostSearches));
			Test->TestTrue(*FString::Printf(TEXT("%s, %s: worst search %.3f ms within %.1f ms"), *Name, Route.Name, Worst, LongCostWorstMs),
				Worst <= LongCostWorstMs);
		}
		Unit->Destroy();
	}

	return true;
}

namespace RTSNavigationTests
{
	URTSMoveRequestSubsystem* GetMoveRequests(UWorld& World)
	{
		return World.GetSubsystem<URTSMoveRequestSubsystem>();
	}

	/** Count units of Hulls in turn on a square grid around Center, a long hull and a bit apart. */
	TArray<ARTSUnit*> SpawnGroup(UWorld& World, int32 TeamId, TConstArrayView<URTSUnitData*> Hulls, int32 Count, const FVector& Center)
	{
		TArray<ARTSUnit*> Group;
		const float Pitch = 1.2f * LongVehicleSize.X;
		const int32 Side = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector Offset(((Index / Side) - 0.5f * (Side - 1)) * Pitch, ((Index % Side) - 0.5f * (Side - 1)) * Pitch, 0.f);
			Group.Add(SpawnUnit(World, Hulls[Index % Hulls.Num()], TeamId, Center + Offset, 0.f));
		}
		return Group;
	}

	/** Whether any of Units still waits for its move's path search (URTSMoveRequestSubsystem). */
	bool AnyMoveRequestPending(TConstArrayView<TWeakObjectPtr<ARTSUnit>> Units)
	{
		return Units.ContainsByPredicate([](const TWeakObjectPtr<ARTSUnit>& Unit) { return Unit.IsValid() && Unit->IsMoveRequestPending(); });
	}

	/**
	 * Empty when Unit drives a usable path: a full one, on the navmesh of its own agent, ending at the goal its order gave
	 * it; else what is wrong.
	 */
	FString CheckUsablePath(const ARTSUnit& Unit)
	{
		const AAIController* AI = Unit.GetController<AAIController>();
		const UPathFollowingComponent* PathFollowing = AI ? AI->GetPathFollowingComponent() : nullptr;
		const FNavPathSharedPtr Path = PathFollowing ? PathFollowing->GetPath() : nullptr;
		if (!Path.IsValid() || !Path->IsValid() || Path->GetPathPoints().IsEmpty() || PathFollowing->GetStatus() != EPathFollowingStatus::Moving)
		{
			return TEXT("no path under way");
		}
		if (Path->IsPartial())
		{
			return TEXT("a partial path");
		}
		if (Path->GetNavigationDataUsed() != RTSNavAgents::GetNavMesh(*Unit.GetWorld(), Unit.GetNavAgentPropertiesRef()))
		{
			return TEXT("a path on another agent's navmesh");
		}
		const TOptional<FVector> Goal = Unit.GetMoveGoal();
		if (!Goal || FVector::Dist2D(Path->GetPathPoints().Last().Location, *Goal) > ArrivalRadius)
		{
			return TEXT("a path that ends away from its goal");
		}
		return FString();
	}

	/** One group order LongPath.Cost times over the frames it takes: Count units of the long-path hulls sent to Target. */
	struct FGroupCost
	{
		const TCHAR* Route = TEXT("");
		FVector Target = FVector::ZeroVector;
		int32 Count = 0;
		/** Every request has to run in the order's own frame. */
		bool bAtOnce = false;
	};

	struct FGroupCostState
	{
		TArray<FGroupCost> Orders;
		int32 Index = -1;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		TArray<FVector> Slots;
		double OrderMs = 0.0;
		double OrderTime = 0.0;
		uint64 OrderFrame = 0;
		int64 SearchesAtStart = 0;
		int64 RaisedAtStart = 0;
		int32 RefusalsAtStart = 0;
		/** Set once an order is checked: the next one waits a frame, so its own frame starts with the whole budget. */
		bool bWaitFrame = false;
		/** Real ms of the slowest frame since the order, whole frames; for the record. */
		double LastFrameSeconds = 0.0;
		double SlowestFrameMs = 0.0;
	};
}

/**
 * Group orders on L_LongPath under the move request budget (#174): 12, 50 and 100 units of the four long-path hulls
 * (infantry, the tracked tank and M20, the wheeled flak truck) sent across, and 100 on the short trip. Each order is timed
 * in its own call and then frame by frame until every unit has its path: no frame may spend more than
 * rts.Nav.MoveRequestBudgetMs before its last request, nor the order's call more than that and one request; every unit
 * ends up driving a full path on its own agent's navmesh to its own formation slot; one search per unit on the long route
 * runs out of nodes at most once per navmesh; and the short trip runs every request in the order's frame.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FRTSNavMeasureGroupOrders, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FGroupCostState>, Cost,
	TSharedRef<RTSNavigationTests::FNavState>, State);
bool FRTSNavMeasureGroupOrders::Update()
{
	using namespace RTSNavigationTests;
	ARTSPlayerController* Controller = GetController(*Test);
	URTSMoveRequestSubsystem* Requests = Controller ? GetMoveRequests(*Controller->GetWorld()) : nullptr;
	if (!Test->TestNotNull(TEXT("The move request subsystem"), Requests))
	{
		return true;
	}
	UWorld& World = *Controller->GetWorld();
	// This test measures the real-time budget itself, which a fixed-step run replaces with a request count.
	RTSTests::UseRealTimeMoveBudget();
	const double Budget = URTSMoveRequestSubsystem::GetBudgetMs();

	if (Cost->Index >= 0 && !Cost->bWaitFrame)
	{
		const double Now = FPlatformTime::Seconds();
		Cost->SlowestFrameMs = FMath::Max(Cost->SlowestFrameMs, (Now - Cost->LastFrameSeconds) * 1000.0);
		Cost->LastFrameSeconds = Now;
		const FGroupCost& Order = Cost->Orders[Cost->Index];
		const double Elapsed = World.GetTimeSeconds() - Cost->OrderTime;
		if (AnyMoveRequestPending(Cost->Units) && Elapsed < GroupResolveTimeout)
		{
			return false;
		}

		const URTSMoveRequestSubsystem::FStats& Stats = Requests->GetStats();
		const int64 Searches = RTSPathSearch::GetCounters().Searches - Cost->SearchesAtStart;
		const int64 Raised = RTSPathSearch::GetCounters().RaisedStarts - Cost->RaisedAtStart;
		const FString What = FString::Printf(TEXT("%d units on the %s"), Order.Count, Order.Route);
		Test->TestFalse(*FString::Printf(TEXT("%s: every unit got its path within %.0f s"), *What, GroupResolveTimeout), AnyMoveRequestPending(Cost->Units));
		int32 Usable = 0;
		for (int32 Index = 0; Index < Cost->Units.Num(); ++Index)
		{
			const ARTSUnit* Unit = Cost->Units[Index].Get();
			if (!Test->TestNotNull(*FString::Printf(TEXT("%s: unit %d is alive"), *What, Index), Unit))
			{
				continue;
			}
			const FString Problem = CheckUsablePath(*Unit);
			const TOptional<FVector> Target = Unit->GetMoveTarget();
			if (!Problem.IsEmpty())
			{
				Test->AddError(FString::Printf(TEXT("%s: %s drives %s"), *What, *DescribeUnit(*Unit->GetUnitData()), *Problem));
			}
			else if (!Target || FVector::Dist2D(*Target, Cost->Slots[Index]) > 1.f)
			{
				Test->AddError(FString::Printf(TEXT("%s: %s heads somewhere else than its formation slot"), *What, *DescribeUnit(*Unit->GetUnitData())));
			}
			else
			{
				++Usable;
			}
		}
		Test->TestEqual(*FString::Printf(TEXT("%s: units driving a usable path to their own slot"), *What), Usable, Order.Count);
		Test->TestEqual(*FString::Printf(TEXT("%s: refusals"), *What), Controller->GetOrderRefusalCount() - Cost->RefusalsAtStart, 0);
		Test->AddInfo(FString::Printf(TEXT("%s: order call %.3f ms; path work %.2f ms in all over %d frames, peak frame %.3f ms (%.3f before its last ")
			TEXT("request), worst request %.3f ms; %d run in the order's frame, %d later, %d stale; longest wait %d frames, %.2f s; %lld searches, ")
			TEXT("%lld started raised; slowest whole frame %.2f ms"), *What, Cost->OrderMs, Stats.TotalMs, Stats.Frames, Stats.PeakFrameMs,
			Stats.MaxSpentBeforeLastMs, Stats.WorstRequestMs, Stats.RunAtOnce, Stats.RunLater, Stats.Stale, Stats.MaxFramesWaited,
			Stats.MaxSecondsWaited, Searches, Raised, Cost->SlowestFrameMs));
		Test->TestTrue(*FString::Printf(TEXT("%s: no frame spent more than the %.1f ms budget before its last request (%.3f ms)"), *What, Budget,
			Stats.MaxSpentBeforeLastMs), Stats.MaxSpentBeforeLastMs <= Budget);
		Test->TestTrue(*FString::Printf(TEXT("%s: worst request %.3f ms within %.1f ms"), *What, Stats.WorstRequestMs, LongCostWorstMs),
			Stats.WorstRequestMs <= LongCostWorstMs);
		Test->TestTrue(*FString::Printf(TEXT("%s: peak frame %.3f ms within the budget and one request"), *What, Stats.PeakFrameMs),
			Stats.PeakFrameMs <= Budget + Stats.WorstRequestMs);
		Test->TestTrue(*FString::Printf(TEXT("%s: the order's call %.3f ms within the budget, one request and %.1f ms of queuing"), *What,
			Cost->OrderMs, LongCostQueueMs), Cost->OrderMs <= Budget + Stats.WorstRequestMs + LongCostQueueMs);
		Test->TestTrue(*FString::Printf(TEXT("%s: the first unit started in the order's frame"), *What), Stats.RunAtOnce >= 1);
		if (Order.bAtOnce)
		{
			Test->TestEqual(*FString::Printf(TEXT("%s: requests left for later frames"), *What), Stats.RunLater, 0);
		}
		else
		{
			// One search per unit, and a second only for the first unit on each navmesh: the hint starts the rest raised.
			const int32 Navmeshes = RTSNavAgents::GetAgents().Num();
			Test->TestTrue(*FString::Printf(TEXT("%s: %lld searches for %d units"), *What, Searches, Order.Count), Searches <= Order.Count + Navmeshes);
			Test->TestTrue(*FString::Printf(TEXT("%s: %lld searches started raised"), *What, Raised), Raised >= Order.Count - Navmeshes);
		}
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Cost->Units)
		{
			if (Unit.IsValid())
			{
				Unit->Destroy();
			}
		}
		Cost->Units.Reset();
		Cost->bWaitFrame = true;
		return false;
	}
	if (Cost->bWaitFrame)
	{
		Cost->bWaitFrame = false;
	}

	++Cost->Index;
	if (!Cost->Orders.IsValidIndex(Cost->Index))
	{
		return true;
	}
	const FGroupCost& Next = Cost->Orders[Cost->Index];
	const TArray<ARTSUnit*> Group = SpawnGroup(World, Controller->GetTeamId(), GetLongPathHulls(*State), Next.Count, LongPathStart);
	Cost->Units = TArray<TWeakObjectPtr<ARTSUnit>>(Group);
	Cost->Slots = RTSCommands::PlanFormation(Group, Next.Target);
	Requests->ResetStats();
	Cost->SearchesAtStart = RTSPathSearch::GetCounters().Searches;
	Cost->RaisedAtStart = RTSPathSearch::GetCounters().RaisedStarts;
	Cost->RefusalsAtStart = Controller->GetOrderRefusalCount();
	FRTSCommand Move;
	Move.Type = ERTSCommandType::Move;
	Move.TargetLocation = Next.Target;
	const double Begin = FPlatformTime::Seconds();
	Controller->Server_IssueCommand(Group, Move);
	Cost->LastFrameSeconds = FPlatformTime::Seconds();
	Cost->OrderMs = (Cost->LastFrameSeconds - Begin) * 1000.0;
	Cost->OrderTime = World.GetTimeSeconds();
	Cost->SlowestFrameMs = 0.0;
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavLongPathCostTest, "WorldDivide.Navigation.LongPath.Cost", RTSTests::TestFlags)

bool FRTSNavLongPathCostTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenLongPathMap(*this, State))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavMeasureLongPath(this, State));
	const TSharedRef<FGroupCostState> Cost = MakeShared<FGroupCostState>();
	for (const int32 Count : { 12, 50, 100 })
	{
		Cost->Orders.Add({ TEXT("long route"), LongPathGoal, Count, false });
	}
	Cost->Orders.Add({ TEXT("short trip"), LongPathShort, 100, true });
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavMeasureGroupOrders(this, Cost, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/**
 * Group orders across L_LongPath under the move request budget (#174), with the drives' obstacle, arrival and refusal
 * checks: a group of 100 infantry closes in on the far side; a group of twelve of the four long-path hulls attack-moves on
 * the short trip and, Shift-queued, across.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavLongPathGroupDriveTest, "WorldDivide.Navigation.LongPath.GroupDrive", RTSTests::TestFlags)

bool FRTSNavLongPathGroupDriveTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenLongPathMap(*this, State))
	{
		return false;
	}
	const TArray<URTSUnitData*> Hulls = GetLongPathHulls(*State);
	const float Pitch = 1.2f * LongVehicleSize.X;
	auto AddGroup = [&](FDrive& Drive, TConstArrayView<URTSUnitData*> Datas, int32 Count)
	{
		const int32 Side = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector Offset(((Index / Side) - 0.5f * (Side - 1)) * Pitch, ((Index % Side) - 0.5f * (Side - 1)) * Pitch, 0.f);
			Drive.Units.Add({ Datas[Index % Datas.Num()], LongPathStart + Offset, 0.f });
		}
	};

	// Infantry: a hundred vehicles jam the pocket's mouth and the 12 m gap, budget or none (see PERFORMANCE_RESULTS.md, #174).
	FDrive Hundred;
	Hundred.Name = TEXT("A group of 100 infantry moved across the pocket");
	Hundred.Target = LongPathGoal;
	Hundred.bGroupOrder = true;
	Hundred.Timeout = LongDriveTimeout;
	AddGroup(Hundred, { State->Infantry }, 100);
	State->Drives.Add(Hundred);

	FDrive Queued;
	Queued.Name = TEXT("A group of twelve attack-moved on the short trip, then queued across");
	Queued.Order = ERTSCommandType::AttackMove;
	Queued.Target = LongPathShort;
	Queued.Queued = { LongPathGoal };
	Queued.bGroupOrder = true;
	Queued.Timeout = LongDriveTimeout;
	AddGroup(Queued, Hulls, 12);
	State->Drives.Add(Queued);

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSNavigationTests
{
	/** A budget so small that every frame runs one request: whatever a group orders waits for many frames. */
	constexpr float OneRequestPerFrameMs = 0.001f;
	/** Game seconds a pending-request phase may take. */
	constexpr double PendingPhaseTimeout = 30.0;

	/** The steps of LongPath.Pending, each called every frame until it returns true, with the seconds since it began. */
	struct FPendingState
	{
		TSharedPtr<FNavState> Nav;
		TArray<TPair<FString, TFunction<bool(double)>>> Phases;
		int32 Index = 0;
		double PhaseStart = -1.0;
		/** The units of the phase at hand, and what it measures. */
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		TArray<TWeakObjectPtr<ARTSUnit>> Others;
		TArray<FVector> Starts;
		TArray<FVector> Slots;
		TArray<int32> Resolved;
		int32 RefusalsAtStart = 0;
		float SavedBudget = 0.f;
	};

	/** Requests a frame the counted phase of WorldDivide.Navigation.LongPath.Pending runs, in place of the ms budget. */
	constexpr int32 CountedRequestsPerFrame = 3;

	IConsoleVariable* GetBudgetVariable()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Nav.MoveRequestBudgetMs"));
	}

	void DestroyAll(TArray<TWeakObjectPtr<ARTSUnit>>& Units)
	{
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
		{
			if (Unit.IsValid())
			{
				Unit->Destroy();
			}
		}
		Units.Reset();
	}

	void Order(ARTSPlayerController& Controller, TConstArrayView<TWeakObjectPtr<ARTSUnit>> Units, ERTSCommandType Type, const FVector& Target,
		bool bQueue = false)
	{
		TArray<ARTSUnit*> Group;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
		{
			if (Unit.IsValid())
			{
				Group.Add(Unit.Get());
			}
		}
		FRTSCommand Command;
		Command.Type = Type;
		Command.TargetLocation = Target;
		Command.bQueue = bQueue;
		Controller.Server_IssueCommand(Group, Command);
	}

	TArray<TWeakObjectPtr<ARTSUnit>> SpawnWeakGroup(UWorld& World, int32 TeamId, TConstArrayView<URTSUnitData*> Hulls, int32 Count, const FVector& Center)
	{
		return TArray<TWeakObjectPtr<ARTSUnit>>(SpawnGroup(World, TeamId, Hulls, Count, Center));
	}

	TArray<FVector> PlanSlots(TConstArrayView<TWeakObjectPtr<ARTSUnit>> Units, const FVector& Target)
	{
		TArray<ARTSUnit*> Group;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
		{
			Group.Add(Unit.Get());
		}
		return RTSCommands::PlanFormation(Group, Target);
	}

	void AddPendingPhases(FAutomationTestBase& Test, const TSharedRef<FPendingState>& Pending)
	{
		auto World = []() -> UWorld& { return *AutomationCommon::GetAnyGameWorld(); };
		auto Controller = [&Test]() { return GetController(Test); };
		auto Requests = [World]() { return GetMoveRequests(World()); };
		auto Hulls = [Pending]() { return GetLongPathHulls(*Pending->Nav); };

		Pending->Phases.Add({ TEXT("Cut the budget to one request a frame"), [Pending](double)
		{
			// The real-time budget, cut: a fixed-step run's request count would otherwise stand in for it.
			RTSTests::UseRealTimeMoveBudget();
			IConsoleVariable* Budget = GetBudgetVariable();
			Pending->SavedBudget = Budget ? Budget->GetFloat() : 2.f;
			if (Budget)
			{
				Budget->Set(OneRequestPerFrameMs, ECVF_SetByCode);
			}
			return true;
		} });

		// 1. Unreachable: every unit refused, the player told once though the refusals take longer than the cooldown.
		Pending->Phases.Add({ TEXT("Order 50 infantry onto the island"), [=, &Test](double)
		{
			Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), { Pending->Nav->Infantry }, 50, LongPathStart);
			Pending->Starts.Reset();
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Pending->Units)
			{
				Pending->Starts.Add(Unit->GetActorLocation());
			}
			Pending->RefusalsAtStart = Controller()->GetOrderRefusalCount();
			Requests()->ResetStats();
			Order(*Controller(), Pending->Units, ERTSCommandType::Move, IslandCenter);
			return true;
		} });
		Pending->Phases.Add({ TEXT("The island group is refused"), [=, &Test](double Elapsed)
		{
			if (AnyMoveRequestPending(Pending->Units) || Elapsed < 0.5)
			{
				return false;
			}
			const URTSMoveRequestSubsystem::FStats& Stats = Requests()->GetStats();
			Test.TestTrue(*FString::Printf(TEXT("Island group: its requests ran over %d frames, %.2f s, longer than the refusal cooldown"), Stats.Frames,
				Stats.MaxSecondsWaited), Stats.MaxSecondsWaited > ARTSPlayerController::OrderRefusalCooldown);
			for (int32 Index = 0; Index < Pending->Units.Num(); ++Index)
			{
				const ARTSUnit* Unit = Pending->Units[Index].Get();
				if (Test.TestNotNull(TEXT("Island group: a unit is alive"), Unit))
				{
					Test.TestEqual(TEXT("Island group: a unit dropped its order"), Unit->GetQueuedCommandCount(), 0);
					Test.TestTrue(TEXT("Island group: a unit stayed put"), FVector::Dist2D(Unit->GetActorLocation(), Pending->Starts[Index]) <= RefusedDrift);
				}
			}
			Test.TestEqual(TEXT("Island group: refusals the player received"), Controller()->GetOrderRefusalCount() - Pending->RefusalsAtStart, 1);
			DestroyAll(Pending->Units);
			return true;
		} });

		// 2. Stop while the paths wait: every waiting request goes stale, nobody moves off.
		Pending->Phases.Add({ TEXT("Order 30 across, then stop them"), [=, &Test](double Elapsed)
		{
			if (Pending->Units.IsEmpty())
			{
				Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 30, LongPathStart);
				Requests()->ResetStats();
				Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathGoal);
				return false;
			}
			Test.TestTrue(TEXT("Stop: requests still wait when the stop comes"), AnyMoveRequestPending(Pending->Units));
			Order(*Controller(), Pending->Units, ERTSCommandType::Stop, FVector::ZeroVector);
			Test.TestFalse(TEXT("Stop: no request waits after it"), AnyMoveRequestPending(Pending->Units));
			return true;
		} });
		Pending->Phases.Add({ TEXT("The stopped group stands"), [=, &Test](double Elapsed)
		{
			if (Elapsed < 1.0)
			{
				return false;
			}
			const URTSMoveRequestSubsystem::FStats& Stats = Requests()->GetStats();
			Test.TestTrue(*FString::Printf(TEXT("Stop: %d requests skipped as stale"), Stats.Stale), Stats.Stale >= 25);
			Test.TestEqual(TEXT("Stop: requests waiting"), Requests()->GetPendingCount(), 0);
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Pending->Units)
			{
				const AAIController* AI = Unit.IsValid() ? Unit->GetController<AAIController>() : nullptr;
				if (Test.TestNotNull(TEXT("Stop: a unit is alive"), AI))
				{
					Test.TestEqual(TEXT("Stop: a unit's orders"), Unit->GetQueuedCommandCount(), 0);
					Test.TestTrue(TEXT("Stop: a unit is not moving"), AI->GetMoveStatus() == EPathFollowingStatus::Idle);
				}
			}
			DestroyAll(Pending->Units);
			return true;
		} });

		// 3. Orders replaced frame after frame: only the last one's paths are used.
		Pending->Phases.Add({ TEXT("Order 30 across, onto the island, then on the short trip"), [=, &Test](double Elapsed)
		{
			if (Pending->Units.IsEmpty())
			{
				Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 30, LongPathStart);
				Pending->RefusalsAtStart = Controller()->GetOrderRefusalCount();
				Pending->Resolved.Reset();
				Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathGoal);
				return false;
			}
			Pending->Resolved.Add(0);
			if (Pending->Resolved.Num() == 1)
			{
				Order(*Controller(), Pending->Units, ERTSCommandType::Move, IslandCenter);
				return false;
			}
			Pending->Slots = PlanSlots(Pending->Units, LongPathShort);
			Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathShort);
			return true;
		} });
		Pending->Phases.Add({ TEXT("The replaced group drives the last order"), [=, &Test](double)
		{
			if (AnyMoveRequestPending(Pending->Units))
			{
				return false;
			}
			for (int32 Index = 0; Index < Pending->Units.Num(); ++Index)
			{
				const ARTSUnit* Unit = Pending->Units[Index].Get();
				if (!Test.TestNotNull(TEXT("Replaced orders: a unit is alive"), Unit))
				{
					continue;
				}
				const FString Problem = CheckUsablePath(*Unit);
				Test.TestTrue(*FString::Printf(TEXT("Replaced orders: %s drives a usable path (%s)"), *DescribeUnit(*Unit->GetUnitData()), *Problem),
					Problem.IsEmpty());
				const TOptional<FVector> Target = Unit->GetMoveTarget();
				Test.TestTrue(TEXT("Replaced orders: a unit heads for its slot of the last order"), Target && FVector::Dist2D(*Target, Pending->Slots[Index]) <= 1.f);
			}
			// At most the island order's one request that ran in its own frame, before the short trip replaced it.
			Test.TestTrue(TEXT("Replaced orders: at most one refusal"), Controller()->GetOrderRefusalCount() - Pending->RefusalsAtStart <= 1);
			DestroyAll(Pending->Units);
			return true;
		} });

		// 4. Units destroyed while their requests wait.
		Pending->Phases.Add({ TEXT("Order 20 across, then destroy half of them"), [=, &Test](double)
		{
			if (Pending->Units.IsEmpty())
			{
				Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 20, LongPathStart);
				Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathGoal);
				return false;
			}
			for (int32 Index = 0; Index < Pending->Units.Num(); Index += 2)
			{
				Pending->Units[Index]->Destroy();
			}
			return true;
		} });
		Pending->Phases.Add({ TEXT("The survivors get their paths"), [=, &Test](double)
		{
			if (AnyMoveRequestPending(Pending->Units))
			{
				return false;
			}
			int32 Alive = 0;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Pending->Units)
			{
				if (IsValid(Unit.Get()))
				{
					++Alive;
					const FString Problem = CheckUsablePath(*Unit);
					Test.TestTrue(*FString::Printf(TEXT("Destroyed units: a survivor drives a usable path (%s)"), *Problem), Problem.IsEmpty());
				}
			}
			Test.TestEqual(TEXT("Destroyed units: survivors"), Alive, 10);
			Test.TestEqual(TEXT("Destroyed units: requests waiting"), Requests()->GetPendingCount(), 0);
			DestroyAll(Pending->Units);
			return true;
		} });

		// 5. A Shift-queued leg behind a waiting move: the move starts once its path comes, the leg stays queued.
		Pending->Phases.Add({ TEXT("Order 12 across and queue the short trip"), [=, &Test](double)
		{
			Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 12, LongPathStart);
			Pending->Slots = PlanSlots(Pending->Units, LongPathGoal);
			Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathGoal);
			Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathShort, /*bQueue*/ true);
			return true;
		} });
		Pending->Phases.Add({ TEXT("The queued group starts its first leg"), [=, &Test](double)
		{
			if (AnyMoveRequestPending(Pending->Units))
			{
				return false;
			}
			for (int32 Index = 0; Index < Pending->Units.Num(); ++Index)
			{
				const ARTSUnit* Unit = Pending->Units[Index].Get();
				if (Test.TestNotNull(TEXT("Queued: a unit is alive"), Unit))
				{
					const FString Problem = CheckUsablePath(*Unit);
					Test.TestTrue(*FString::Printf(TEXT("Queued: a unit drives a usable path (%s)"), *Problem), Problem.IsEmpty());
					const TOptional<FVector> Target = Unit->GetMoveTarget();
					Test.TestTrue(TEXT("Queued: a unit heads for its slot of the first leg"), Target && FVector::Dist2D(*Target, Pending->Slots[Index]) <= 1.f);
					Test.TestEqual(TEXT("Queued: a unit's orders"), Unit->GetQueuedCommandCount(), 2);
				}
			}
			DestroyAll(Pending->Units);
			return true;
		} });

		// 6. Two issuers ordering at once take turns: neither waits for the other's whole group.
		Pending->Phases.Add({ TEXT("A player and an AI each order 20 across"), [=, &Test](double)
		{
			Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 20, LongPathStart + FVector(0.f, 3000.f, 0.f));
			Pending->Others = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 20, LongPathStart - FVector(0.f, 3000.f, 0.f));
			Pending->Resolved.Reset();
			Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathGoal);
			// As an AI player orders: through Dispatch, with no player to tell of a refusal.
			TArray<ARTSUnit*> Group;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Pending->Others)
			{
				Group.Add(Unit.Get());
			}
			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = LongPathGoal + FVector(0.f, -2000.f, 0.f);
			RTSCommands::Dispatch(Group, Move);
			return true;
		} });
		Pending->Phases.Add({ TEXT("Both groups get their paths in turn"), [=, &Test](double)
		{
			// Resolved records, in order, which group each newly started unit belongs to: 0 the player's, 1 the AI's.
			for (int32 Group = 0; Group < 2; ++Group)
			{
				for (const TWeakObjectPtr<ARTSUnit>& Unit : Group == 0 ? Pending->Units : Pending->Others)
				{
					if (Unit.IsValid() && !Unit->IsMoveRequestPending() && !Unit->Tags.Contains(TEXT("Resolved")))
					{
						Unit->Tags.Add(TEXT("Resolved"));
						Pending->Resolved.Add(Group);
					}
				}
			}
			if (AnyMoveRequestPending(Pending->Units) || AnyMoveRequestPending(Pending->Others))
			{
				return false;
			}
			int32 PlayerFirst = 0;
			for (int32 Index = 0; Index < 20 && Pending->Resolved.IsValidIndex(Index); ++Index)
			{
				PlayerFirst += Pending->Resolved[Index] == 0 ? 1 : 0;
			}
			Test.TestTrue(*FString::Printf(TEXT("Two issuers: of the first 20 units to start, %d are the player's and %d the AI's"), PlayerFirst, 20 - PlayerFirst),
				PlayerFirst >= 8 && PlayerFirst <= 12);
			for (TArray<TWeakObjectPtr<ARTSUnit>>* Units : { &Pending->Units, &Pending->Others })
			{
				for (const TWeakObjectPtr<ARTSUnit>& Unit : *Units)
				{
					if (Test.TestNotNull(TEXT("Two issuers: a unit is alive"), Unit.Get()))
					{
						const FString Problem = CheckUsablePath(*Unit);
						Test.TestTrue(*FString::Printf(TEXT("Two issuers: a unit drives a usable path (%s)"), *Problem), Problem.IsEmpty());
					}
				}
				DestroyAll(*Units);
			}
			return true;
		} });

		// 7. A count of requests per frame instead of the ms budget (#179): the same frames on any machine, the issuers in turn.
		Pending->Phases.Add({ TEXT("Three requests a frame: a player and an AI each order 12 across"), [=, &Test](double)
		{
			if (IConsoleVariable* PerFrame = RTSTests::GetMoveRequestsPerFrameVariable())
			{
				PerFrame->Set(CountedRequestsPerFrame, ECVF_SetByCode);
			}
			Pending->Units = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 12, LongPathStart + FVector(0.f, 3000.f, 0.f));
			Pending->Others = SpawnWeakGroup(World(), Controller()->GetTeamId(), Hulls(), 12, LongPathStart - FVector(0.f, 3000.f, 0.f));
			Requests()->ResetStats();
			Order(*Controller(), Pending->Units, ERTSCommandType::Move, LongPathGoal);
			TArray<ARTSUnit*> Group;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Pending->Others)
			{
				Group.Add(Unit.Get());
			}
			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = LongPathGoal + FVector(0.f, -2000.f, 0.f);
			RTSCommands::Dispatch(Group, Move);
			return true;
		} });
		Pending->Phases.Add({ TEXT("Both groups get their paths, three a frame"), [=, &Test](double)
		{
			if (AnyMoveRequestPending(Pending->Units) || AnyMoveRequestPending(Pending->Others))
			{
				return false;
			}
			const URTSMoveRequestSubsystem::FStats& Stats = Requests()->GetStats();
			Test.TestEqual(TEXT("Counted: frames that ran requests"), Stats.Frames, 24 / CountedRequestsPerFrame);
			Test.TestEqual(TEXT("Counted: requests run in the order's frame"), Stats.RunAtOnce, CountedRequestsPerFrame);
			Test.TestEqual(TEXT("Counted: requests run later"), Stats.RunLater, 24 - CountedRequestsPerFrame);
			Test.TestEqual(TEXT("Counted: the longest wait in frames"), Stats.MaxFramesWaited, 24 / CountedRequestsPerFrame - 1);
			Test.TestEqual(TEXT("Counted: stale requests"), Stats.Stale, 0);
			for (TArray<TWeakObjectPtr<ARTSUnit>>* Units : { &Pending->Units, &Pending->Others })
			{
				for (const TWeakObjectPtr<ARTSUnit>& Unit : *Units)
				{
					if (Test.TestNotNull(TEXT("Counted: a unit is alive"), Unit.Get()))
					{
						const FString Problem = CheckUsablePath(*Unit);
						Test.TestTrue(*FString::Printf(TEXT("Counted: a unit drives a usable path (%s)"), *Problem), Problem.IsEmpty());
					}
				}
				DestroyAll(*Units);
			}
			RTSTests::UseRealTimeMoveBudget();
			return true;
		} });

		Pending->Phases.Add({ TEXT("Restore the budget"), [Pending](double)
		{
			if (IConsoleVariable* Budget = GetBudgetVariable())
			{
				Budget->Set(Pending->SavedBudget, ECVF_SetByCode);
			}
			return true;
		} });
	}
}

/** Gives rts.Nav.MoveRequestBudgetMs its default back, whatever a phase that cut it left behind. */
DEFINE_LATENT_AUTOMATION_COMMAND(FRTSNavRestoreBudget);
bool FRTSNavRestoreBudget::Update()
{
	if (IConsoleVariable* Budget = RTSNavigationTests::GetBudgetVariable())
	{
		Budget->Set(URTSMoveRequestSubsystem::DefaultBudgetMs, ECVF_SetByCode);
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavRunPendingPhases, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FPendingState>, Pending);
bool FRTSNavRunPendingPhases::Update()
{
	using namespace RTSNavigationTests;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	if (!World || !GetController(*Test) || !Test->TestNotNull(TEXT("The move request subsystem"), GetMoveRequests(*World))
		|| !Pending->Phases.IsValidIndex(Pending->Index))
	{
		return true;
	}
	const double Now = World->GetTimeSeconds();
	if (Pending->PhaseStart < 0.0)
	{
		Pending->PhaseStart = Now;
	}
	TPair<FString, TFunction<bool(double)>>& Phase = Pending->Phases[Pending->Index];
	if (Phase.Value(Now - Pending->PhaseStart))
	{
		++Pending->Index;
		Pending->PhaseStart = -1.0;
		return !Pending->Phases.IsValidIndex(Pending->Index);
	}
	if (Now - Pending->PhaseStart > PendingPhaseTimeout)
	{
		Test->AddError(FString::Printf(TEXT("%s did not finish in %.0f s"), *Phase.Key, PendingPhaseTimeout));
		return true;
	}
	return false;
}

/**
 * Move requests that wait for their turn (#174), with the budget cut to one request a frame so every group waits:
 * 50 infantry sent onto the island are all refused, stay put, and the player hears of it once, though the refusals take
 * longer than the cooldown; a Stop makes 30 waiting requests stale and nobody moves; orders replaced on the next frames
 * leave only the last one's paths; units destroyed while they wait are skipped and the rest get their paths; a Shift-queued
 * leg waits behind its group's first leg; a player's and an AI's groups ordered at once start in turn; and with
 * rts.Nav.MoveRequestsPerFrame at 3, as fixed-step test runs count requests (#179), 24 requests run three a frame.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavLongPathPendingTest, "WorldDivide.Navigation.LongPath.Pending", RTSTests::TestFlags)

bool FRTSNavLongPathPendingTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenLongPathMap(*this, State))
	{
		return false;
	}
	const TSharedRef<FPendingState> Pending = MakeShared<FPendingState>();
	Pending->Nav = State;
	AddPendingPhases(*this, Pending);
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunPendingPhases(this, Pending));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRestoreBudget());
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

namespace RTSNavigationTests
{
	const TCHAR* ThinWallsMap = TEXT("/Game/Maps/L_ThinWalls");

	/**
	 * L_ThinWalls (#173), from its labelled walls: the north end of the free-standing Wall_Free and its north-east corner,
	 * and the junction where Wall_JunctionNorth and Wall_JunctionEast cross, each running 3 m past the other.
	 */
	FVector2D ThinWallEnd = FVector2D::ZeroVector;
	FVector2D ThinWallEndCorner = FVector2D::ZeroVector;
	FVector2D ThinWallJunction = FVector2D::ZeroVector;

	/** Where a route round a thin wall starts and ends. */
	struct FThinWallRoute
	{
		const TCHAR* Name;
		FVector2D Start;
		FVector2D Goal;
	};

	/**
	 * The routes: round the free wall's end the way L_LongPath's first layout had the M20 drive it and back, a hairpin from
	 * one side of the wall to the other, and round the junction and back.
	 */
	TArray<FThinWallRoute> GetThinWallRoutes()
	{
		return {
			{ TEXT("round the free wall's end"), ThinWallEnd + FVector2D(-3400.f, 3000.f), ThinWallEnd + FVector2D(2100.f, -3000.f) },
			{ TEXT("back round the free wall's end"), ThinWallEnd + FVector2D(2100.f, -3000.f), ThinWallEnd + FVector2D(-3400.f, 3000.f) },
			{ TEXT("in a hairpin round the free wall's end"), ThinWallEnd + FVector2D(-2000.f, -1500.f), ThinWallEnd + FVector2D(2000.f, -1500.f) },
			{ TEXT("round the junction"), ThinWallJunction + FVector2D(-3400.f, 2100.f), ThinWallJunction + FVector2D(1100.f, -3400.f) },
			{ TEXT("back round the junction"), ThinWallJunction + FVector2D(1100.f, -3400.f), ThinWallJunction + FVector2D(-3400.f, 2100.f) },
		};
	}

	bool OpenThinWallsMap(FAutomationTestBase& Test, const TSharedRef<FNavState>& State)
	{
		if (!AutomationOpenMap(ThinWallsMap))
		{
			Test.AddError(FString::Printf(TEXT("Could not open %s"), ThinWallsMap));
			return false;
		}
		const UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const FBox2D Free = World ? RTSTests::GetLabelledBox(*World, TEXT("Wall_Free")) : FBox2D(ForceInit);
		const FBox2D North = World ? RTSTests::GetLabelledBox(*World, TEXT("Wall_JunctionNorth")) : FBox2D(ForceInit);
		const FBox2D East = World ? RTSTests::GetLabelledBox(*World, TEXT("Wall_JunctionEast")) : FBox2D(ForceInit);
		LongVehicleSize = GetLongVehicleSize();
		if (!Free.bIsValid || !North.bIsValid || !East.bIsValid || LongVehicleSize.X <= 0.f)
		{
			Test.AddError(TEXT("Missing test data, or L_ThinWalls lacks its walls"));
			return false;
		}
		ThinWallEnd = FVector2D(Free.GetCenter().X, Free.Max.Y);
		ThinWallEndCorner = Free.Max;
		ThinWallJunction = FVector2D(East.GetCenter().X, North.GetCenter().Y);
		ArrivalRadius = LongVehicleSize.X;
		return State->LoadUnits(Test) && Test.TestNotNull(TEXT("Flak truck data"), LoadObject<URTSUnitData>(nullptr, FlakTruckPath));
	}

	/** The hulls of the thin-wall drives: the tracked M20 and placeholder tank, and the wheeled flak truck. */
	TArray<URTSUnitData*> GetThinWallHulls(const FNavState& State)
	{
		return { State.M20, State.Tank, LoadObject<URTSUnitData>(nullptr, FlakTruckPath) };
	}
}

/**
 * The first hull fit of a session (#173): before anything has found out which side of its walls the navmesh lies on, the
 * M20's path round the free wall's end keeps its corner off the wall's north-east corner, where the hull clears the wall
 * at every heading it turns through. Read inside out, the corner moved 2 m toward the wall, and the way to it ran the hull
 * into the wall's end.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSNavCheckFirstHullFit, FAutomationTestBase*, Test, TSharedRef<RTSNavigationTests::FNavState>, State);
bool FRTSNavCheckFirstHullFit::Update()
{
	using namespace RTSNavigationTests;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	const FRTSUnitShape Shape = RTSUnitShape::FromData(*State->M20);
	const ARecastNavMesh* NavMesh = World ? RTSNavAgents::GetNavMesh(*World, RTSNavAgents::SelectAgent(Shape)) : nullptr;
	if (!Test->TestNotNull(TEXT("The M20's navmesh"), NavMesh))
	{
		return true;
	}
	// The navmesh's corner off the wall's corner, where its edges meet.
	const float Erosion = RTSHullClearance::GetErosion(*NavMesh);
	const FVector Extent(50.f, 50.f, 250.f);
	const FThinWallRoute& Route = GetThinWallRoutes()[0];
	FNavLocation Corner;
	FNavLocation Start;
	FNavLocation Goal;
	if (!Test->TestTrue(TEXT("The route and the corner off the wall's end lie on the navmesh"),
		NavMesh->ProjectPoint(FVector(ThinWallEndCorner + FVector2D(Erosion), 0.f), Corner, Extent)
		&& NavMesh->ProjectPoint(FVector(Route.Start, 0.f), Start, Extent) && NavMesh->ProjectPoint(FVector(Route.Goal, 0.f), Goal, Extent)))
	{
		return true;
	}

	RTSHullClearance::ForgetInsideSign();
	TArray<FNavPathPoint> Points = { FNavPathPoint(Start.Location, Start.NodeRef), FNavPathPoint(Corner.Location, Corner.NodeRef),
		FNavPathPoint(Goal.Location, Goal.NodeRef) };
	RTSHullClearance::AdjustPath(*NavMesh, Shape, Points);
	const float Moved = Points.Num() == 3 ? FVector::Dist2D(Points[1].Location, Corner.Location) : FLT_MAX;
	Test->TestTrue(*FString::Printf(TEXT("The corner off the wall's end stays where the navmesh puts it (moved %.0f cm)"), Moved),
		Moved <= RTSHullClearance::GetNavUncertainty(*NavMesh));

	// The gear's and avoidance's first readings start the same way.
	for (const float Yaw : { -40.f, -60.f })
	{
		RTSHullClearance::ForgetInsideSign();
		const FVector2D Heading(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
		const float Clearance = RTSHullClearance::GetClearance(*NavMesh, RTSHullClearance::MakeFootprint(Shape, Corner.Location, Heading));
		Test->TestTrue(*FString::Printf(TEXT("The M20 on that corner at %.0f degrees reads clear of the wall (%.1f cm)"), Yaw, Clearance),
			Clearance >= 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavThinWallsFirstFitTest, "WorldDivide.Navigation.ThinWalls.FirstFit", RTSTests::TestFlags)

bool FRTSNavThinWallsFirstFitTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenThinWallsMap(*this, State))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavCheckFirstHullFit(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/** Forgets which side of its walls the navmesh lies on, so the next drive fits its path as the first of a session does. */
DEFINE_LATENT_AUTOMATION_COMMAND(FRTSNavForgetInsideSign);
bool FRTSNavForgetInsideSign::Update()
{
	RTSHullClearance::ForgetInsideSign();
	return true;
}

/**
 * Drives round 2 m walls on L_ThinWalls (#173), from a session's first hull fit on: the tracked M20 and placeholder tank
 * and the wheeled flak truck, each alone on its own move, round the free wall's end both ways, in a hairpin round it and
 * round the junction both ways; then a group of six, two of each, round the end on one order. Each arrives without its
 * move given up (no refusal reaches the player), never comes more than 2 cm into a wall and does not keep switching
 * between forward and reverse.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNavThinWallsDriveTest, "WorldDivide.Navigation.ThinWalls.Drive", RTSTests::TestFlags)

bool FRTSNavThinWallsDriveTest::RunTest(const FString& Parameters)
{
	using namespace RTSNavigationTests;
	const TSharedRef<FNavState> State = MakeShared<FNavState>();
	if (!OpenThinWallsMap(*this, State))
	{
		return false;
	}
	const TArray<URTSUnitData*> Hulls = GetThinWallHulls(*State);
	const TArray<FThinWallRoute> Routes = GetThinWallRoutes();
	for (const FThinWallRoute& Route : Routes)
	{
		for (URTSUnitData* Data : Hulls)
		{
			FDrive Drive;
			Drive.Name = FString::Printf(TEXT("%s %s"), *Data->GetName(), Route.Name);
			Drive.Target = FVector(Route.Goal, 0.f);
			Drive.bCheckReversals = true;
			const FVector2D Way = Route.Goal - Route.Start;
			Drive.Units.Add({ Data, FVector(Route.Start, 0.f), static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Way.Y, Way.X))) });
			State->Drives.Add(Drive);
		}
	}
	FDrive Group;
	Group.Name = TEXT("A group of six round the free wall's end");
	Group.Target = FVector(Routes[0].Goal, 0.f);
	Group.bGroupOrder = true;
	Group.bCheckReversals = true;
	const float Pitch = 1.2f * LongVehicleSize.X;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const FVector2D Offset(-(Index / 3) * Pitch, (Index % 3 - 1) * 0.5f * Pitch);
		Group.Units.Add({ Hulls[Index % Hulls.Num()], FVector(Routes[0].Start + Offset, 0.f), 0.f });
	}
	State->Drives.Add(Group);

	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavWaitForNavigation(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavForgetInsideSign());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSNavRunDrives(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
