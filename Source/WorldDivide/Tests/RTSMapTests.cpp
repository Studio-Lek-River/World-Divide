#include "Core/RTSSettings.h"
#include "Data/RTSMapData.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/RecastNavMesh.h"
#include "Tests/RTSTestFlags.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
}

/**
 * Both generated maps are saved with their navmesh built, so a map the editor did not start with, or one loaded in a
 * packaged build, has a full navmesh from its first frame.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSavedNavMeshTest, "WorldDivide.Maps.SavedNavMesh", TestFlags)

bool FRTSSavedNavMeshTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* MapPath : { TEXT("/Game/Maps/L_TestMap"), TEXT("/Game/Maps/L_Skirmish") })
	{
		UPackage* Package = LoadPackage(nullptr, MapPath, LOAD_None);
		const UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), MapPath), World) || !World->PersistentLevel)
		{
			continue;
		}

		int32 Tiles = 0;
		for (const AActor* Actor : World->PersistentLevel->Actors)
		{
			if (const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(Actor))
			{
				Tiles += NavMesh->GetNavMeshTilesCount();
			}
		}
		TestTrue(FString::Printf(TEXT("%s is saved with a built navmesh"), MapPath), Tiles > 0);
	}
	return true;
}

/**
 * Every lobby map holds a preview and, for each PlayerStart tagged Team<N> in its level, the start spot's position on the
 * preview at index N.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMapStartSpotPositionsTest, "WorldDivide.Maps.StartSpotPositions", TestFlags)

bool FRTSMapStartSpotPositionsTest::RunTest(const FString& Parameters)
{
	const TArray<TSoftObjectPtr<URTSMapData>>& Maps = GetDefault<URTSSettings>()->SkirmishMaps;
	TestTrue(TEXT("The lobby offers maps"), Maps.Num() > 0);
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

		TMap<int32, FVector> Starts;
		for (const AActor* Actor : World->PersistentLevel->Actors)
		{
			const APlayerStart* Start = Cast<APlayerStart>(Actor);
			const FString Tag = Start ? Start->PlayerStartTag.ToString() : FString();
			if (Tag.StartsWith(TEXT("Team")))
			{
				Starts.Add(FCString::Atoi(*Tag.Mid(4)), Start->GetActorLocation());
			}
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

#endif
