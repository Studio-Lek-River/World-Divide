#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class AActor;
class APlayerController;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Which LOD the renderer draws a unit's meshes at (#149), worked out on the game thread the way the renderer picks it
 * (ComputeBoundsScreenSize and ComputeLODForMeshes): a part's bounds sphere, or each instance's box for an instanced part
 * (GPU LOD selection), against its mesh's screen sizes, scaled by r.StaticMeshLODDistanceScale. rts.Debug.UnitShots logs
 * it with each LOD shot and rts.Debug.Bench counts it per row. r.ForceLOD is left out: it is what is being compared.
 */
namespace RTSUnitLOD
{
	/** The LOD of Mesh for bounds of Radius at Distance cm from a view with Projection. */
	int32 ChooseLOD(const UStaticMesh& Mesh, double Radius, double Distance, const FMatrix& Projection);

	/** A unit's hull: the first mesh on its root component (the collision box) that is not instanced; null without one. */
	const UStaticMeshComponent* FindHull(const AActor& Actor);

	/** How many of an actor's drawn mesh pieces (parts, and each instance of an instanced one) are at each LOD. */
	struct FPartLODs
	{
		TArray<int32, TInlineAllocator<4>> Counts;
		/** The LOD of the hull (FindHull), INDEX_NONE without one. */
		int32 RootLOD = INDEX_NONE;
		/** Each piece's mesh, LOD and distance in m, for those not at the hull's LOD. */
		TArray<FString> Outliers;

		int32 Total() const;
		/** Whether every piece is at the root's LOD. */
		bool IsCoherent() const;
		/** Such as "LOD1 x 21", or "LOD1 x 19, LOD2 x 2" and the outliers when they differ. */
		FString ToString() const;
	};

	FPartLODs GetPartLODs(const AActor& Actor, const FVector& ViewOrigin, const FMatrix& Projection);

	/** The local player's view origin and projection, as its viewport draws it; false without one. */
	bool GetPlayerView(const APlayerController& Controller, FVector& OutOrigin, FMatrix& OutProjection);
}

#endif
