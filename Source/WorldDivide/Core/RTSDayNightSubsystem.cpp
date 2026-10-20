#include "Core/RTSDayNightSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarForceDark(
		TEXT("rts.Light.ForceDark"), -1,
		TEXT("Whether it is dark for the vehicles' lights (#223): -1 from the level's sun (or the environment's override), 0 day, 1 night."));

	TAutoConsoleVariable<float> CVarDarkThreshold(
		TEXT("rts.Light.DarkThreshold"), 1.f,
		TEXT("The sun's intensity in lux times the sine of its elevation under which it is dark (#223): a tenth of the default 10 lux sun overhead."));

	/** It is light again once the sun is this many times the threshold, so a sun about at it does not switch every check. */
	constexpr float LightAgainScale = 1.5f;

	/** Real seconds between looks at the sun: the lights need not follow a sunset by the frame. */
	constexpr double CheckSeconds = 1.0;
}

float URTSDayNightSubsystem::GetDarkThreshold()
{
	return CVarDarkThreshold.GetValueOnGameThread();
}

float URTSDayNightSubsystem::GetSunLight(const ULightComponent& InSun)
{
	// The light shines along its forward axis: down onto the ground when the sun is up.
	const float Elevation = static_cast<float>(-InSun.GetDirection().Z);
	return InSun.IsVisible() ? FMath::Max(InSun.Intensity, 0.f) * FMath::Max(Elevation, 0.f) : 0.f;
}

const ULightComponent* URTSDayNightSubsystem::FindSun() const
{
	const ULightComponent* First = nullptr;
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!Light)
		{
			continue;
		}
		if (Light->IsUsedAsAtmosphereSunLight())
		{
			return Light;
		}
		First = First ? First : Light;
	}
	return First;
}

bool URTSDayNightSubsystem::IsDark() const
{
	const int32 Forced = CVarForceDark.GetValueOnGameThread();
	if (Forced >= 0)
	{
		return Forced > 0;
	}
	if (DarkOverride)
	{
		return *DarkOverride;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const double Now = World->GetRealTimeSeconds();
	if (CheckedAt && Now - *CheckedAt < CheckSeconds && Now >= *CheckedAt)
	{
		return bSunDark;
	}
	CheckedAt = Now;
	if (!Sun.IsValid())
	{
		Sun = FindSun();
	}
	// Without a sun it is day: a level that has none is lit some other way.
	const float Light = Sun.IsValid() ? GetSunLight(*Sun.Get()) : TNumericLimits<float>::Max();
	const float Threshold = GetDarkThreshold();
	bSunDark = bSunDark ? Light < Threshold * LightAgainScale : Light < Threshold;
	return bSunDark;
}
