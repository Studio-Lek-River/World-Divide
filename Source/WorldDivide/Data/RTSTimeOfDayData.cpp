#include "Data/RTSTimeOfDayData.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WorldDivide.h"

namespace
{
	/** The key a share Alpha of the way from A to B. */
	FRTSTimeOfDayKey BlendKeys(const FRTSTimeOfDayKey& A, const FRTSTimeOfDayKey& B, float Alpha)
	{
		FRTSTimeOfDayKey Key;
		Key.SunElevation = FMath::Lerp(A.SunElevation, B.SunElevation, Alpha);
		Key.SunLux = FMath::Lerp(A.SunLux, B.SunLux, Alpha);
		Key.SunColor = FMath::Lerp(A.SunColor, B.SunColor, Alpha);
		Key.SkyLightIntensity = FMath::Lerp(A.SkyLightIntensity, B.SkyLightIntensity, Alpha);
		Key.ExposureBias = FMath::Lerp(A.ExposureBias, B.ExposureBias, Alpha);
		Key.FogDensity = FMath::Lerp(A.FogDensity, B.FogDensity, Alpha);
		Key.FogColor = FMath::Lerp(A.FogColor, B.FogColor, Alpha);
		Key.SceneTint = FMath::Lerp(A.SceneTint, B.SceneTint, Alpha);
		return Key;
	}

	/** The keys blended at Elevation; Keys run lowest elevation first, and past either end the end key holds. */
	FRTSTimeOfDayKey KeyAt(const TArray<FRTSTimeOfDayKey>& Keys, float Elevation)
	{
		if (Keys.IsEmpty())
		{
			return FRTSTimeOfDayKey();
		}
		if (Elevation <= Keys[0].SunElevation)
		{
			return Keys[0];
		}
		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			const FRTSTimeOfDayKey& Above = Keys[Index];
			if (Elevation <= Above.SunElevation)
			{
				const FRTSTimeOfDayKey& Below = Keys[Index - 1];
				const float Span = Above.SunElevation - Below.SunElevation;
				return BlendKeys(Below, Above, Span > 0.f ? (Elevation - Below.SunElevation) / Span : 1.f);
			}
		}
		return Keys.Last();
	}

	/** The level's sun as URTSDayNightSubsystem finds it: the atmosphere's sun light, else the first directional light. */
	UDirectionalLightComponent* FindSun(UWorld& World)
	{
		UDirectionalLightComponent* First = nullptr;
		for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
		{
			UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
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

	template <typename ActorType>
	ActorType* FindFirst(UWorld& World)
	{
		TActorIterator<ActorType> It(&World);
		return It ? *It : nullptr;
	}

	APostProcessVolume* FindUnboundVolume(UWorld& World)
	{
		for (TActorIterator<APostProcessVolume> It(&World); It; ++It)
		{
			if (It->bUnbound)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

float URTSTimeOfDayData::GetSunElevation(float Hour) const
{
	const float Day = FMath::Clamp(SunsetHour - SunriseHour, 0.01f, 23.99f);
	const float SinceSunrise = FMath::Fmod(FMath::Fmod(Hour - SunriseHour, 24.f) + 24.f, 24.f);
	if (SinceSunrise <= Day)
	{
		return NoonElevation * FMath::Sin(PI * SinceSunrise / Day);
	}
	return MidnightElevation * FMath::Sin(PI * (SinceSunrise - Day) / (24.f - Day));
}

FRTSTimeOfDayState URTSTimeOfDayData::Evaluate(float Hour) const
{
	FRTSTimeOfDayState State;
	State.SunElevation = GetSunElevation(Hour);
	const FRTSTimeOfDayKey Key = KeyAt(Keys, State.SunElevation);
	State.SkyLightIntensity = Key.SkyLightIntensity;
	State.ExposureBias = Key.ExposureBias;
	State.FogDensity = Key.FogDensity;
	State.FogColor = Key.FogColor;
	State.SceneTint = Key.SceneTint;

	// Hours from noon, -12 to 12: at 15 degrees an hour the light turns a full circle a day, so it never jumps at midnight.
	const float Noon = (SunriseHour + SunsetHour) / 2.f;
	const float FromNoon = FMath::Fmod(FMath::Fmod(Hour - Noon + 12.f, 24.f) + 24.f, 24.f) - 12.f;
	const float SunYaw = NoonYaw + FromNoon * YawPerHour;

	State.bMoon = State.SunElevation < MoonBelowElevation;
	if (State.bMoon)
	{
		State.LightRotation = FRotator(-MoonElevation, SunYaw + 180.f, 0.f);
		State.LightLux = MoonLux * FMath::Clamp((MoonBelowElevation - State.SunElevation) / MoonFadeDegrees, 0.f, 1.f);
		State.LightColor = MoonColor;
	}
	else
	{
		// Under the horizon the sun shines up: it lights only the sky, the twilight the sky light then brings down.
		State.LightRotation = FRotator(-State.SunElevation, SunYaw, 0.f);
		State.LightLux = Key.SunLux;
		State.LightColor = Key.SunColor;
	}
	return State;
}

bool URTSTimeOfDayData::Apply(UWorld* World, float Hour) const
{
	if (!World)
	{
		return false;
	}
	const FRTSTimeOfDayState State = Evaluate(Hour);
	bool bComplete = true;
	auto Missing = [&bComplete, World](const TCHAR* What)
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("URTSTimeOfDayData::Apply: %s has no %s"), *World->GetMapName(), What);
		bComplete = false;
	};

	if (UDirectionalLightComponent* Sun = FindSun(*World))
	{
		Sun->GetOwner()->Modify();
		Sun->SetWorldRotation(State.LightRotation);
		Sun->SetIntensity(State.LightLux);
		Sun->SetLightColor(State.LightColor);
	}
	else
	{
		Missing(TEXT("directional light"));
	}

	if (ASkyLight* Sky = FindFirst<ASkyLight>(*World))
	{
		Sky->Modify();
		Sky->GetLightComponent()->SetIntensity(State.SkyLightIntensity);
	}
	else
	{
		Missing(TEXT("sky light"));
	}

	if (AExponentialHeightFog* Fog = FindFirst<AExponentialHeightFog>(*World))
	{
		Fog->Modify();
		UExponentialHeightFogComponent* Component = Fog->GetComponent();
		Component->SetFogDensity(State.FogDensity);
		Component->SetFogInscatteringColor(State.FogColor);
	}
	else
	{
		Missing(TEXT("height fog"));
	}

	if (APostProcessVolume* Volume = FindUnboundVolume(*World))
	{
		Volume->Modify();
		FPostProcessSettings& Settings = Volume->Settings;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = State.ExposureBias;
		Settings.bOverride_SceneColorTint = true;
		Settings.SceneColorTint = State.SceneTint;
	}
	else
	{
		Missing(TEXT("unbound post-process volume"));
	}
	return bComplete;
}
