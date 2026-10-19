#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace RTSCombat
{
	/**
	 * Damage a blast deals Distance from its centre: Damage at the centre, falling off linearly to EdgeFraction of it at
	 * Radius, and nothing beyond. A zero radius hits only the point itself.
	 */
	WORLDDIVIDE_API float ComputeFalloffDamage(float Damage, float Distance, float Radius, float EdgeFraction);

	/**
	 * Server only. Deals a blast of Damage at Center, falling off to EdgeFraction at Radius, to every unit on the ground and
	 * every building, of any team: units by their centre, buildings by the nearest point of their footprint, measured along
	 * the ground. Aircraft in flight are out of it. Does nothing when Radius is not above zero.
	 */
	WORLDDIVIDE_API void ApplyAreaDamage(UWorld& World, const FVector& Center, float Damage, float Radius, float EdgeFraction,
		AActor* Instigator);
}
