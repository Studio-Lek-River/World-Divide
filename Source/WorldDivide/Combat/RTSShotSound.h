#pragma once

#include "CoreMinimal.h"

class UObject;
class USoundBase;

/**
 * Shot and blast sounds synthesized in code, until the project has real audio: decaying filtered noise, with a low thump
 * for heavy weapons. A bigger effect scale sounds deeper and longer.
 */
namespace RTSShotSound
{
	constexpr int32 SampleRate = 22050;

	/** 16-bit mono PCM at SampleRate. Built on first use and cached per tenth of scale. */
	WORLDDIVIDE_API const TArray<uint8>& GetPcm(float EffectScale, bool bImpact);

	/** Plays Sound at Location, or the synthesized sound when it is null. Does nothing without an audio device. */
	WORLDDIVIDE_API void Play(const UObject& WorldContext, const FVector& Location, float EffectScale, bool bImpact, USoundBase* Sound);
}
