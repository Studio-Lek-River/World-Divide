#include "Navigation/RTSPathSearch.h"

#include "HAL/IConsoleManager.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "WorldDivide.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMaxSearchNodes(
		TEXT("rts.Nav.MaxSearchNodes"), 16384,
		TEXT("Node limit of the second path search a move order gets when the first ran out of Recast's 2048 nodes (#169). ")
		TEXT("A connected navmesh with fewer polygons than this always gets a conclusive answer. At most 65534."));

	/** Detour indexes its node pool with 16 bits, one value of which marks no node. */
	constexpr int32 DetourNodeCeiling = 65534;

	FString DescribeQuery(const FPathFindingQuery& Query)
	{
		return FString::Printf(TEXT("%s from %s to %s"), *GetNameSafe(Query.Owner.Get()), *Query.StartLocation.ToCompactString(),
			*Query.EndLocation.ToCompactString());
	}

	FSharedConstNavQueryFilter WithLimit(const FSharedConstNavQueryFilter& Filter, uint32 Limit)
	{
		FSharedNavQueryFilter Raised = Filter->GetCopy();
		Raised->SetMaxSearchNodes(Limit);
		return Raised;
	}

	bool RanOutOfNodes(const FPathFindingResult& Result)
	{
		return Result.Path.IsValid() && Result.Path->IsPartial() && Result.Path->DidSearchReachedLimit();
	}
}

namespace RTSPathSearch
{
	uint32 GetMaxSearchNodes()
	{
		return static_cast<uint32>(FMath::Clamp(CVarMaxSearchNodes.GetValueOnGameThread(), RECAST_MAX_SEARCH_NODES, DetourNodeCeiling));
	}

	FPathFindingResult FindFullPath(UNavigationSystemV1& NavSystem, const FPathFindingQuery& Query, FStats* OutStats)
	{
		FStats Stats;
		Stats.MaxSearchNodes = Query.QueryFilter.IsValid() ? Query.QueryFilter->GetMaxSearchNodes() : 0;
		FPathFindingResult Result = NavSystem.FindPathSync(Query);
		Stats.Searches = 1;

		const uint32 Limit = GetMaxSearchNodes();
		if (RanOutOfNodes(Result) && Query.QueryFilter.IsValid() && Stats.MaxSearchNodes < Limit)
		{
			UE_LOG(LogWorldDivide, Verbose, TEXT("Path search %s ran out of its %u nodes; searching again with up to %u"),
				*DescribeQuery(Query), Stats.MaxSearchNodes, Limit);
			FPathFindingQuery Raised(Query);
			Raised.QueryFilter = WithLimit(Query.QueryFilter, Limit);
			Result = NavSystem.FindPathSync(Raised);
			Stats.Searches = 2;
			Stats.MaxSearchNodes = Limit;
			if (Result.IsSuccessful() && !Result.IsPartial())
			{
				UE_LOG(LogWorldDivide, Verbose, TEXT("Path search %s found a full path of %d points within %u nodes"),
					*DescribeQuery(Query), Result.Path->GetPathPoints().Num(), Limit);
			}
		}

		if (Result.Path.IsValid() && !Result.IsPartial() && Result.IsSuccessful())
		{
			Stats.Outcome = EOutcome::Full;
		}
		else if (RanOutOfNodes(Result))
		{
			// Not a proven dead end: the order is refused, but the log says why, so a map that outgrows the limit shows up.
			Stats.Outcome = EOutcome::Inconclusive;
			UE_LOG(LogWorldDivide, Warning, TEXT("Path search %s ran out of %u nodes, the most rts.Nav.MaxSearchNodes allows: reachability unknown"),
				*DescribeQuery(Query), Stats.MaxSearchNodes);
		}
		else if (Result.Path.IsValid() && Result.Path->IsPartial())
		{
			Stats.Outcome = EOutcome::Unreachable;
			UE_LOG(LogWorldDivide, Verbose, TEXT("Path search %s covered everything reachable without meeting the goal: unreachable"),
				*DescribeQuery(Query));
		}
		else
		{
			Stats.Outcome = EOutcome::Error;
		}

		if (OutStats)
		{
			*OutStats = Stats;
		}
		return Result;
	}

	void KeepLimitOnRepath(FNavigationPath& Path)
	{
		const FSharedConstNavQueryFilter Filter = Path.GetFilter();
		const uint32 Limit = GetMaxSearchNodes();
		if (Filter.IsValid() && Filter->GetMaxSearchNodes() < Limit)
		{
			Path.SetFilter(WithLimit(Filter, Limit));
		}
	}
}
