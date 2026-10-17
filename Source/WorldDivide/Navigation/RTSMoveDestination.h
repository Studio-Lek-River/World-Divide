#pragma once

#include "CoreMinimal.h"

class APawn;

/** Where a move order ends when the clicked spot has no navmesh under it: inside a wall, a building or the map's edge. */
namespace RTSMoveDestination
{
	/**
	 * The spot of Agent's navmesh nearest Target in any direction that Agent reaches by a full path. Among spots nearly as
	 * near as the nearest (within a slack of a few metres, so both faces of a wall count) the one with the shortest path
	 * wins, which keeps a unit on its own side of a block. False when none of them is reachable: Target lies in an enclosed
	 * area or on another navmesh island, and the order is refused instead of sending the unit round the outside.
	 *
	 * Bounded: box queries doubling up to the navmesh bounds, each capped by Recast at 256 polygons, then at most a handful
	 * of path searches, each searched once more with a raised node limit when it runs out of nodes (RTSPathSearch). Meant for
	 * once per order, never per frame.
	 */
	WORLDDIVIDE_API bool FindReachable(const APawn& Agent, const FVector& Target, FVector& OutDestination);
}
