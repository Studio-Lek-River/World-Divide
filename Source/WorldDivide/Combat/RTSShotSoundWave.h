#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "RTSShotSoundWave.generated.h"

/**
 * One synthesized shot sound, shared by every shot that makes it (#227). Each playing copy reads the same samples through a
 * sound generator of its own, so copies overlap freely and each ends when its sound is over.
 */
UCLASS(Transient)
class WORLDDIVIDE_API URTSShotSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	/** Plays Pcm, 16-bit mono at RTSShotSound::SampleRate. */
	void SetPcm(const TArray<uint8>& Pcm);

	using USoundWaveProcedural::CreateSoundGenerator;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> Samples;
};
