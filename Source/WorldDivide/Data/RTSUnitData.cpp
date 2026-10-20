#include "Data/RTSUnitData.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float FRTSGunRecoil::OffsetAt(float Time, float From) const
{
	// Back from where the barrel was, so a shot before it is out again starts its stroke there instead of jumping; never
	// past Distance.
	if (Time < RecoilSeconds)
	{
		const float Back = 1.f - FMath::Square(1.f - FMath::Max(Time, 0.f) / RecoilSeconds);
		return FMath::Lerp(FMath::Min(From, Distance), Distance, Back);
	}
	const float Out = (Time - RecoilSeconds - HoldSeconds) / ReturnSeconds;
	if (Out <= 0.f)
	{
		return Distance;
	}
	// The recuperator pushes it out from a standstill, fastest a third of the way in, then the buffer brakes it: half the
	// stroke is gone by 40% of the time and the last tenth creeps home over the final third.
	return Out >= 1.f ? 0.f : Distance * FMath::Cube(1.f - Out) * (1.f + 3.f * Out);
}

void FRTSRecoilSpring::Evaluate(float Elapsed, float& OutAngle, float& OutRate) const
{
	// The damped spring's motion from FromAngle and FromRate: e^(-Decay t) (A cos(Swing t) + B sin(Swing t)).
	const float Fade = FMath::Exp(-Decay * Elapsed);
	float Sin = 0.f;
	float Cos = 1.f;
	FMath::SinCos(&Sin, &Cos, Swing * Elapsed);
	const float B = (FromRate + Decay * FromAngle) / Swing;
	OutAngle = Fade * (FromAngle * Cos + B * Sin);
	OutRate = Fade * (FromRate * Cos - (Decay * FromRate + (FMath::Square(Decay) + FMath::Square(Swing)) * FromAngle) / Swing * Sin);
}

void FRTSRecoilSpring::Kick(float Peak, float SettleSeconds, float Damping)
{
	float Now = 0.f;
	float Rate = 0.f;
	if (bActive)
	{
		Evaluate(Time, Now, Rate);
	}
	FromAngle = Now;
	// Settled once the swing has faded to under 2% (e^-4): for the M20's hull a quarter of a centimetre at its nose, too little
	// to see it come to rest, where waiting for less would keep it moving, and paying for it, for half as long again.
	Settle = FMath::Max(SettleSeconds, 0.05f);
	const float Ratio = FMath::Clamp(Damping, 0.05f, 0.95f);
	Decay = 4.f / Settle;
	Swing = Decay / Ratio * FMath::Sqrt(1.f - FMath::Square(Ratio));
	// From rest a kick of rate V peaks at V / Swing * e^(-Decay tp) sin(Swing tp), tp = atan(Swing / Decay) / Swing.
	const float PeakTime = FMath::Atan2(Swing, Decay) / Swing;
	const float PeakPerRate = FMath::Exp(-Decay * PeakTime) * FMath::Sin(Swing * PeakTime) / Swing;
	FromRate = Rate + Peak / PeakPerRate;
	Time = 0.f;
	Angle = FromAngle;
	bActive = true;
}

void FRTSRecoilSpring::Advance(float DeltaSeconds)
{
	if (!bActive)
	{
		return;
	}
	Time += DeltaSeconds;
	if (Time >= Settle)
	{
		Reset();
		return;
	}
	float Rate = 0.f;
	Evaluate(Time, Angle, Rate);
}

float FRTSLocomotionProfile::GetMinTurnRadius() const
{
	const float Steered = Wheelbase / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(MaxSteerAngle, 1.f, 80.f)));
	return FMath::Max(MinTurnRadius, Steered);
}

bool FRTSLocomotionProfile::Validate(FString* OutError) const
{
	auto Fail = [OutError](const TCHAR* Problem)
	{
		if (OutError)
		{
			*OutError = Problem;
		}
		return false;
	};
	auto Positive = [](float Value) { return FMath::IsFinite(Value) && Value > 0.f; };
	if (Type == ERTSLocomotionType::Legs)
	{
		return true;
	}
	if (!FMath::IsFinite(ReverseSpeed) || ReverseSpeed < 0.f)
	{
		return Fail(TEXT("ReverseSpeed must be finite and not negative"));
	}
	if (!Positive(Acceleration) || !Positive(Braking))
	{
		return Fail(TEXT("Acceleration and Braking must be finite and positive"));
	}
	if (!Positive(MaxLateralAcceleration))
	{
		return Fail(TEXT("MaxLateralAcceleration must be finite and positive"));
	}
	if (Type == ERTSLocomotionType::Tracked)
	{
		if (!Positive(TrackGauge))
		{
			return Fail(TEXT("TrackGauge must be finite and positive"));
		}
		if (!FMath::IsFinite(PivotAngle) || PivotAngle < 1.f || PivotAngle > 179.f)
		{
			return Fail(TEXT("PivotAngle must lie between 1 and 179 degrees"));
		}
		return true;
	}
	if (!Positive(Wheelbase) || !Positive(AxleTrack))
	{
		return Fail(TEXT("Wheelbase and AxleTrack must be finite and positive"));
	}
	if (!FMath::IsFinite(MaxSteerAngle) || MaxSteerAngle < 1.f || MaxSteerAngle > 80.f)
	{
		return Fail(TEXT("MaxSteerAngle must lie between 1 and 80 degrees"));
	}
	if (!Positive(SteerRate))
	{
		return Fail(TEXT("SteerRate must be finite and positive"));
	}
	if (!FMath::IsFinite(MinTurnRadius) || MinTurnRadius < 0.f)
	{
		return Fail(TEXT("MinTurnRadius must be finite and not negative"));
	}
	return true;
}

TArray<FVector, TInlineAllocator<2>> FRTSVehicleLights::GetClusterMidpoints(TConstArrayView<FVector> Offsets)
{
	// The left side (-Y) first, then the right; a lamp on the middle line counts with the right.
	FVector Sums[2] = { FVector::ZeroVector, FVector::ZeroVector };
	int32 Counts[2] = { 0, 0 };
	for (const FVector& Offset : Offsets)
	{
		const int32 Side = Offset.Y < 0.0 ? 0 : 1;
		Sums[Side] += Offset;
		++Counts[Side];
	}
	TArray<FVector, TInlineAllocator<2>> Midpoints;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (Counts[Side] > 0)
		{
			Midpoints.Add(Sums[Side] / Counts[Side]);
		}
	}
	return Midpoints;
}

int32 FRTSVehicleLights::GetLightCount() const
{
	return GetClusterMidpoints(HeadlightOffsets).Num() + GetClusterMidpoints(TaillightOffsets).Num();
}

bool FRTSVehicleLights::Validate(FString* OutError) const
{
	auto Fail = [OutError](const TCHAR* Problem)
	{
		if (OutError)
		{
			*OutError = Problem;
		}
		return false;
	};
	if (!HeadlightOffsets.IsEmpty() && HeadlightInnerCone > HeadlightOuterCone)
	{
		return Fail(TEXT("the headlights' inner cone passes their outer one"));
	}
	if (!TaillightOffsets.IsEmpty() && TaillightInnerCone > TaillightOuterCone)
	{
		return Fail(TEXT("the tail lamps' inner cone passes their outer one"));
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult URTSUnitData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FString Error;
	if (!Locomotion.Validate(&Error))
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("Locomotion: %s"), *Error)));
		Result = EDataValidationResult::Invalid;
	}
	if (!Exhaust.Outlets.IsEmpty() && !Exhaust.System)
	{
		Context.AddError(FText::FromString(TEXT("Exhaust: outlets without a system")));
		Result = EDataValidationResult::Invalid;
	}
	if (!Lights.Validate(&Error))
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("Lights: %s"), *Error)));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
