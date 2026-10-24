#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSTimeOfDayData.generated.h"

/** How the world looks at one sun elevation; URTSTimeOfDayData blends between the two keys around the sun's. */
USTRUCT(BlueprintType)
struct FRTSTimeOfDayKey
{
	GENERATED_BODY()

	/** The sun's elevation in degrees this key holds at; below 0 the sun is down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day", meta = (ClampMin = "-90", ClampMax = "90"))
	float SunElevation = 0.f;

	/** The sun's intensity in lux. The sky atmosphere already reddens and dims it near the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day", meta = (ClampMin = "0"))
	float SunLux = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day")
	FLinearColor SunColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day", meta = (ClampMin = "0"))
	float SkyLightIntensity = 1.f;

	/** The manual exposure's bias in stops: the post-process volume's AutoExposureBias, physical camera exposure off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day")
	float ExposureBias = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day", meta = (ClampMin = "0"))
	float FogDensity = 0.01f;

	/** The height fog's inscattering colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day")
	FLinearColor FogColor = FLinearColor(0.447f, 0.638f, 1.f);

	/** Multiplies the whole frame (the post-process volume's SceneColorTint): the night's cold grade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time of day")
	FLinearColor SceneTint = FLinearColor::White;
};

/** The environment at one hour, as URTSTimeOfDayData::Evaluate works it out. */
USTRUCT(BlueprintType)
struct FRTSTimeOfDayState
{
	GENERATED_BODY()

	/** The sun's elevation in degrees, below 0 at night. */
	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	float SunElevation = 0.f;

	/** Whether the light is the moon: the sun is below URTSTimeOfDayData::MoonBelowElevation. */
	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	bool bMoon = false;

	/** The rotation of the level's one directional light, the sun's or the moon's. */
	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	FRotator LightRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	float LightLux = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	FLinearColor LightColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	float SkyLightIntensity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	float ExposureBias = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	float FogDensity = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	FLinearColor FogColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Time of day")
	FLinearColor SceneTint = FLinearColor::White;
};

/**
 * A map's time of day (#159): one movable sun whose path follows the hour, and the light, exposure, fog and grade that follow
 * the sun's elevation. The sky atmosphere, the real-time sky light, Lumen and the shadows follow the sun by themselves.
 * Below MoonBelowElevation the level's one directional light becomes the moon, so URTSDayNightSubsystem keeps reading one
 * light. DefaultHour is the map's authored look, time of day Off (#313); the setup lights the level at it, and #318's
 * environment director calls Apply with the replicated hour.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSTimeOfDayData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The hour, 0 to 24, the map is lit at when time of day is Off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "24"))
	float DefaultHour = 15.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "24"))
	float SunriseHour = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "24"))
	float SunsetHour = 20.f;

	/** The sun's elevation in degrees at noon, halfway between sunrise and sunset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "0", ClampMax = "90"))
	float NoonElevation = 53.f;

	/** The sun's elevation in degrees at midnight, halfway through the night: below 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun", meta = (ClampMin = "-90", ClampMax = "0"))
	float MidnightElevation = -30.f;

	/** The yaw the sun's light shines along at noon: 0 along +X, north on the maps, so the sun stands in the south. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float NoonYaw = 0.f;

	/**
	 * Degrees the sun turns an hour, from the east in the morning to the west in the evening. At 15 it turns a full circle
	 * a day, so the moon, which shines from opposite it, does not jump at midnight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	float YawPerHour = 15.f;

	/** How the world looks by sun elevation, lowest first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	TArray<FRTSTimeOfDayKey> Keys;

	/** The sun's elevation in degrees under which the light is the moon: past dusk, when the sky has gone dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon", meta = (ClampMin = "-90", ClampMax = "0"))
	float MoonBelowElevation = -6.f;

	/** Degrees the sun sinks further under MoonBelowElevation while the moon fades in to MoonLux. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon", meta = (ClampMin = "0.01"))
	float MoonFadeDegrees = 6.f;

	/** The moon's elevation in degrees; it shines from opposite the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon", meta = (ClampMin = "0", ClampMax = "90"))
	float MoonElevation = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon", meta = (ClampMin = "0"))
	float MoonLux = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moon")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.66f, 1.f);

	/** The sun's elevation in degrees at Hour: a sine over the day from sunrise to sunset, and another under the horizon over the night. */
	UFUNCTION(BlueprintPure, Category = "Time of day")
	float GetSunElevation(float Hour) const;

	UFUNCTION(BlueprintPure, Category = "Time of day")
	FRTSTimeOfDayState Evaluate(float Hour) const;

	/**
	 * Lights World as at Hour: the level's sun (the atmosphere's sun light, else the first directional light), its first sky
	 * light and height fog, and its unbound post-process volume's exposure and tint. Purely visual. False, with a warning,
	 * when the level lacks one of them; the rest is still set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Time of day")
	bool Apply(UWorld* World, float Hour) const;
};
