#pragma once

#include "CoreMinimal.h"

class UObject;
enum class ERTSAttackAlert : uint8;

/**
 * Attack warning sounds synthesized in code, until the project has real audio: a falling two-note alarm when a building is
 * hit, a short high double beep when units are. Played without position, as an interface cue for the local player only.
 */
namespace RTSAlertSound
{
	constexpr int32 SampleRate = 22050;

	/** 16-bit mono PCM at SampleRate. Built on first use and cached per kind. */
	WORLDDIVIDE_API const TArray<uint8>& GetPcm(ERTSAttackAlert Kind);

	/** Plays the sound for Kind to the local player. Does nothing without an audio device. */
	WORLDDIVIDE_API void Play(const UObject& WorldContext, ERTSAttackAlert Kind);
}
