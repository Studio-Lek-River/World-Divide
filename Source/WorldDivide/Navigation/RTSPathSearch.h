#pragma once

#include "CoreMinimal.h"
#include "NavigationData.h"

class UNavigationSystemV1;

/**
 * Path searches that tell a long path from an unreachable goal (#169). Recast's A* stops at the query filter's node limit
 * (2048 by default) and hands back a partial path flagged DidSearchReachedLimit; a partial path without that flag means the
 * search ran out of navmesh first, so the goal is cut off: another island, an enclosed area.
 */
namespace RTSPathSearch
{
	enum class EOutcome : uint8
	{
		/** A full path to the goal. */
		Full,
		/** Proven: the search covered everything reachable from the start without meeting the goal. */
		Unreachable,
		/** Even the largest node limit (rts.Nav.MaxSearchNodes) ran out before the search ended: reachability is unknown. */
		Inconclusive,
		/** No search ran: no navmesh under the start, or no navigation data. */
		Error,
	};

	struct FStats
	{
		EOutcome Outcome = EOutcome::Error;
		/** Searches run: 1 for an ordinary path, 2 when the first ran out of nodes. */
		int32 Searches = 0;
		/** The node limit of the last search. */
		uint32 MaxSearchNodes = 0;
	};

	/** The node limit a search gets once the filter's own ran out: rts.Nav.MaxSearchNodes, never below the engine default. */
	WORLDDIVIDE_API uint32 GetMaxSearchNodes();

	/**
	 * FindPathSync with Query as given; when that search runs out of nodes, once more with the node limit raised to
	 * GetMaxSearchNodes. Ordinary paths cost exactly what they cost before; only a search that would have ended partial for
	 * lack of nodes is repeated. Synchronous and bounded by that limit: meant for once per order, never per frame.
	 */
	WORLDDIVIDE_API FPathFindingResult FindFullPath(UNavigationSystemV1& NavSystem, const FPathFindingQuery& Query, FStats* OutStats = nullptr);

	/**
	 * Gives Path the raised node limit for the searches the navigation system runs again when the navmesh under it changes,
	 * so a long path a new building forces round does not come back partial for lack of nodes.
	 */
	WORLDDIVIDE_API void KeepLimitOnRepath(FNavigationPath& Path);
}
