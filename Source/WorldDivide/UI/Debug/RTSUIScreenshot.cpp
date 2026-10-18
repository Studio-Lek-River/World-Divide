#include "UI/Debug/RTSUIScreenshot.h"

#if !UE_BUILD_SHIPPING

#include "Containers/Ticker.h"
#include "Core/RTSPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"
#include "UI/Debug/RTSUITestState.h"
#include "UI/Layout/RTSLayoutWidget.h"
#include "UI/RTSHUDWidget.h"
#include "UI/RTSUIStyleSet.h"
#include "UI/RTSUIStyleSubsystem.h"
#include "UnrealClient.h"
#include "WorldDivide.h"

namespace
{
	const TCHAR* NeutralTheme = TEXT("Neutral");
	const TCHAR* FullScreenPanel = TEXT("HUD");
	const TCHAR* QuitParam = TEXT("RTSUIShotsQuit");
	const TCHAR* MotionBlurVariable = TEXT("r.MotionBlurQuality");

	/** Real seconds the game may take to give the player a HUD, the screen to take a new size, a shot to arrive. */
	constexpr double ReadyTimeout = 60.0;
	constexpr double ResizeTimeout = 10.0;
	constexpr double CaptureTimeout = 10.0;
	/** Frames for the camera to settle on the state before the game pauses, and for the HUD to lay out after a change. */
	constexpr int32 SettleFrames = 60;
	constexpr int32 LayoutFrames = 10;

	/** One run of rts.UI.Screenshot, a step per frame, driven by the core ticker until it is done. */
	class FScreenshotRun : public TSharedFromThis<FScreenshotRun>
	{
	public:
		FScreenshotRun(UWorld& InWorld, const FString& InPanel, TArray<FString> InFactions)
			: World(&InWorld), Panel(InPanel), Factions(MoveTemp(InFactions))
		{
			// <Element>@<Scene>: the crop of Element in RTSUITestState::ApplyScene's Scene, saved under the whole name.
			if (!Panel.Split(TEXT("@"), &Element, &Scene))
			{
				Element = Panel;
			}
		}

		static bool IsRunning() { return Active.IsValid(); }

		static void Start(const TSharedRef<FScreenshotRun>& Run)
		{
			Active = Run;
			Run->StepStart = FPlatformTime::Seconds();
			Run->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddSP(Run, &FScreenshotRun::OnCaptured);
			Run->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FScreenshotRun::Tick));
		}

	private:
		enum class EStep : uint8
		{
			WaitForHUD,
			Settle,
			Restyle,
			Resize,
			Layout,
			Capture,
		};

		bool Tick(float)
		{
			if (!World.IsValid())
			{
				return Finish(TEXT("the world ended"));
			}
			++StepFrames;
			switch (Step)
			{
			case EStep::WaitForHUD:
				return WaitForHUD();
			case EStep::Settle:
				if (StepFrames >= SettleFrames)
				{
					UGameplayStatics::SetGamePaused(World.Get(), true);
					// A paused world keeps the last frame's view as the previous one, so after a resize motion blur
					// would smear every frame.
					if (IConsoleVariable* MotionBlur = IConsoleManager::Get().FindConsoleVariable(MotionBlurVariable))
					{
						SavedMotionBlur = MotionBlur->GetInt();
						MotionBlur->Set(0, ECVF_SetByConsole);
					}
					GoTo(EStep::Restyle);
				}
				return true;
			case EStep::Restyle:
				return Restyle();
			case EStep::Resize:
				return Resize();
			case EStep::Layout:
				if (StepFrames >= LayoutFrames)
				{
					return RequestShot();
				}
				return true;
			case EStep::Capture:
				return Capture();
			}
			return true;
		}

		void GoTo(EStep Next)
		{
			Step = Next;
			StepFrames = 0;
			StepStart = FPlatformTime::Seconds();
		}

		bool TimedOut(double Timeout) const { return FPlatformTime::Seconds() - StepStart > Timeout; }

		ARTSPlayerController* GetController() const
		{
			return World.IsValid() ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
		}

		URTSHUDWidget* GetHUDWidget() const
		{
			const ARTSPlayerController* Controller = GetController();
			const ARTSHUD* HUD = Controller ? Controller->GetHUD<ARTSHUD>() : nullptr;
			return HUD ? HUD->GetHUDWidget() : nullptr;
		}

		FIntPoint GetViewportSize() const
		{
			const UGameViewportClient* Client = World.IsValid() ? World->GetGameViewport() : nullptr;
			return Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		}

		bool WaitForHUD()
		{
			ARTSPlayerController* Controller = GetController();
			const ARTSPlayerState* Player = Controller ? Controller->GetPlayerState<ARTSPlayerState>() : nullptr;
			const URTSHUDWidget* HUD = GetHUDWidget();
			if (!Player || !Player->GetFaction() || !HUD || !World->HasBegunPlay())
			{
				return TimedOut(ReadyTimeout) ? Finish(TEXT("the player got no faction or HUD in time")) : true;
			}
			if (Element != FullScreenPanel && (!HUD->GetLayout() || !HUD->GetLayout()->FindElement(*Element)))
			{
				return Finish(FString::Printf(TEXT("the HUD layout has no element named %s"), *Element));
			}

			FString Error;
			if (!RTSUITestState::Apply(*Controller, Error) || (!Scene.IsEmpty() && !RTSUITestState::ApplyScene(*Controller, Scene, Error)))
			{
				return Finish(Error);
			}
			// PIE draws into the editor's viewport, whose size the game cannot change.
			OriginalSize = GetViewportSize();
			if (World->WorldType == EWorldType::PIE)
			{
				Resolutions = { OriginalSize };
				UE_LOG(LogWorldDivide, Display, TEXT("rts.UI.Screenshot: in PIE only at the viewport's %dx%d; ue.ps1 -Task uishots takes every resolution"),
					OriginalSize.X, OriginalSize.Y);
			}
			else
			{
				Resolutions = TArray<FIntPoint>(RTSUIScreenshot::GetResolutions());
			}
			GoTo(EStep::Settle);
			return true;
		}

		/** The next faction's look, once every resolution of the one before is taken. */
		bool Restyle()
		{
			if (FactionIndex >= Factions.Num())
			{
				return Finish(FString());
			}
			const URTSUIStyleSet* Style = RTSUIScreenshot::LoadThemeStyle(Factions[FactionIndex]);
			if (!Style)
			{
				return Finish(FString::Printf(TEXT("no style set for %s; run ue.ps1 -Task setup"), *Factions[FactionIndex]));
			}
			World->GetGameInstance()->GetSubsystem<URTSUIStyleSubsystem>()->SetStyleOverride(Style);
			if (URTSHUDWidget* HUD = GetHUDWidget())
			{
				HUD->Rebuild();
			}
			ResolutionIndex = 0;
			GoTo(EStep::Resize);
			return true;
		}

		bool Resize()
		{
			const FIntPoint Target = Resolutions[ResolutionIndex];
			if (GetViewportSize() == Target)
			{
				GoTo(EStep::Layout);
				return true;
			}
			if (StepFrames == 1)
			{
				FSystemResolution::RequestResolutionChange(Target.X, Target.Y, EWindowMode::Windowed);
			}
			if (TimedOut(ResizeTimeout))
			{
				const FIntPoint Size = GetViewportSize();
				return Finish(FString::Printf(TEXT("the screen stayed %dx%d instead of %dx%d"), Size.X, Size.Y, Target.X, Target.Y));
			}
			return true;
		}

		bool RequestShot()
		{
			const FIntPoint Size = Resolutions[ResolutionIndex];
			CropRect = FIntRect();
			const URTSHUDWidget* HUD = GetHUDWidget();
			if (Element != FullScreenPanel && (!HUD || !RTSUIScreenshot::GetPanelRect(*HUD, *Element, Size, CropRect)))
			{
				UE_LOG(LogWorldDivide, Warning, TEXT("rts.UI.Screenshot: %s covers no pixel at %dx%d, so it has no crop"), *Element, Size.X, Size.Y);
			}
			Pixels.Reset();
			FScreenshotRequest::RequestScreenshot(/*bInShowUI*/ true, /*bInRestrictToGameViewport*/ true);
			GoTo(EStep::Capture);
			return true;
		}

		void OnCaptured(int32 Width, int32 Height, const TArray<FColor>& Colors)
		{
			if (Step == EStep::Capture)
			{
				PixelsSize = FIntPoint(Width, Height);
				Pixels = Colors;
			}
		}

		bool Capture()
		{
			if (Pixels.IsEmpty())
			{
				return TimedOut(CaptureTimeout) ? Finish(TEXT("no screenshot arrived")) : true;
			}
			const FIntPoint Size = Resolutions[ResolutionIndex];
			if (PixelsSize != Size || Pixels.Num() != Size.X * Size.Y)
			{
				return Finish(FString::Printf(TEXT("the screenshot is %dx%d instead of %dx%d"), PixelsSize.X, PixelsSize.Y, Size.X, Size.Y));
			}

			const FString& Faction = Factions[FactionIndex];
			if (!Save(RTSUIScreenshot::GetShotPath(Panel, Faction, Size, false), FImageView(Pixels.GetData(), Size.X, Size.Y)))
			{
				return Finish(TEXT("a file could not be written"));
			}
			if (CropRect.Area() > 0)
			{
				TArray<FColor> Crop;
				Crop.Reserve(CropRect.Area());
				for (int32 Y = CropRect.Min.Y; Y < CropRect.Max.Y; ++Y)
				{
					Crop.Append(&Pixels[Y * Size.X + CropRect.Min.X], CropRect.Width());
				}
				if (!Save(RTSUIScreenshot::GetShotPath(Panel, Faction, Size, true), FImageView(Crop.GetData(), CropRect.Width(), CropRect.Height())))
				{
					return Finish(TEXT("a file could not be written"));
				}
			}

			if (++ResolutionIndex < Resolutions.Num())
			{
				GoTo(EStep::Resize);
			}
			else
			{
				++FactionIndex;
				GoTo(EStep::Restyle);
			}
			return true;
		}

		bool Save(const FString& Path, const FImageView& Image)
		{
			if (!FImageUtils::SaveImageByExtension(*Path, Image))
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.UI.Screenshot: could not write %s"), *Path);
				return false;
			}
			UE_LOG(LogWorldDivide, Display, TEXT("rts.UI.Screenshot: %s"), *FPaths::ConvertRelativePathToFull(Path));
			++Written;
			return true;
		}

		/** Gives the player back their look, size and a running game; false, so the ticker drops the run. */
		bool Finish(const FString& Error)
		{
			if (World.IsValid())
			{
				World->GetGameInstance()->GetSubsystem<URTSUIStyleSubsystem>()->SetStyleOverride(nullptr);
				if (URTSHUDWidget* HUD = GetHUDWidget())
				{
					HUD->Rebuild();
				}
				UGameplayStatics::SetGamePaused(World.Get(), false);
				IConsoleVariable* MotionBlur = IConsoleManager::Get().FindConsoleVariable(MotionBlurVariable);
				if (MotionBlur && SavedMotionBlur != INDEX_NONE)
				{
					MotionBlur->Set(SavedMotionBlur, ECVF_SetByConsole);
				}
				if (World->WorldType != EWorldType::PIE && OriginalSize.X > 0 && GetViewportSize() != OriginalSize)
				{
					FSystemResolution::RequestResolutionChange(OriginalSize.X, OriginalSize.Y, EWindowMode::Windowed);
				}
			}
			if (Error.IsEmpty())
			{
				UE_LOG(LogWorldDivide, Display, TEXT("rts.UI.Screenshot: wrote %d files"), Written);
			}
			else
			{
				UE_LOG(LogWorldDivide, Error, TEXT("rts.UI.Screenshot failed: %s"), *Error);
			}
			UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
			if (FParse::Param(FCommandLine::Get(), QuitParam))
			{
				FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
			}
			// The ticker holds the last reference until this returns.
			Active.Reset();
			return false;
		}

		static TSharedPtr<FScreenshotRun> Active;

		TWeakObjectPtr<UWorld> World;
		FString Panel;
		FString Element;
		FString Scene;
		TArray<FString> Factions;
		TArray<FIntPoint> Resolutions;
		FIntPoint OriginalSize = FIntPoint::ZeroValue;
		int32 SavedMotionBlur = INDEX_NONE;

		EStep Step = EStep::WaitForHUD;
		int32 StepFrames = 0;
		double StepStart = 0.0;
		int32 FactionIndex = 0;
		int32 ResolutionIndex = 0;
		int32 Written = 0;

		FIntRect CropRect;
		TArray<FColor> Pixels;
		FIntPoint PixelsSize = FIntPoint::ZeroValue;

		FDelegateHandle CaptureHandle;
		FTSTicker::FDelegateHandle TickHandle;
	};

	TSharedPtr<FScreenshotRun> FScreenshotRun::Active;

	void RunScreenshotCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (!World || !World->IsGameWorld())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.UI.Screenshot needs a running game"));
			return;
		}
		if (FScreenshotRun::IsRunning())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.UI.Screenshot is already running"));
			return;
		}

		const TArray<FString> Themes = RTSUIScreenshot::GetFactionThemes();
		FString Panel = FullScreenPanel;
		TArray<FString> Factions;
		for (const FString& Arg : Args)
		{
			if (const FString* Theme = Themes.FindByPredicate([&Arg](const FString& Name) { return Name.Equals(Arg, ESearchCase::IgnoreCase); }))
			{
				Factions = { *Theme };
			}
			else
			{
				Panel = Arg;
			}
		}
		if (Themes.IsEmpty())
		{
			Output.Log(ELogVerbosity::Error, TEXT("rts.UI.Screenshot: no theme files in UI/Themes"));
			return;
		}
		if (Factions.IsEmpty())
		{
			Factions = Themes;
		}
		Output.Logf(TEXT("rts.UI.Screenshot: %s for %s, to %s"), *Panel, *FString::Join(Factions, TEXT(", ")),
			*FPaths::ConvertRelativePathToFull(FPaths::GetPath(RTSUIScreenshot::GetShotPath(Panel, Factions[0], FIntPoint::ZeroValue, false))));
		FScreenshotRun::Start(MakeShared<FScreenshotRun>(*World, Panel, MoveTemp(Factions)));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice ScreenshotCommand(
		TEXT("rts.UI.Screenshot"),
		TEXT("rts.UI.Screenshot [Panel[@Scene]] [Faction]: puts the match in the UI test state (and Scene, see RTSUITestState::ApplyScene) ")
		TEXT("and saves the HUD of each faction at 1920x1080, 2560x1440, 3440x1440 and 1680x1050 (in PIE at the viewport's size) to ")
		TEXT("Saved/UIShots, with a crop of the layout element Panel."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RunScreenshotCommand));
}

TConstArrayView<FIntPoint> RTSUIScreenshot::GetResolutions()
{
	static const FIntPoint Resolutions[] = { FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3440, 1440), FIntPoint(1680, 1050) };
	return Resolutions;
}

TArray<FString> RTSUIScreenshot::GetFactionThemes()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(FPaths::ProjectDir(), TEXT("UI/Themes/*.theme.json")), true, false);
	TArray<FString> Themes;
	for (const FString& File : Files)
	{
		const FString Theme = File.LeftChop(FString(TEXT(".theme.json")).Len());
		if (!Theme.Equals(NeutralTheme, ESearchCase::IgnoreCase))
		{
			Themes.Add(Theme);
		}
	}
	Themes.Sort();
	return Themes;
}

const URTSUIStyleSet* RTSUIScreenshot::LoadThemeStyle(const FString& Theme)
{
	const FString Name = TEXT("DA_UIStyle_") + Theme;
	return LoadObject<URTSUIStyleSet>(nullptr, *FString::Printf(TEXT("/Game/UI/Styles/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn);
}

FString RTSUIScreenshot::GetShotPath(const FString& Panel, const FString& Faction, const FIntPoint& Size, bool bCrop)
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UIShots"), Panel,
		FString::Printf(TEXT("%s_%dx%d%s.png"), *Faction, Size.X, Size.Y, bCrop ? TEXT("_crop") : TEXT("")));
}

bool RTSUIScreenshot::GetPanelRect(const URTSHUDWidget& HUD, FName Panel, const FIntPoint& ScreenSize, FIntRect& OutRect)
{
	const URTSLayoutWidget* Layout = HUD.GetLayout();
	const UWidget* Element = Layout ? Layout->FindElement(Panel) : nullptr;
	if (!Element)
	{
		return false;
	}
	// The HUD covers the player's viewport, and absolute units are pixels.
	const FGeometry& Geometry = Element->GetCachedGeometry();
	const FVector2D Min = Geometry.GetAbsolutePosition() - HUD.GetCachedGeometry().GetAbsolutePosition();
	const FVector2D Max = Min + Geometry.GetAbsoluteSize();
	OutRect = FIntRect(
		FIntPoint(FMath::Clamp(FMath::RoundToInt32(Min.X), 0, ScreenSize.X), FMath::Clamp(FMath::RoundToInt32(Min.Y), 0, ScreenSize.Y)),
		FIntPoint(FMath::Clamp(FMath::RoundToInt32(Max.X), 0, ScreenSize.X), FMath::Clamp(FMath::RoundToInt32(Max.Y), 0, ScreenSize.Y)));
	return OutRect.Area() > 0;
}

#endif
