#include "Data/RTSUnitData.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float FRTSGunRecoil::OffsetAt(float Time, float From) const
{
	// Back from where the barrel was, so a shot before it is out again starts its stroke there instead of jumping, then
	// out to rest; never past Distance.
	if (Time < RecoilSeconds)
	{
		const float Back = 1.f - FMath::Square(1.f - FMath::Max(Time, 0.f) / RecoilSeconds);
		return FMath::Lerp(FMath::Min(From, Distance), Distance, Back);
	}
	const float Out = (Time - RecoilSeconds) / ReturnSeconds;
	return Out >= 1.f ? 0.f : Distance * (1.f - FMath::SmoothStep(0.f, 1.f, Out));
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
	return Result;
}
#endif
