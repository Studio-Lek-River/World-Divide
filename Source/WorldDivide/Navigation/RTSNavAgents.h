#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"

class ANavigationData;
class ARecastNavMesh;
class UWorld;
class URTSUnitData;
struct FRTSUnitShape;

/**
 * The navigation agent classes (NavigationSystemV1 SupportedAgents in Config/DefaultEngine.ini) and which one a unit walks.
 * Each class is its own navmesh, eroded by the class's radius, so a path on it is wide enough for every unit no wider
 * than that. A unit takes the narrowest class at least as wide as its hull; its length, and the part of its width the
 * class does not cover, are kept clear by RTSHullClearance. Nothing here names a class: adding one is a config line.
 */
namespace RTSNavAgents
{
	/** The configured classes, narrowest first. Never empty: without any configured, the engine's default agent. */
	WORLDDIVIDE_API const TArray<FNavDataConfig>& GetAgents();

	/** The narrowest class whose radius covers HalfWidth, or the widest class when none does. */
	WORLDDIVIDE_API const FNavDataConfig& SelectAgent(float HalfWidth);

	/** SelectAgent for a hull: half its width across, the way it drives. */
	WORLDDIVIDE_API const FNavDataConfig& SelectAgent(const FRTSUnitShape& Shape);

	/** The class a unit of Data walks, sized from its data alone, before it spawns. */
	WORLDDIVIDE_API const FNavDataConfig& SelectAgent(const URTSUnitData& Data);

	/** The narrowest class: the one buildings are placed on and that every unit's navmesh is cut from. */
	WORLDDIVIDE_API const FNavDataConfig& GetNarrowestAgent();

	/** World's navmesh for Agent, or null when the world has none for it (yet). */
	WORLDDIVIDE_API const ARecastNavMesh* GetNavMesh(const UWorld& World, const FNavAgentProperties& Agent);
}
