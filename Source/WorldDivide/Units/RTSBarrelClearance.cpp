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

	/** IsSwingClear looks at the barrel at least this many degrees apart, however long it is. */
	constexpr float MinSwingStep = 2.f;

	/** GetPenetration looks at the barrel this many cm apart. */
	constexpr float PenetrationSampleStep = 5.f;

	const FName SweepTag(TEXT("RTSBarrelClearance"));

	/** What a barrel is kept out of: the world's static and dynamic geometry. Units are pawns. */
	FCollisionObjectQueryParams GetObstacleObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}

	/** The turn between two looks at a swinging barrel: its muzzle moves no more than the barrel is thick. */
	float GetSwingStep(const FRTSBarrel& Barrel)
	{
		const float Length = FVector2D(Barrel.Start + Barrel.Muzzle).Size();
		return Length > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FMath::RadiansToDegrees(2.f * Barrel.Radius / Length), MinSwingStep, YawStep)
			: YawStep;
	}
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

		TArray<FHitResult> Hits;
		World.SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, GetObstacleObjects(), FCollisionShape::MakeSphere(Radius),
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

	bool IsSwingClear(const UWorld& World, const FRTSBarrel& Barrel, const FTransform& Actor, float FromYaw, float ToYaw, float Direction,
		float Pitch)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSBarrelClearance::IsSwingClear);

		float Arc = Direction * FMath::FindDeltaAngleDegrees(FromYaw, ToYaw);
		if (Arc < 0.f)
		{
			Arc += 360.f;
		}
		const int32 Samples = FMath::CeilToInt32(Arc / GetSwingStep(Barrel));
		for (int32 Index = 1; Index <= Samples; ++Index)
		{
			if (!IsClearAt(World, Barrel, Actor, FromYaw + Direction * Arc * Index / Samples, Pitch))
			{
				return false;
			}
		}
		return true;
	}

	bool IsAnythingInReach(const UWorld& World, const FRTSBarrel& Barrel, const FTransform& Actor, float MinPitch, float MaxPitch,
		float Margin)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RTSBarrelClearance::IsAnythingInReach);

		// The barrel's height band over its elevation, from where it starts to the muzzle lowered and raised.
		float Low = FLT_MAX;
		float High = -FLT_MAX;
		for (const float Pitch : { MinPitch, MaxPitch })
		{
			FVector Start;
			FVector End;
			GetSegment(Barrel, FTransform::Identity, 0.f, Pitch, Start, End);
			Low = FMath::Min3(Low, static_cast<float>(Start.Z), static_cast<float>(End.Z));
			High = FMath::Max3(High, static_cast<float>(Start.Z), static_cast<float>(End.Z));
		}
		const float HalfWidth = Barrel.GetReach() + Margin;
		const FVector HalfExtent(HalfWidth, HalfWidth, 0.5f * (High - Low) + Barrel.Radius);
		const FVector Center = Actor.GetLocation() + FVector(0.f, 0.f, 0.5f * (Low + High));
		return World.OverlapAnyTestByObjectType(Center, FQuat::Identity, GetObstacleObjects(), FCollisionShape::MakeBox(HalfExtent),
			FCollisionQueryParams(SweepTag));
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
