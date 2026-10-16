#include "Units/RTSBarrelClearance.h"

#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSUnitShape.h"

namespace
{
	/** A surface whose normal points up at least this much is ground, a ramp or a roof seen from above: a barrel passes over it. */
	constexpr float UpFacingNormalZ = 0.7f;

	/** The turret is tried this many degrees further round each time, up to half a turn either way. */
	constexpr float YawStep = 15.f;

	/** GetPenetration looks at the barrel this many cm apart. */
	constexpr float PenetrationSampleStep = 5.f;

	const FName SweepTag(TEXT("RTSBarrelClearance"));
}

namespace RTSBarrelClearance
{
	void GetSegment(const FRTSBarrel& Barrel, const FTransform& Actor, float Yaw, float Pitch, FVector& OutStart, FVector& OutEnd)
	{
		const FRotator Turret(0.f, Yaw, 0.f);
		const FVector Muzzle = Barrel.bElevates ? FRotator(Pitch, 0.f, 0.f).RotateVector(Barrel.Muzzle) : Barrel.Muzzle;
		OutStart = Actor.TransformPosition(Barrel.TurretPivot + Turret.RotateVector(Barrel.Start));
		OutEnd = Actor.TransformPosition(Barrel.TurretPivot + Turret.RotateVector(Barrel.Start + Muzzle));
	}

	bool IsClear(const UWorld& World, const FVector& Start, const FVector& End, float Radius, FHitResult* OutHit)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSBarrelClearance::IsClear);

		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		TArray<FHitResult> Hits;
		World.SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius),
			FCollisionQueryParams(SweepTag));
		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bStartPenetrating && Hit.ImpactNormal.Z >= UpFacingNormalZ)
			{
				continue;
			}
			if (OutHit)
			{
				*OutHit = Hit;
			}
			return false;
		}
		return true;
	}

	bool IsClearAt(const UWorld& World, const FRTSBarrel& Barrel, const FTransform& Actor, float Yaw, float Pitch)
	{
		FVector Start;
		FVector End;
		GetSegment(Barrel, Actor, Yaw, Pitch, Start, End);
		return IsClear(World, Start, End, Barrel.Radius);
	}

	float GetPenetration(const FVector& Start, const FVector& End, float Radius, const FBox& Box)
	{
		const int32 Samples = FMath::Max(1, FMath::CeilToInt32(FVector::Dist(Start, End) / PenetrationSampleStep));
		float Deepest = 0.f;
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const FVector Point = FMath::Lerp(Start, End, static_cast<float>(Index) / Samples);
			// Inside, how far to the nearest face; outside, minus how far from the box.
			const FVector Inside = FVector::Min(Point - Box.Min, Box.Max - Point);
			const float Depth = Inside.GetMin() > 0.f ? Inside.GetMin() : -FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(Point));
			Deepest = FMath::Max(Deepest, Depth + Radius);
		}
		return Deepest;
	}

	FAim FindClearAim(const UWorld& World, const FRTSBarrel& Barrel, TConstArrayView<FTransform> Poses, float WantedYaw,
		float WantedPitch, float CurrentYaw, float MaxPitch)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSBarrelClearance::FindClearAim);

		// Raising the gun first: it may clear a low wall or a roof and still point at the target.
		TArray<float, TInlineAllocator<2>> Pitches = { WantedPitch };
		if (Barrel.bElevates && MaxPitch > WantedPitch)
		{
			Pitches.Add(MaxPitch);
		}
		const float Side = FMath::FindDeltaAngleDegrees(WantedYaw, CurrentYaw) >= 0.f ? 1.f : -1.f;
		for (const int32 PoseCount : { Poses.Num(), 1 })
		{
			for (float Offset = 0.f; Offset <= 180.f; Offset += YawStep)
			{
				for (const float Sign : { Side, -Side })
				{
					// Straight on and straight back are one yaw each.
					if ((Offset == 0.f || Offset == 180.f) && Sign != Side)
					{
						continue;
					}
					const float Yaw = FRotator::NormalizeAxis(WantedYaw + Sign * Offset);
					for (const float Pitch : Pitches)
					{
						bool bClear = true;
						for (int32 Pose = 0; bClear && Pose < PoseCount; ++Pose)
						{
							bClear = IsClearAt(World, Barrel, Poses[Pose], Yaw, Pitch);
						}
						if (bClear)
						{
							return { Yaw, Pitch, true };
						}
					}
				}
			}
			if (Poses.Num() <= 1)
			{
				break;
			}
		}
		return {};
	}
}
