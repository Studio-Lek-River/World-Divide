#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class APostProcessVolume;
class ULightComponentBase;
class UWorld;

/**
 * Night for the debug pictures (rts.Debug.UnitShots #294, rts.Debug.TrackShots #325): the sun and the sky dimmed to a share of
 * their brightness, with the exposure held at the daylight one (manual, no bias, as the map previews), so the eye does not
 * adapt the night away. With a moon (rts.Debug.BuildingShots #424) the sun and the sky take its light and the exposure its
 * bias instead. Set(false) brings them back.
 */
class FRTSShotNight
{
public:
	/** The share of their brightness the sun and the sky keep at night. */
	static constexpr float LightShare = 0.01f;

	/** A night's light: the sun as a moon, the sky and the manual exposure's bias. */
	struct FMoon
	{
		float Lux = 0.f;
		FLinearColor Color = FLinearColor::White;
		float SkyIntensity = 0.f;
		float ExposureBias = 0.f;
	};

	/** The game's night, as L_TestMapNight lights it (setup_testmap.py, NIGHT_MOON_LUX and on; the colour as the bytes it sets). */
	static FMoon GetGameMoon() { return { 0.6f, FLinearColor(FColor(140, 168, 255)), 0.25f, 2.f }; }

	void Set(UWorld& World, bool bNight, const FMoon* Moon = nullptr);
	bool IsNight() const;

private:
	struct FLight
	{
		TWeakObjectPtr<ULightComponentBase> Light;
		float Intensity = 0.f;
		FLinearColor Color = FLinearColor::White;
	};

	/** The sun and sky dimmed, with the brightness and colour they had, and the exposure held for them. */
	TArray<FLight> Lights;
	TWeakObjectPtr<APostProcessVolume> Exposure;
};

#endif
