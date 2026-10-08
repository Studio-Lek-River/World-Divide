#include "Combat/RTSShotSound.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

namespace
{
	/** Heard across a screen of an RTS camera, fading out over the next few. */
	USoundAttenuation* GetAttenuation()
	{
		static TStrongObjectPtr<USoundAttenuation> Attenuation;
		if (!Attenuation)
		{
			Attenuation.Reset(NewObject<USoundAttenuation>());
			FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
			Settings.bAttenuate = true;
			Settings.bSpatialize = true;
			Settings.AttenuationShape = EAttenuationShape::Sphere;
			Settings.AttenuationShapeExtents = FVector(3000.f, 0.f, 0.f);
			Settings.FalloffDistance = 8000.f;
		}
		return Attenuation.Get();
	}

	TArray<uint8> Synthesize(float EffectScale, bool bImpact)
	{
		const float Seconds = FMath::Clamp((bImpact ? 0.4f : 0.22f) * FMath::Sqrt(EffectScale), 0.12f, 0.9f);
		const int32 NumSamples = FMath::CeilToInt(Seconds * RTSShotSound::SampleRate);
		// Rifles crack, cannons boom: the noise is filtered lower the bigger the weapon, and lower still for a blast.
		const float Cutoff = FMath::Clamp(6000.f / EffectScale * (bImpact ? 0.5f : 1.f), 400.f, 10000.f);
		const float Smoothing = FMath::Exp(-2.f * UE_PI * Cutoff / RTSShotSound::SampleRate);
		const float Decay = Seconds / 5.f;
		const float ThumpWeight = bImpact ? 1.f : FMath::Clamp(EffectScale - 0.6f, 0.f, 1.f);

		FRandomStream Random(1847);
		TArray<float> Samples;
		Samples.SetNumUninitialized(NumSamples);
		float Filtered = 0.f;
		float ThumpPhase = 0.f;
		float Peak = KINDA_SMALL_NUMBER;
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const float Time = static_cast<float>(Index) / RTSShotSound::SampleRate;
			// A 2 ms attack, so the start does not click.
			const float Attack = FMath::Min(Time / 0.002f, 1.f);
			Filtered = FMath::Lerp(Random.FRandRange(-1.f, 1.f), Filtered, Smoothing);
			// The thump sweeps down from 110 to 40 Hz.
			ThumpPhase += 2.f * UE_PI * FMath::Lerp(110.f, 40.f, Time / Seconds) / EffectScale / RTSShotSound::SampleRate;
			const float Thump = FMath::Sin(ThumpPhase) * FMath::Exp(-Time / (Decay * 2.f)) * ThumpWeight;
			Samples[Index] = Attack * (Filtered * FMath::Exp(-Time / Decay) + Thump);
			Peak = FMath::Max(Peak, FMath::Abs(Samples[Index]));
		}

		TArray<uint8> Pcm;
		Pcm.SetNumUninitialized(NumSamples * sizeof(int16));
		int16* Out = reinterpret_cast<int16*>(Pcm.GetData());
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Out[Index] = static_cast<int16>(Samples[Index] / Peak * 0.8f * MAX_int16);
		}
		return Pcm;
	}
}

const TArray<uint8>& RTSShotSound::GetPcm(float EffectScale, bool bImpact)
{
	// On the heap, so a returned buffer stays put when the map grows.
	static TMap<int32, TUniquePtr<TArray<uint8>>> Cache;
	const int32 Tenths = FMath::Clamp(FMath::RoundToInt(EffectScale * 10.f), 1, 100);
	const int32 Key = Tenths * 2 + (bImpact ? 1 : 0);
	if (const TUniquePtr<TArray<uint8>>* Cached = Cache.Find(Key))
	{
		return **Cached;
	}
	return *Cache.Add(Key, MakeUnique<TArray<uint8>>(Synthesize(Tenths / 10.f, bImpact)));
}

void RTSShotSound::Play(const UObject& WorldContext, const FVector& Location, float EffectScale, bool bImpact, USoundBase* Sound)
{
	UWorld* World = WorldContext.GetWorld();
	// Worlds that are not running, such as the unit tests', stay silent.
	if (!World || !World->HasBegunPlay() || !World->bAllowAudioPlayback || World->GetNetMode() == NM_DedicatedServer
		|| !World->GetAudioDevice())
	{
		return;
	}

	float Seconds = 0.f;
	if (!Sound)
	{
		const TArray<uint8>& Pcm = GetPcm(EffectScale, bImpact);
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
		Wave->SetSampleRate(SampleRate);
		Wave->NumChannels = 1;
		Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
		Sound = Wave;
		Seconds = static_cast<float>(Pcm.Num()) / sizeof(int16) / SampleRate;
	}

	// Light weapons fire often, so they play quieter; a little pitch spread keeps a burst from sounding like one sample.
	const float Volume = 0.5f + 0.5f * FMath::Min(EffectScale, 1.f);
	const float Pitch = FMath::FRandRange(0.92f, 1.08f);
	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume, Pitch,
		0.f, GetAttenuation());
	if (Audio && Seconds > 0.f)
	{
		// A procedural wave plays silence once its queue runs dry instead of ending, so it is stopped when its sound is over.
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Audio, [Audio] { Audio->Stop(); }),
			Seconds / Pitch + 0.1f, false);
	}
}
