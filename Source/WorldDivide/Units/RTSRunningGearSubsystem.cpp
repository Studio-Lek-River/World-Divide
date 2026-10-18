#include "Units/RTSRunningGearSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Units/RTSUnit.h"

namespace
{
	TAutoConsoleVariable<bool> CVarTrackAnimation(
		TEXT("rts.Vehicle.TrackAnimation"), true,
		TEXT("Tracks move their links with their side's travel (#151); off, they are drawn rigid. Read when a unit makes its parts."));

	TAutoConsoleVariable<float> CVarTrackAnimationDistance(
		TEXT("rts.Vehicle.TrackAnimationDistance"), 6000.f,
		TEXT("cm from the camera past which the renderer leaves the tracks' link shift out; 0 never. Read when a unit makes its parts."));

	/**
	 * A track's phase goes to its material within the animation distance times this, plus TrackSendMargin cm, of a camera:
	 * the renderer measures from the camera to the track's bounds, and the camera moves between frames. The camera's cone
	 * is widened by TrackSendMargin round the track's bounds too, for its shadow at the view's edge (#212).
	 */
	constexpr float TrackSendScale = 1.25f;
	constexpr float TrackSendMargin = 1000.f;

	uint64 GearCycles = 0;
	int64 TrackUpdates = 0;

	/**
	 * Half the angle across the diagonal of Controller's view, in radians, for FOV degrees across whichever axis the viewport
	 * keeps: tan of the half diagonal is tan(FOV / 2) * sqrt(1 + aspect^2) at most, with the aspect ratio of either way round.
	 */
	float GetViewHalfAngle(const APlayerController& Controller, float FOV)
	{
		FVector2D Size(16.f, 9.f);
		if (const ULocalPlayer* Player = Controller.GetLocalPlayer(); Player && Player->ViewportClient)
		{
			FVector2D Viewport;
			Player->ViewportClient->GetViewportSize(Viewport);
			Size = Viewport.X > 0.f && Viewport.Y > 0.f ? Viewport : Size;
		}
		const double Aspect = FMath::Max(Size.X, Size.Y) / FMath::Min(Size.X, Size.Y);
		const double Tan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FOV, 1.f, 170.f)) / 2.0);
		return static_cast<float>(FMath::Atan(Tan * FMath::Sqrt(1.0 + Aspect * Aspect)));
	}
}

void FRTSRunningGearTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Subsystem && TickType != LEVELTICK_ViewportsOnly)
	{
		Subsystem->PlaceAll();
	}
}

FString FRTSRunningGearTickFunction::DiagnosticMessage()
{
	return TEXT("URTSRunningGearSubsystem::PlaceAll");
}

void URTSRunningGearSubsystem::Register(ARTSUnit& Unit)
{
	Units.AddUnique(&Unit);
}

void URTSRunningGearSubsystem::Unregister(ARTSUnit& Unit)
{
	Units.RemoveSwap(&Unit);
}

bool URTSRunningGearSubsystem::AnimatesTracks()
{
	return CVarTrackAnimation.GetValueOnGameThread();
}

float URTSRunningGearSubsystem::GetTrackAnimationDistance()
{
	return FMath::Max(CVarTrackAnimationDistance.GetValueOnGameThread(), 0.f);
}

double URTSRunningGearSubsystem::TakeSeconds()
{
	const double Seconds = FPlatformTime::ToSeconds64(GearCycles);
	GearCycles = 0;
	return Seconds;
}

int64 URTSRunningGearSubsystem::TakeTrackUpdates()
{
	const int64 Updates = TrackUpdates;
	TrackUpdates = 0;
	return Updates;
}

void URTSRunningGearSubsystem::PlaceAll()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URTSRunningGearSubsystem::PlaceAll);
	const uint64 StartCycles = FPlatformTime::Cycles64();

	// Where this machine draws from: a dedicated server draws nothing, and a track far from every camera keeps its phase
	// to itself until one comes near.
	FRTSTrackViews Views;
	const UWorld* World = GetWorld();
	Views.bDraws = World && World->GetNetMode() != NM_DedicatedServer;
	const float Distance = GetTrackAnimationDistance();
	Views.Range = Distance > 0.f ? Distance * TrackSendScale + TrackSendMargin : 0.f;
	Views.Margin = TrackSendMargin;
	if (Views.bDraws)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Controller = It->Get();
			if (Controller && Controller->IsLocalController() && Controller->PlayerCameraManager)
			{
				// The cameras were updated for this frame before TG_PostUpdateWork, so their cone is the one drawn now (#212).
				const APlayerCameraManager& Camera = *Controller->PlayerCameraManager;
				Views.Locations.Add(Camera.GetCameraLocation());
				Views.Directions.Add(Camera.GetCameraRotation().Vector());
				Views.HalfAngles.Add(GetViewHalfAngle(*Controller, Camera.GetFOVAngle()));
			}
		}
	}

	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		if (ARTSUnit* Unit = Units[Index].Get())
		{
			TrackUpdates += Unit->UpdateRunningGear(&Views);
		}
		else
		{
			Units.RemoveAtSwap(Index);
		}
	}
	GearCycles += FPlatformTime::Cycles64() - StartCycles;
}

void URTSRunningGearSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// After the actor ticks and the tickables, avoidance among them, and before the frame's render updates are sent.
	TickFunction.Subsystem = this;
	TickFunction.TickGroup = TG_PostUpdateWork;
	TickFunction.bCanEverTick = true;
	TickFunction.bTickEvenWhenPaused = false;
	TickFunction.RegisterTickFunction(InWorld.PersistentLevel);
}

void URTSRunningGearSubsystem::Deinitialize()
{
	if (TickFunction.IsTickFunctionRegistered())
	{
		TickFunction.UnRegisterTickFunction();
	}
	Units.Reset();
	Super::Deinitialize();
}
