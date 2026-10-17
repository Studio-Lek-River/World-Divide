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
		/** Searches run: 1 for an ordinary path, 2 when the first ran out of nodes; 1 when an FRaisedStart started it raised. */
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

	/**
	 * The navigation data on which a search of one group order already ran out of the default node limit (#174). While an
	 * FRaisedStart for it is alive, FindFullPath starts searches on that data at the raised limit, sparing the first search
	 * that would run out again, and adds the data of any search that did run out. The outcome and the path are the ones the
	 * two searches would have given: A* with more nodes ends where it would have, only it does not stop at 2048.
	 */
	using FRaisedNavData = TSet<TWeakObjectPtr<const ANavigationData>>;

	class WORLDDIVIDE_API FRaisedStart
	{
	public:
		explicit FRaisedStart(FRaisedNavData& NavData);
		~FRaisedStart();
		FRaisedStart(const FRaisedStart&) = delete;
		FRaisedStart& operator=(const FRaisedStart&) = delete;

	private:
		FRaisedNavData* Previous = nullptr;
	};

	/** Since the process started, for tests and benches: every search FindFullPath ran, and those an FRaisedStart started raised. */
	struct FCounters
	{
		int64 Searches = 0;
		int64 RaisedStarts = 0;
	};
	WORLDDIVIDE_API const FCounters& GetCounters();
}
