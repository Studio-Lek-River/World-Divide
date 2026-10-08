#include "Player/RTSAlertSound.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Player/RTSPlayerController.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

namespace
{
	struct FAlertNote
	{
		float Frequency;
		float Seconds;
	};

	/** The notes one after another, a short pause between them. A third harmonic gives the tone some edge. */
	TArray<uint8> Synthesize(const TArray<FAlertNote>& Notes, float Gap)
	{
		TArray<float> Samples;
		for (const FAlertNote& Note : Notes)
		{
			const int32 NumSamples = FMath::CeilToInt(Note.Seconds * RTSAlertSound::SampleRate);
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				const float Time = static_cast<float>(Index) / RTSAlertSound::SampleRate;
				// 5 ms in and 20 ms out, so the notes do not click.
				const float Envelope = FMath::Min3(Time / 0.005f, (Note.Seconds - Time) / 0.02f, 1.f);
				const float Phase = 2.f * UE_PI * Note.Frequency * Time;
				Samples.Add(Envelope * (FMath::Sin(Phase) + 0.3f * FMath::Sin(3.f * Phase)));
			}
			Samples.AddZeroed(FMath::CeilToInt(Gap * RTSAlertSound::SampleRate));
		}

		float Peak = KINDA_SMALL_NUMBER;
		for (const float Sample : Samples)
		{
			Peak = FMath::Max(Peak, FMath::Abs(Sample));
		}
		TArray<uint8> Pcm;
		Pcm.SetNumUninitialized(Samples.Num() * sizeof(int16));
		int16* Out = reinterpret_cast<int16*>(Pcm.GetData());
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			Out[Index] = static_cast<int16>(Samples[Index] / Peak * 0.6f * MAX_int16);
		}
		return Pcm;
	}
}

const TArray<uint8>& RTSAlertSound::GetPcm(ERTSAttackAlert Kind)
{
	static const TArray<uint8> Base = Synthesize({ { 880.f, 0.25f }, { 660.f, 0.3f } }, 0.05f);
	static const TArray<uint8> Units = Synthesize({ { 1320.f, 0.1f }, { 1320.f, 0.1f } }, 0.06f);
	return Kind == ERTSAttackAlert::Base ? Base : Units;
}

void RTSAlertSound::Play(const UObject& WorldContext, ERTSAttackAlert Kind)
{
	UWorld* World = WorldContext.GetWorld();
	// Worlds that are not running, such as the unit tests', stay silent.
	if (!World || !World->HasBegunPlay() || !World->bAllowAudioPlayback || World->GetNetMode() == NM_DedicatedServer
		|| !World->GetAudioDevice())
	{
		return;
	}

	const TArray<uint8>& Pcm = GetPcm(Kind);
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
	UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(World, Wave);
	if (Audio)
	{
		// A procedural wave plays silence once its queue runs dry instead of ending, so it is stopped when its sound is over.
		const float Seconds = static_cast<float>(Pcm.Num()) / sizeof(int16) / SampleRate;
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Audio, [Audio] { Audio->Stop(); }),
			Seconds + 0.1f, false);
	}
}
