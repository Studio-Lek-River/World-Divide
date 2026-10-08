#include "Engine/Level.h"
#include "Engine/World.h"
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

#endif
