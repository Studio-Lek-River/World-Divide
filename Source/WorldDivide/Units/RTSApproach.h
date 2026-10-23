#pragma once

#include "CoreMinimal.h"

class APawn;
struct FRTSBuildingFootprint;

/** Where a unit stands to reach a building or supply source, whose centre lies in its own navmesh hole. */
namespace RTSApproach
{
	/**
	 * A navmesh spot about StandOff outside Footprint, turned however it is, that Agent reaches by a full path and that lies
	 * within MaxDistance of the footprint. Tries the spot facing the agent first, unless the agent stands inside the footprint,
	 * then the rest of the ring around the footprint, nearest first. False when none qualifies.
	 */
	WORLDDIVIDE_API bool FindReachableSpot(const APawn& Agent, const FRTSBuildingFootprint& Footprint, float StandOff,
		float MaxDistance, FVector& OutSpot);
}
