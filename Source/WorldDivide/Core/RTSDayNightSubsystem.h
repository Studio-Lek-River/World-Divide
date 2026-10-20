#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSDayNightSubsystem.generated.h"

class ULightComponent;

/**
 * Whether it is dark in the world (#223), the one question the vehicles' lamps and lights ask. Every machine works it
 * out for itself from the same source, so they agree without anything replicated: rts.Light.ForceDark when it is set,
 * else what SetDarkOverride was given (#318's environment director), else the level's sun. The sun is dark when its
 * intensity times the sine of its elevation falls below rts.Light.DarkThreshold, and light again only once that is a
 * share above it, so dusk does not flicker; it is looked at about once a second.
 */
UCLASS()
class WORLDDIVIDE_API URTSDayNightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Whether it is dark now. */
	bool IsDark() const;

	/** Makes IsDark this instead of the sun's answer, or the sun's again when unset; rts.Light.ForceDark still comes first. */
	void SetDarkOverride(TOptional<bool> bDark) { DarkOverride = bDark; }

	/**
	 * How much the sun lights the ground: its intensity in lux times the sine of its elevation, 0 below the horizon or
	 * without a sun. Public for the tests.
	 */
	static float GetSunLight(const ULightComponent& Sun);

	/** rts.Light.DarkThreshold: the sun's light (GetSunLight) under which it is dark. */
	static float GetDarkThreshold();

private:
	/** The level's sun: the directional light that is the atmosphere's sun, else the first directional light. */
	const ULightComponent* FindSun() const;

	TOptional<bool> DarkOverride;

	/** The sun's answer and the real seconds it was worked out at; worked out anew once it is old. */
	mutable TWeakObjectPtr<const ULightComponent> Sun;
	mutable TOptional<double> CheckedAt;
	mutable bool bSunDark = false;
};
