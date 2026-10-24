#include "Units/Debug/RTSShotNight.h"

#if !UE_BUILD_SHIPPING

#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool FRTSShotNight::IsNight() const
{
	return Exposure.IsValid();
}

void FRTSShotNight::Set(UWorld& World, bool bNight, const FMoon* Moon)
{
	if (bNight == IsNight())
	{
		return;
	}
	if (!bNight)
	{
		Exposure->Destroy();
		Exposure.Reset();
		for (const FLight& Light : Lights)
		{
			if (ULightComponent* Direct = Cast<ULightComponent>(Light.Light.Get()))
			{
				Direct->SetIntensity(Light.Intensity);
				Direct->SetLightColor(Light.Color);
			}
			else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Light.Light.Get()))
			{
				Sky->SetIntensity(Light.Intensity);
			}
		}
		Lights.Reset();
		return;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	APostProcessVolume* Volume = World.SpawnActor<APostProcessVolume>(Params);
	Exposure = Volume;
	Volume->bUnbound = true;
	Volume->Priority = 1000.f;
	FPostProcessSettings& Settings = Volume->Settings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = Moon ? Moon->ExposureBias : 0.f;
	for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
	{
		ULightComponent* Light = It->GetLightComponent();
		Light->SetMobility(EComponentMobility::Movable);
		Lights.Add({ Light, Light->Intensity, Light->GetLightColor() });
		Light->SetIntensity(Moon ? Moon->Lux : Light->Intensity * LightShare);
		if (Moon)
		{
			Light->SetLightColor(Moon->Color);
		}
	}
	for (TActorIterator<ASkyLight> It(&World); It; ++It)
	{
		USkyLightComponent* Sky = It->GetLightComponent();
		Sky->SetMobility(EComponentMobility::Movable);
		Lights.Add({ Sky, Sky->Intensity });
		Sky->SetIntensity(Moon ? Moon->SkyIntensity : Sky->Intensity * LightShare);
	}
}

#endif
