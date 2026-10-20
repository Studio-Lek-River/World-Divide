#include "Core/RTSEditorScripting.h"
#include "Core/RTSMaps.h"
#include "Core/RTSSettings.h"
#include "Core/RTSStartSpot.h"
#include "Data/RTSMapData.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/RTSNavAgents.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestMaps.h"
#include "Tests/RTSTestWorld.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;

	/** The positions of the Team<N> PlayerStarts of a level by N; its start spot problems go to OutProblems. */
	TMap<int32, FVector> FindStartSpots(const UWorld& World, TArray<FString>* OutProblems = nullptr)
	{
		TMap<int32, FVector> Starts;
		for (const TPair<int32, const APlayerStart*>& Start : RTSMaps::FindStartSpots(World, OutProblems))
		{
			Starts.Add(Start.Key, Start.Value->GetActorLocation());
		}
		return Starts;
	}
}

/**
 * An ARTSStartSpot tags itself Team<SpotIndex>, and RTSMaps::FindStartSpots reports a spot index used twice and a spot index left out.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSStartSpotActorTest, "WorldDivide.Maps.StartSpotActor", TestFlags)

bool FRTSStartSpotActorTest::RunTest(const FString& Parameters)
{
	const RTSTests::FRTSTestWorld TestWorld;
	UWorld& World = *TestWorld.World;
	const auto SpawnSpot = [&World](int32 SpotIndex, const FVector& Location)
	{
		const FTransform Transform(Location);
		ARTSStartSpot* Spot = World.SpawnActorDeferred<ARTSStartSpot>(ARTSStartSpot::StaticClass(), Transform);
		Spot->SpotIndex = SpotIndex;
		Spot->FinishSpawning(Transform);
		return Spot;
	};

	const ARTSStartSpot* Second = SpawnSpot(1, FVector(1000.0, 0.0, 0.0));
	const ARTSStartSpot* First = SpawnSpot(0, FVector(-1000.0, 0.0, 0.0));
	TestEqual(TEXT("Spot index 0 is tagged Team0"), First->PlayerStartTag, FName(TEXT("Team0")));
	TestEqual(TEXT("Spot index 1 is tagged Team1"), Second->PlayerStartTag, FName(TEXT("Team1")));

	TArray<FString> Problems;
	TMap<int32, const APlayerStart*> Starts = RTSMaps::FindStartSpots(World, &Problems);
	TestEqual(TEXT("Two spots are found"), Starts.Num(), 2);
	TestTrue(TEXT("Spot 0 is the actor with index 0"), Starts.FindRef(0) == First);
	TestTrue(TEXT("Spot 1 is the actor with index 1"), Starts.FindRef(1) == Second);
	TestEqual(TEXT("Two numbered spots have no problems"), Problems.Num(), 0);

	// A second actor with index 1: the first one keeps the spot and the duplicate is reported.
	SpawnSpot(1, FVector(0.0, 1000.0, 0.0));
	Problems.Reset();
	Starts = RTSMaps::FindStartSpots(World, &Problems);
	TestTrue(TEXT("The first actor with index 1 keeps spot 1"), Starts.FindRef(1) == Second);
	TestEqual(TEXT("A duplicate spot index is reported"), Problems.Num(), 1);

	// Index 3 with no index 2: the gap is reported next to the duplicate.
	SpawnSpot(3, FVector(0.0, -1000.0, 0.0));
	Problems.Reset();
	RTSMaps::FindStartSpots(World, &Problems);
	TestEqual(TEXT("A duplicate and a gap are reported"), Problems.Num(), 2);
	TestTrue(TEXT("The gap names spot 3 (Team2)"), Problems.ContainsByPredicate([](const FString& Problem)
	{
		return Problem.Contains(TEXT("Team2"));
	}));
	return true;
}

/**
 * L_TestMap, L_Bench, every map the lobby offers and the test maps are saved with a built navmesh for every nav agent class, so a map the
 * editor did not start with, or one loaded in a packaged build, has full navmeshes from its first frame.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSavedNavMeshTest, "WorldDivide.Maps.SavedNavMesh", TestFlags)

bool FRTSSavedNavMeshTest::RunTest(const FString& Parameters)
{
	TArray<FString> MapPaths = { TEXT("/Game/Maps/L_TestMap"), TEXT("/Game/Maps/L_Bench") };
	for (const TSoftObjectPtr<URTSMapData>& MapPtr : RTSTests::GetCheckedMaps())
	{
		const URTSMapData* Map = MapPtr.LoadSynchronous();
		if (TestNotNull(FString::Printf(TEXT("%s loads"), *MapPtr.ToString()), Map))
		{
			MapPaths.Add(Map->Map.GetLongPackageName());
		}
	}
	for (const FString& MapPath : MapPaths)
	{
		UPackage* Package = LoadPackage(nullptr, *MapPath, LOAD_None);
		const UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), *MapPath), World) || !World->PersistentLevel)
		{
			continue;
		}

		// One navmesh per nav agent class, each with its tiles.
		for (const FNavDataConfig& Agent : RTSNavAgents::GetAgents())
		{
			int32 Tiles = 0;
			for (const AActor* Actor : World->PersistentLevel->Actors)
			{
				const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(Actor);
				if (NavMesh && NavMesh->GetConfig().Name == Agent.Name)
				{
					Tiles += NavMesh->GetNavMeshTilesCount();
				}
			}
			TestTrue(FString::Printf(TEXT("%s is saved with a built %s navmesh"), *MapPath, *Agent.Name.ToString()), Tiles > 0);
		}
	}
	return true;
}

/**
 * Every lobby map and test map holds a preview and, for each PlayerStart tagged Team<N> in its level, the start spot's position on the
 * preview at index N.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMapStartSpotPositionsTest, "WorldDivide.Maps.StartSpotPositions", TestFlags)

bool FRTSMapStartSpotPositionsTest::RunTest(const FString& Parameters)
{
	const TArray<TSoftObjectPtr<URTSMapData>> Maps = RTSTests::GetCheckedMaps();
	for (const TSoftObjectPtr<URTSMapData>& MapPtr : Maps)
	{
		const URTSMapData* Map = MapPtr.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), *MapPtr.ToString()), Map))
		{
			continue;
		}
		const FString Name = Map->GetName();
		TestNotNull(FString::Printf(TEXT("%s has a preview"), *Name), Cast<UTexture2D>(Map->Preview.TryLoad()));

		UPackage* Package = LoadPackage(nullptr, *Map->Map.GetLongPackageName(), LOAD_None);
		const UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s: its level loads"), *Name), World) || !World->PersistentLevel)
		{
			continue;
		}

		TArray<FString> Problems;
		const TMap<int32, FVector> Starts = FindStartSpots(*World, &Problems);
		for (const FString& Problem : Problems)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *Name, *Problem));
		}
		TestEqual(FString::Printf(TEXT("%s: a PlayerStart per start spot"), *Name), Starts.Num(), Map->StartSpots);
		TestEqual(FString::Printf(TEXT("%s: a position per start spot"), *Name), Map->StartSpotPositions.Num(), Map->StartSpots);
		for (const TPair<int32, FVector>& Start : Starts)
		{
			if (!TestTrue(FString::Printf(TEXT("%s: Team%d has a position"), *Name, Start.Key),
				Map->StartSpotPositions.IsValidIndex(Start.Key)))
			{
				continue;
			}
			const FVector2D Expected = Map->ToPreviewPosition(Start.Value);
			const FVector2D Position = Map->StartSpotPositions[Start.Key];
			TestTrue(FString::Printf(TEXT("%s: Team%d is at %s on the preview, not %s"), *Name, Start.Key, *Expected.ToString(),
				*Position.ToString()), Position.Equals(Expected, 0.001));
			TestTrue(FString::Printf(TEXT("%s: Team%d is on the preview"), *Name, Start.Key),
				Position.X >= 0.0 && Position.X <= 1.0 && Position.Y >= 0.0 && Position.Y <= 1.0);
		}
	}
	return true;
}

/**
 * The camera that renders a map's preview sees each start spot where the lobby puts its marker: projected the way a scene
 * capture projects (view axes swapped to x right, y up, orthographic over OrthoWidth), every Team<N> PlayerStart lands on
 * StartSpotPositions[N].
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMapPreviewCameraTest, "WorldDivide.Maps.PreviewCameraAlignment", TestFlags)

bool FRTSMapPreviewCameraTest::RunTest(const FString& Parameters)
{
	const TArray<TSoftObjectPtr<URTSMapData>> Maps = RTSTests::GetCheckedMaps();
	for (const TSoftObjectPtr<URTSMapData>& MapPtr : Maps)
	{
		const URTSMapData* Map = MapPtr.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), *MapPtr.ToString()), Map))
		{
			continue;
		}
		const FString Name = Map->GetName();
		UPackage* Package = LoadPackage(nullptr, *Map->Map.GetLongPackageName(), LOAD_None);
		const UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s: its level loads"), *Name), World) || !World->PersistentLevel)
		{
			continue;
		}

		FVector Location;
		FRotator Rotation;
		double OrthoWidth;
		URTSEditorScripting::GetPreviewCamera(Map->PreviewArea, Location, Rotation, OrthoWidth);
		const FMatrix View = FTranslationMatrix(-Location) * FInverseRotationMatrix(Rotation)
			* FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));

		const TMap<int32, FVector> Starts = FindStartSpots(*World);
		TestTrue(FString::Printf(TEXT("%s has start spots"), *Name), Starts.Num() > 0);
		for (const TPair<int32, FVector>& Start : Starts)
		{
			if (!Map->StartSpotPositions.IsValidIndex(Start.Key))
			{
				continue;
			}
			const FVector InView = View.TransformPosition(Start.Value);
			TestTrue(FString::Printf(TEXT("%s: Team%d is in front of the camera"), *Name, Start.Key), InView.Z > 0.0);
			const FVector2D OnImage(0.5 + InView.X / OrthoWidth, 0.5 - InView.Y / OrthoWidth);
			const FVector2D Marker = Map->StartSpotPositions[Start.Key];
			TestTrue(FString::Printf(TEXT("%s: Team%d is at %s on the rendered preview, its marker at %s"), *Name, Start.Key,
				*OnImage.ToString(), *Marker.ToString()), OnImage.Equals(Marker, 0.001));
		}
	}
	return true;
}

/**
 * Every lobby map and test map's preview covers its whole level, as bake_map.py bakes it (#289): its PreviewArea contains the
 * level's NavMeshBoundsVolumes, the area the camera pans over, and every start spot.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMapPreviewAreaTest, "WorldDivide.Maps.PreviewArea", TestFlags)

bool FRTSMapPreviewAreaTest::RunTest(const FString& Parameters)
{
	for (const TSoftObjectPtr<URTSMapData>& MapPtr : RTSTests::GetCheckedMaps())
	{
		const URTSMapData* Map = MapPtr.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), *MapPtr.ToString()), Map))
		{
			continue;
		}
		const FString Name = Map->GetName();
		UPackage* Package = LoadPackage(nullptr, *Map->Map.GetLongPackageName(), LOAD_None);
		const UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s: its level loads"), *Name), World) || !World->PersistentLevel)
		{
			continue;
		}

		// A centimetre of slack for the float round trip through the data asset.
		const FBox2D Area = Map->PreviewArea.ExpandBy(1.0);
		bool bHasVolume = false;
		for (const AActor* Actor : World->PersistentLevel->Actors)
		{
			if (const ANavMeshBoundsVolume* Volume = Cast<ANavMeshBoundsVolume>(Actor))
			{
				bHasVolume = true;
				const FBox Bounds = Volume->GetComponentsBoundingBox(true);
				TestTrue(FString::Printf(TEXT("%s: the preview area %s contains %s (%s to %s)"), *Name, *Map->PreviewArea.ToString(),
					*Volume->GetName(), *FVector2D(Bounds.Min).ToString(), *FVector2D(Bounds.Max).ToString()),
					Area.IsInsideOrOn(FVector2D(Bounds.Min)) && Area.IsInsideOrOn(FVector2D(Bounds.Max)));
			}
		}
		TestTrue(FString::Printf(TEXT("%s: its level has a NavMeshBoundsVolume"), *Name), bHasVolume);

		for (const TPair<int32, FVector>& Start : FindStartSpots(*World))
		{
			TestTrue(FString::Printf(TEXT("%s: the preview area %s contains Team%d at %s"), *Name, *Map->PreviewArea.ToString(), Start.Key,
				*FVector2D(Start.Value).ToString()), Area.IsInsideOrOn(FVector2D(Start.Value)));
		}
	}
	return true;
}

/** The lobby offers only real maps, and starts on one of them: none of the test maps is among them. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLobbyMapsTest, "WorldDivide.Maps.LobbyMaps", TestFlags)

bool FRTSLobbyMapsTest::RunTest(const FString& Parameters)
{
	const TArray<TSoftObjectPtr<URTSMapData>>& Maps = GetDefault<URTSSettings>()->SkirmishMaps;
	if (!TestTrue(TEXT("The lobby offers maps"), Maps.Num() > 0))
	{
		return true;
	}
	TestNotNull(FString::Printf(TEXT("The first, %s, loads"), *Maps[0].ToString()), Maps[0].LoadSynchronous());
	for (const TSoftObjectPtr<URTSMapData>& TestMap : RTSTests::GetTestSkirmishMaps())
	{
		TestFalse(FString::Printf(TEXT("The lobby does not offer %s"), *TestMap.GetAssetName()), Maps.Contains(TestMap));
	}
	return true;
}

#endif
