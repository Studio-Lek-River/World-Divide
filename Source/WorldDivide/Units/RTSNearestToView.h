#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"

/**
 * What the vehicle effects that only the vehicles nearest a camera show have in common (the exhaust, #222, and the real
 * lights, #223): this machine's cameras, the distance to the nearest of them, and the nearest few of the candidates.
 */
namespace RTSNearestToView
{
	/** The local cameras of World this frame, into OutCameras; none on a machine that never renders. */
	inline void GatherCameras(const UWorld* World, TArray<FVector>& OutCameras)
	{
		OutCameras.Reset();
		if (!World || !FApp::CanEverRender())
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (Controller && Controller->IsLocalController() && Controller->PlayerCameraManager)
			{
				// The cameras were updated for this frame before TG_PostUpdateWork.
				OutCameras.Add(Controller->PlayerCameraManager->GetCameraLocation());
			}
		}
	}

	/** cm from Override, when set, or else from the nearest of Cameras to Location; 0 without a camera. */
	inline float GetDistance(TConstArrayView<FVector> Cameras, const TOptional<FVector>& Override, const FVector& Location)
	{
		if (Override)
		{
			return static_cast<float>(FVector::Dist(*Override, Location));
		}
		if (Cameras.IsEmpty())
		{
			return 0.f;
		}
		double NearestSquared = TNumericLimits<double>::Max();
		for (const FVector& Camera : Cameras)
		{
			NearestSquared = FMath::Min(NearestSquared, FVector::DistSquared(Camera, Location));
		}
		return static_cast<float>(FMath::Sqrt(NearestSquared));
	}

	/** Leaves the MaxCount of Candidates, (distance, entry) pairs, with the smallest distance; untouched when they fit. */
	inline void KeepNearest(TArray<TPair<float, int32>>& Candidates, int32 MaxCount)
	{
		MaxCount = FMath::Max(MaxCount, 0);
		if (Candidates.Num() > MaxCount)
		{
			Candidates.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
			Candidates.SetNum(MaxCount, EAllowShrinking::No);
		}
	}
}
