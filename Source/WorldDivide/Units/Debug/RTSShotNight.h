#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class APostProcessVolume;
class ULightComponentBase;
class UWorld;

/**
 * Night for the debug pictures (rts.Debug.UnitShots #294, rts.Debug.TrackShots #325): the sun and the sky dimmed to a share of
 * their brightness, with the exposure held at the daylight one (manual, no bias, as the map previews), so the eye does not
 * adapt the night away. Set(false) brings them back.
 */
class FRTSShotNight
{
public:
	/** The share of their brightness the sun and the sky keep at night. */
	static constexpr float LightShare = 0.01f;

	void Set(UWorld& World, bool bNight);
	bool IsNight() const;

private:
	/** The sun and sky dimmed, with the brightness they had, and the exposure held for them. */
	TArray<TPair<TWeakObjectPtr<ULightComponentBase>, float>> Lights;
	TWeakObjectPtr<APostProcessVolume> Exposure;
};

#endif
