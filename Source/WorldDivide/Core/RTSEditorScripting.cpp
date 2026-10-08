#include "Core/RTSEditorScripting.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

int32 URTSEditorScripting::BuildNavigation(UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return 0;
	}
	// A commandlet's editor world comes without a navigation system.
	if (!World->GetNavigationSystem())
	{
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);
	}
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem)
	{
		return 0;
	}
	// Spawns the navmesh if the world has none yet, rebuilds every tile and blocks until done.
	NavSystem->Build();
	const ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavSystem->GetDefaultNavDataInstance());
	return NavMesh ? NavMesh->GetNavMeshTilesCount() : 0;
}
