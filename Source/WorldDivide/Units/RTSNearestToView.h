#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"

/**
 * What the vehicle effects that only the vehicles nearest a camera show have in common (the exhaust, #222, the real
 * lights, #223, and the dust, #224): this machine's cameras, the distance to the nearest of them, the nearest few of the
 * candidates, and how their emitters thin out with the effects quality and the distance.
 */
namespace RTSNearestToView
{
	/** A vehicle showing an effect stops only this much past its distance, so one at the edge does not start and stop. */
	constexpr float StopScale = 1.1f;

	/**
	 * A vehicle showing an effect counts as this much nearer when the nearest take the budget's places, so two at about the
	 * same distance do not swap them every frame.
	 */
	constexpr float KeepScale = 0.9f;

	/** The share of an emitter's rate at each effects quality, low to epic; cinematic is epic. As the muzzle smoke's (#221). */
	constexpr float QualityScale[] = { 0.3f, 0.5f, 0.75f, 1.f };

	/** The share of the rate left at the effect's distance, falling off from 1 at the camera. */
	constexpr float FarScale = 0.5f;

	/** A rate or an opacity is sent to the emitters again once it differs by this share from what they have. */
	constexpr float ResendShare = 0.05f;

	/**
	 * The share of an emitter's rate at the effects quality EffectsQuality (0 low to 3 epic), ViewDistance cm from the camera,
	 * for an effect shown within Distance cm (0 or less everywhere).
	 */
	inline float GetRateScale(float ViewDistance, float Distance, int32 EffectsQuality)
	{
		const float Quality = QualityScale[FMath::Clamp(EffectsQuality, 0, static_cast<int32>(UE_ARRAY_COUNT(QualityScale)) - 1)];
		const float Far = Distance > 0.f ? FMath::Lerp(1.f, FarScale, FMath::Clamp(ViewDistance / Distance, 0.f, 1.f)) : 1.f;
		return Quality * Far;
	}

	/** Whether Value, once sent as Sent, should be sent again. */
	inline bool Differs(float Sent, float Value)
	{
		return FMath::Abs(Value - Sent) > ResendShare * FMath::Max(FMath::Abs(Sent), KINDA_SMALL_NUMBER);
	}

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
