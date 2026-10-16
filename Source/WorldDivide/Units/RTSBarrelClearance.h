#pragma once

#include "CoreMinimal.h"

class UWorld;
struct FRTSBarrel;

/**
 * Keeps a turret's barrel out of walls and buildings (#162). The hull alone steers and stops the unit (RTSHullClearance);
 * the barrel only chooses where the turret and gun point. A barrel is a capsule from where it starts on the turret to the
 * muzzle, swept against the world's static and dynamic geometry, so it may reach over anything lower than itself, a low
 * wall or a roof, and never into a side. Units are pawns and ignore both channels, so they never block a barrel; the ground
 * and other surfaces facing up are passed over.
 *
 * Sweeps cost more than a hull check, so ARTSUnit only asks once the barrel's reach comes near a wall of its navmesh, or an
 * overlap now and then finds something there the navmesh leaves out, and no more often than its aim check interval. The
 * turret's swing to a clear aim is swept too: it turns round the back, or raises the gun first, rather than through a wall.
 */
namespace RTSBarrelClearance
{
	/** The barrel's ends in world space for a unit standing at Actor with its turret at Yaw and its gun at Pitch (degrees). */
	WORLDDIVIDE_API void GetSegment(const FRTSBarrel& Barrel, const FTransform& Actor, float Yaw, float Pitch, FVector& OutStart,
		FVector& OutEnd);

	/** True when nothing solid lies along the barrel from Start to End. OutHit, when given, is what blocks it. */
	WORLDDIVIDE_API bool IsClear(const UWorld& World, const FVector& Start, const FVector& End, float Radius, FHitResult* OutHit = nullptr);

	/** IsClear for the barrel of a unit standing at Actor with its turret at Yaw and its gun at Pitch. */
	WORLDDIVIDE_API bool IsClearAt(const UWorld& World, const FRTSBarrel& Barrel, const FTransform& Actor, float Yaw, float Pitch);

	/**
	 * True when the barrel stays clear all the way while the turret turns from FromYaw to ToYaw, toward larger yaws for a
	 * Direction of 1 and smaller for -1, the gun at Pitch. FromYaw itself is not looked at; the barrel is, every few degrees,
	 * so its muzzle moves no more than the barrel is thick between two looks.
	 */
	WORLDDIVIDE_API bool IsSwingClear(const UWorld& World, const FRTSBarrel& Barrel, const FTransform& Actor, float FromYaw, float ToYaw,
		float Direction, float Pitch);

	/**
	 * True when anything solid stands where the barrel could reach at any yaw, with the gun anywhere between MinPitch and
	 * MaxPitch, or up to Margin further out: one overlap of a box round the unit, as high as the barrel's band, so the ground
	 * below it is left out. Tells when the sweeps are needed for what the navmesh does not show.
	 */
	WORLDDIVIDE_API bool IsAnythingInReach(const UWorld& World, const FRTSBarrel& Barrel, const FTransform& Actor, float MinPitch,
		float MaxPitch, float Margin);

	/**
	 * How deep a barrel from Start to End, Radius thick, reaches into Box, in cm; zero when it stays out, over the top
	 * included. For measuring, not for steering: the debug shots and the tests.
	 */
	WORLDDIVIDE_API float GetPenetration(const FVector& Start, const FVector& End, float Radius, const FBox& Box);

	struct FAim
	{
		float Yaw = 0.f;
		float Pitch = 0.f;
		/** False when no aim tried is clear. */
		bool bFound = false;
	};

	/**
	 * The aim closest to WantedYaw and WantedPitch whose barrel is clear at every one of Poses: the wanted aim, or the gun
	 * raised to MaxPitch at that yaw, or the turret turned further and further either way, the side CurrentYaw lies on first
	 * so it does not swing across. When no aim clears every pose, the first pose alone counts: where the unit stands now.
	 */
	WORLDDIVIDE_API FAim FindClearAim(const UWorld& World, const FRTSBarrel& Barrel, TConstArrayView<FTransform> Poses, float WantedYaw,
		float WantedPitch, float CurrentYaw, float MaxPitch);
}
