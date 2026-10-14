#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Core/RTSPlayerState.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestPIE.h"
#include "Tests/RTSTestUI.h"
#include "UI/Debug/RTSUIScreenshot.h"
#include "UI/Debug/RTSUITestState.h"
#include "UI/RTSHUDWidget.h"
#include "UI/RTSUIStyleSubsystem.h"
#include "Units/RTSUnit.h"

/**
 * The HUD screenshots (rts.UI.Screenshot) without taking any: every faction theme but Neutral has a style set, the style
 * override takes the place of the player's faction look, and on L_TestMap the UI test state gives the player 12,500
 * credits, low power, six selected units led by a builder and three units queued at a barracks, twice in a row without
 * piling up, while the resource bar crops to a rectangle inside the screen.
 */
namespace RTSUIScreenshotTests
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	constexpr double StartTimeout = 30.0;
	constexpr double LayoutTimeout = 10.0;
	const FIntPoint ScreenSize(1920, 1080);

	struct FState
	{
		double PhaseStart = 0.0;
	};

	int32 CountSpawned(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			Count += It->ActorHasTag(RTSUITestState::SpawnedTag) && !It->IsActorBeingDestroyed() ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUIScreenshotThemesTest, "WorldDivide.UI.Screenshot.Themes", RTSTests::TestFlags)
bool FRTSUIScreenshotThemesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Themes = RTSUIScreenshot::GetFactionThemes();
	TestTrue(FString::Printf(TEXT("The faction looks are the themes but Neutral (%s)"), *FString::Join(Themes, TEXT(", "))),
		Themes == TArray<FString>{ TEXT("China"), TEXT("Insurgents"), TEXT("USA") });
	for (const FString& Theme : Themes)
	{
		TestNotNull(FString::Printf(TEXT("%s has a style set"), *Theme), RTSUIScreenshot::LoadThemeStyle(Theme));
	}
	TestTrue(TEXT("A shot goes to Saved/UIShots/<Panel>"), RTSUIScreenshot::GetShotPath(TEXT("ResourceBar"), TEXT("USA"), FIntPoint(1920, 1080), true)
		.EndsWith(TEXT("UIShots/ResourceBar/USA_1920x1080_crop.png")));
	TestEqual(TEXT("Three resolutions"), RTSUIScreenshot::GetResolutions().Num(), 3);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSUIShotWaitForPlay, FAutomationTestBase*, Test, TSharedRef<RTSUIScreenshotTests::FState>, State);
bool FRTSUIShotWaitForPlay::Update()
{
	if (State->PhaseStart == 0.0)
	{
		State->PhaseStart = FPlatformTime::Seconds();
	}
	UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
	const ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const ARTSPlayerState* Player = Controller ? Controller->GetPlayerState<ARTSPlayerState>() : nullptr;
	if (World && World->HasBegunPlay() && Player && Player->GetFaction())
	{
		State->PhaseStart = FPlatformTime::Seconds();
		return true;
	}
	if (FPlatformTime::Seconds() - State->PhaseStart > RTSUIScreenshotTests::StartTimeout)
	{
		Test->AddError(TEXT("PIE did not start or the player got no faction in time"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRTSUIShotCheckState, FAutomationTestBase*, Test);
bool FRTSUIShotCheckState::Update()
{
	using namespace RTSUIScreenshotTests;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	ARTSPlayerController* Controller = World ? Cast<ARTSPlayerController>(World->GetFirstPlayerController()) : nullptr;
	ARTSPlayerState* Player = Controller ? Controller->GetPlayerState<ARTSPlayerState>() : nullptr;
	if (!Player)
	{
		Test->AddError(TEXT("No player in the PIE world"));
		return true;
	}

	// The override stands in for the faction's look, and clearing it gives that back.
	URTSUIStyleSubsystem* Styles = World->GetGameInstance()->GetSubsystem<URTSUIStyleSubsystem>();
	const URTSUIStyleSet* USA = RTSUIScreenshot::LoadThemeStyle(TEXT("USA"));
	const TArray<const URTSUIStyleSet*> FactionChain = Styles->GetStyleChain();
	Styles->SetStyleOverride(USA);
	const TArray<const URTSUIStyleSet*> OverriddenChain = Styles->GetStyleChain();
	Test->TestTrue(TEXT("The override is tried first"), USA && !OverriddenChain.IsEmpty() && OverriddenChain[0] == USA);
	Styles->SetStyleOverride(nullptr);
	Test->TestTrue(TEXT("Cleared, the faction's chain is back"), Styles->GetStyleChain() == FactionChain);

	FString Error;
	if (!Test->TestTrue(TEXT("The UI test state goes up"), RTSUITestState::Apply(*Controller, Error)))
	{
		Test->AddError(Error);
		return true;
	}
	const int32 Spawned = CountSpawned(*World);
	Test->TestTrue(TEXT("Again, over the first"), RTSUITestState::Apply(*Controller, Error));
	Test->TestEqual(TEXT("The second replaced the first"), CountSpawned(*World), Spawned);

	Test->TestEqual(TEXT("Credits"), Player->GetCredits(), RTSUITestState::Credits);
	Test->TestTrue(TEXT("Low power"), Player->IsLowPower());
	Test->TestEqual(TEXT("Selected units"), Controller->GetSelectedUnits().Num(), RTSUITestState::SelectedUnits);
	Test->TestNotNull(TEXT("A builder leads the selection, so the command grid shows its build menu"), Controller->GetSelectedBuilder());
	Test->TestFalse(TEXT("The build menu is not empty"), Controller->GetBuildMenu().IsEmpty());
	const ARTSBuilding* Barracks = RTSUITestState::FindQueueBuilding(*World);
	Test->TestEqual(TEXT("Queued at the barracks"), Barracks ? Barracks->GetProductionComponent()->GetQueue().Num() : 0, RTSUITestState::QueuedItems);
	FVector AlertLocation;
	float AlertAge = 0.f;
	Test->TestTrue(TEXT("An attack warning came in"), Controller->GetLastAttackAlert(ERTSAttackAlert::Base, AlertLocation, AlertAge));
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSUIShotCheckCrop, FAutomationTestBase*, Test, TSharedRef<RTSUIScreenshotTests::FState>, State);
bool FRTSUIShotCheckCrop::Update()
{
	using namespace RTSUIScreenshotTests;
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const ARTSHUD* HUD = Controller ? Controller->GetHUD<ARTSHUD>() : nullptr;
	URTSHUDWidget* Widget = HUD ? HUD->GetHUDWidget() : nullptr;
	if (!Widget)
	{
		Test->AddError(TEXT("The player has no HUD widget"));
		return true;
	}
	if (!FApp::CanEverRender())
	{
		RTSTests::LayOutHeadless(*Widget, FVector2D(ScreenSize));
	}
	FIntRect Rect;
	if (!RTSUIScreenshot::GetPanelRect(*Widget, TEXT("ResourceBar"), ScreenSize, Rect))
	{
		if (FPlatformTime::Seconds() - State->PhaseStart > LayoutTimeout)
		{
			Test->AddError(TEXT("The resource bar never covered a pixel"));
			return true;
		}
		return false;
	}
	Test->TestTrue(FString::Printf(TEXT("The crop %s lies inside the screen"), *Rect.ToString()),
		FIntRect(FIntPoint::ZeroValue, ScreenSize).Contains(Rect) && Rect.Area() > 0);
	Test->TestFalse(TEXT("No crop of an element the layout lacks"), RTSUIScreenshot::GetPanelRect(*Widget, TEXT("NoSuchPanel"), ScreenSize, Rect));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUIScreenshotStateTest, "WorldDivide.UI.Screenshot.TestState", RTSTests::TestFlags)
bool FRTSUIScreenshotStateTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(RTSUIScreenshotTests::TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), RTSUIScreenshotTests::TestMap));
		return false;
	}
	const TSharedRef<RTSUIScreenshotTests::FState> State = MakeShared<RTSUIScreenshotTests::FState>();
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(FRTSUIShotWaitForPlay(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSUIShotCheckState(this));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSUIShotCheckCrop(this, State));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
