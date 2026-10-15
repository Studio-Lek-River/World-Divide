#include "Navigation/RTSNavAgents.h"

#include "Data/RTSUnitData.h"
#include "Engine/World.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Units/RTSUnitShape.h"
#include "WorldDivide.h"

namespace RTSNavAgents
{
	const TArray<FNavDataConfig>& GetAgents()
	{
		// The config does not change while the game runs.
		static const TArray<FNavDataConfig> Agents = []
		{
			TArray<FNavDataConfig> Sorted = GetDefault<UNavigationSystemV1>()->GetSupportedAgents();
			Sorted.RemoveAll([](const FNavDataConfig& Agent) { return !Agent.IsValid() || Agent.bCanFly; });
			if (Sorted.IsEmpty())
			{
				Sorted.Add(FNavDataConfig());
			}
			Sorted.StableSort([](const FNavDataConfig& A, const FNavDataConfig& B) { return A.AgentRadius < B.AgentRadius; });
			return Sorted;
		}();
		return Agents;
	}

	const FNavDataConfig& SelectAgent(float HalfWidth)
	{
		const TArray<FNavDataConfig>& Agents = GetAgents();
		for (const FNavDataConfig& Agent : Agents)
		{
			if (Agent.AgentRadius >= HalfWidth)
			{
				return Agent;
			}
		}
		UE_LOG(LogWorldDivide, Display, TEXT("A hull %.0f cm wide is wider than every nav agent; it walks the %s navmesh"),
			HalfWidth * 2.f, *Agents.Last().Name.ToString());
		return Agents.Last();
	}

	const FNavDataConfig& SelectAgent(const FRTSUnitShape& Shape)
	{
		// Units drive forward, so the width across the hull is what has to fit between two walls.
		return SelectAgent(Shape.GetHalfWidth());
	}

	const FNavDataConfig& SelectAgent(const URTSUnitData& Data)
	{
		return SelectAgent(RTSUnitShape::FromData(Data));
	}

	const FNavDataConfig& GetNarrowestAgent()
	{
		return GetAgents()[0];
	}

	const ARecastNavMesh* GetNavMesh(const UWorld& World, const FNavAgentProperties& Agent)
	{
		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		return NavSystem ? Cast<ARecastNavMesh>(NavSystem->GetNavDataForProps(Agent)) : nullptr;
	}
}
