#pragma once

#include "CoreMinimal.h"

class APawn;

/** Where a unit stands to reach a building or supply source, whose centre lies in its own navmesh hole. */
namespace RTSApproach
{
	/**
	 * A navmesh spot about StandOff outside the axis-aligned footprint at Center that Agent reaches by a full path and that lies
	 * within MaxDistance of the footprint. Tries the spot facing the agent first, unless the agent stands inside the footprint,
	 * then the rest of the ring around the footprint, nearest first. False when none qualifies.
	 */
	WORLDDIVIDE_API bool FindReachableSpot(const APawn& Agent, const FVector& Center, const FIntPoint& Footprint, float StandOff,
		float MaxDistance, FVector& OutSpot);
}
