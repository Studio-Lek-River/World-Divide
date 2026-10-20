#pragma once

#include "CoreMinimal.h"
#include "Chaos/ChaosEngineInterface.h"

class UWorld;

/** The ground units stand on and aircraft fly over: the level's static geometry, never buildings, supply sources or units. */
namespace RTSGround
{
	/**
	 * Traces straight down at Location's XY, from Up above Location to Down below it, against world-static geometry only.
	 * Returns false when there is no ground in that span, leaving the outputs as they are.
	 */
	WORLDDIVIDE_API bool Trace(const UWorld& World, const FVector& Location, float Up, float Down, float& OutGroundZ,
		FVector* OutNormal = nullptr);

	/**
	 * Trace's ray, asking for the ground's physical material too (#224): the surface type of the ground hit in OutSurface,
	 * SurfaceType_Default where it has no physical material. Returns false when there is no ground in that span, leaving
	 * OutSurface as it is.
	 */
	WORLDDIVIDE_API bool TraceSurface(const UWorld& World, const FVector& Location, float Up, float Down, EPhysicalSurface& OutSurface);
}
