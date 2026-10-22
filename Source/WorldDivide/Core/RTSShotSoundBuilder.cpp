// URTSEditorScripting::BuildLayeredShotSound (#356): a cannon's firing sound cue from takes of a blast, a body and an echo tail.
#include "Core/RTSEditorScripting.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeDistanceCrossfade.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "WorldDivide.h"

namespace RTSShotSoundBuilder
{
	/** The listener sits at the camera, 150 m from the ground at the default zoom (DefaultGame.ini) and 450 m at the widest. */
	constexpr float NearDistance = 18000.f;
	constexpr float FarDistance = 35000.f;
	constexpr float SilentDistance = 60000.f;

	/** How one layer of a take is heard by distance: from full at FadeOutStart to silent at FadeOutEnd. */
	struct FLayer
	{
		float FadeOutStart;
		float FadeOutEnd;
		float Volume;
	};

	// The blast is the near crack and is gone by the far distance; the body carries further; the echo tail rolls on to the
	// edge of hearing, so far shots are echo-led.
	const FLayer Layers[3] = {
		{ NearDistance, FarDistance, 1.f },
		{ FarDistance, SilentDistance, 0.9f },
		{ SilentDistance, SilentDistance, 0.8f },
	};
}

USoundCue* URTSEditorScripting::BuildLayeredShotSound(const FString& AssetPath, const TArray<USoundWave*>& Waves)
{
	using namespace RTSShotSoundBuilder;
	if (Waves.IsEmpty() || Waves.Num() % 3 != 0 || Waves.Contains(nullptr))
	{
		UE_LOG(LogWorldDivide, Error, TEXT("BuildLayeredShotSound %s: needs a blast, a body and an echo wave per take"), *AssetPath);
		return nullptr;
	}

	const FString Name = FPackageName::GetShortName(AssetPath);
	if (USoundCue* Old = LoadObject<USoundCue>(nullptr, *(AssetPath + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Old->ClearFlags(RF_Public | RF_Standalone);
		Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	UPackage* Package = CreatePackage(*AssetPath);
	USoundCue* Cue = NewObject<USoundCue>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);

	// One take at random each shot, never the same twice running.
	USoundNodeRandom* Random = Cue->ConstructSoundNode<USoundNodeRandom>(USoundNodeRandom::StaticClass(), false);
	Random->bRandomizeWithoutReplacement = true;
	const int32 NumTakes = Waves.Num() / 3;
	for (int32 Take = 0; Take < NumTakes; ++Take)
	{
		USoundNodeDistanceCrossFade* Crossfade = Cue->ConstructSoundNode<USoundNodeDistanceCrossFade>(
			USoundNodeDistanceCrossFade::StaticClass(), false);
		for (int32 Layer = 0; Layer < 3; ++Layer)
		{
			USoundNodeWavePlayer* Player = Cue->ConstructSoundNode<USoundNodeWavePlayer>(USoundNodeWavePlayer::StaticClass(), false);
			Player->SetSoundWave(Waves[Take * 3 + Layer]);
			Crossfade->InsertChildNode(Layer);
			Crossfade->ChildNodes[Layer] = Player;
			FDistanceDatum& Datum = Crossfade->CrossFadeInput[Layer];
			Datum.FadeInDistanceStart = 0.f;
			Datum.FadeInDistanceEnd = 0.f;
			Datum.FadeOutDistanceStart = Layers[Layer].FadeOutStart;
			Datum.FadeOutDistanceEnd = Layers[Layer].FadeOutEnd;
			Datum.Volume = Layers[Layer].Volume;
		}
		Random->InsertChildNode(Take);
		Random->ChildNodes[Take] = Crossfade;
	}
	Cue->FirstNode = Random;

	// Full across the screen at the default zoom, fading over the next few and duller with distance.
	Cue->bOverrideAttenuation = true;
	FSoundAttenuationSettings& Attenuation = Cue->AttenuationOverrides;
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(NearDistance, 0.f, 0.f);
	Attenuation.FalloffDistance = SilentDistance - NearDistance;
	Attenuation.bAttenuateWithLPF = true;
	Attenuation.LPFRadiusMin = NearDistance;
	Attenuation.LPFRadiusMax = SilentDistance;
	Attenuation.LPFFrequencyAtMin = 20000.f;
	Attenuation.LPFFrequencyAtMax = 1500.f;

	// A battle of 240 tanks plays the nearest dozen shots; each new one ducks the ones still ringing, so they do not clip.
	Cue->bOverrideConcurrency = true;
	FSoundConcurrencySettings& Concurrency = Cue->ConcurrencyOverrides;
	Concurrency.MaxCount = 12;
	Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
	// VolumeScale is private to the struct; only its editor details panel sets it.
	if (FFloatProperty* VolumeScale = FindFProperty<FFloatProperty>(FSoundConcurrencySettings::StaticStruct(), TEXT("VolumeScale")))
	{
		VolumeScale->SetPropertyValue_InContainer(&Concurrency, 0.85f);
	}

	Cue->LinkGraphNodesFromSoundNodes();
	Cue->PostEditChange();
	Cue->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Cue);
	return Cue;
}
#else
USoundCue* URTSEditorScripting::BuildLayeredShotSound(const FString& AssetPath, const TArray<USoundWave*>& Waves)
{
	return nullptr;
}
#endif
