#include "Player/RTSHUD.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Buildings/RTSSuperweaponComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Player/RTSPlayerController.h"
#include "UI/RTSHUDWidget.h"
#include "Units/RTSUnit.h"

namespace
{
	const FLinearColor SelectionColor(0.2f, 1.f, 0.3f, 1.f);
	const FLinearColor SelectionFill(0.2f, 1.f, 0.3f, 0.12f);
	const FLinearColor HealthBackground(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor ConstructionColor(1.f, 0.8f, 0.1f, 1.f);
	const FLinearColor MenuColor(0.9f, 0.9f, 0.9f, 1.f);
	const FLinearColor UnaffordableColor(0.45f, 0.45f, 0.45f, 1.f);
	const FLinearColor VictoryColor(0.3f, 1.f, 0.4f, 1.f);
	const FLinearColor DefeatColor(1.f, 0.3f, 0.25f, 1.f);
	const FLinearColor UnitAlertColor(1.f, 0.65f, 0.2f, 1.f);

	constexpr float ConstructionBarWidth = 60.f;
	constexpr float ButtonPadding = 8.f;
	constexpr float BarSpacing = 8.f;
	constexpr float MenuMargin = 16.f;
	constexpr float MenuLineHeight = 18.f;
	constexpr float ScoreboardSwatchSize = 12.f;
	constexpr float AttackAlertSeconds = 4.f;
	constexpr float AttackAlertTextScale = 1.5f;
	constexpr float AttackRingRadius = 40.f;
	constexpr float AttackRingPulse = 16.f;
	constexpr int32 AttackRingSegments = 24;
	constexpr float SuperweaponAlertTop = 60.f;
	constexpr float OrderRefusalSeconds = 2.f;
	constexpr float OrderRefusalTop = 120.f;

	/** A ring around Center, in segments. */
	void DrawRing(AHUD& HUD, const FVector2D& Center, float Radius, const FLinearColor& Color)
	{
		for (int32 Index = 0; Index < AttackRingSegments; ++Index)
		{
			const float From = 2.f * UE_PI * Index / AttackRingSegments;
			const float To = 2.f * UE_PI * (Index + 1) / AttackRingSegments;
			HUD.DrawLine(Center.X + Radius * FMath::Cos(From), Center.Y + Radius * FMath::Sin(From),
				Center.X + Radius * FMath::Cos(To), Center.Y + Radius * FMath::Sin(To), Color, 2.f);
		}
	}
}

ARTSHUD::ARTSHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARTSHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureHUDWidget();
}

void ARTSHUD::EnsureHUDWidget()
{
	// A joining client's controller gets its local player after the HUD spawns.
	if (HUDWidget || !PlayerOwner || !PlayerOwner->IsLocalController() || !PlayerOwner->GetLocalPlayer())
	{
		return;
	}
	TSubclassOf<URTSHUDWidget> WidgetClass = GetDefault<URTSSettings>()->HUDWidgetClass.LoadSynchronous();
	if (!WidgetClass)
	{
		WidgetClass = URTSHUDWidget::StaticClass();
	}
	HUDWidget = CreateWidget<URTSHUDWidget>(PlayerOwner, WidgetClass, TEXT("HUDWidget"));
	if (HUDWidget)
	{
		HUDWidget->AddToPlayerScreen();
	}
}

void ARTSHUD::DrawHUD()
{
	Super::DrawHUD();

	LeftInset = MenuMargin + (HUDWidget ? HUDWidget->GetLeftColumnBottom() : 0.f);
	RightInset = MenuMargin + (HUDWidget ? HUDWidget->GetRightColumnBottom() : 0.f);
	BottomInset = MenuMargin + (HUDWidget ? HUDWidget->GetBottomFrameHeight() : 0.f);

	const ARTSPlayerController* Controller = Cast<ARTSPlayerController>(GetOwningPlayerController());
	if (!Controller)
	{
		return;
	}

	for (const TWeakObjectPtr<ARTSUnit>& Unit : Controller->GetSelectedUnits())
	{
		if (Unit.IsValid())
		{
			DrawUnitMarker(*Controller, *Unit);
		}
	}

	// A selected unit under the cursor already has its bar.
	ARTSUnit* HoveredUnit = Controller->GetUnitUnderCursor();
	if (HoveredUnit && !Controller->GetSelectedUnits().Contains(HoveredUnit))
	{
		DrawUnitHealth(*Controller, *HoveredUnit);
	}

	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == Controller->GetTeamId() && !It->IsConstructed())
		{
			DrawConstructionBar(*Controller, **It);
		}
	}

	// Once the match is over every order is refused, so the result screen replaces the menus.
	const bool bMatchOver = Controller->GetMatchResult() != ERTSMatchResult::InProgress;
	const ARTSBuilding* Selected = Controller->GetSelectedBuilding();
	if (Selected)
	{
		DrawBuildingMarker(*Controller, *Selected);
	}

	const ARTSBuilding* Hovered = Controller->GetBuildingUnderCursor();
	if (Hovered && Hovered != Selected)
	{
		DrawBuildingHealth(*Controller, *Hovered);
	}

	LaunchButton = FBox2D(ForceInit);
	if (!bMatchOver)
	{
		DrawPlacementHint(*Controller);
		if (const ARTSBuilding* Producer = Controller->GetProductionBuilding())
		{
			DrawRallyPoint(*Controller, *Producer);
		}
		DrawSuperweaponMenu(*Controller);
		DrawTargetingHint(*Controller);
	}
	DrawSuperweapons(*Controller, DrawScoreboard() + MenuLineHeight * 0.5f);
	if (!bMatchOver)
	{
		DrawAttackAlert(*Controller);
		DrawSuperweaponAlert(*Controller);
		DrawOrderRefusal(*Controller);
	}

	FVector2D Start;
	FVector2D End;
	if (Controller->GetSelectionBox(Start, End))
	{
		DrawSelectionBox(Start, End);
	}

}

void ARTSHUD::DrawSelectionBox(const FVector2D& Start, const FVector2D& End)
{
	const FVector2D Min = FVector2D::Min(Start, End);
	const FVector2D Max = FVector2D::Max(Start, End);

	DrawRect(SelectionFill, Min.X, Min.Y, Max.X - Min.X, Max.Y - Min.Y);
	DrawLine(Min.X, Min.Y, Max.X, Min.Y, SelectionColor);
	DrawLine(Max.X, Min.Y, Max.X, Max.Y, SelectionColor);
	DrawLine(Max.X, Max.Y, Min.X, Max.Y, SelectionColor);
	DrawLine(Min.X, Max.Y, Min.X, Min.Y, SelectionColor);
}

bool ARTSHUD::GetUnitScreenRect(const ARTSUnit& Unit, TFunctionRef<bool(const FVector&, FVector2D&)> Project, FBox2D& OutRect)
{
	const FBox Bounds = Unit.GetSelectionBounds();
	const FTransform Transform = Unit.GetActorTransform();
	FBox2D Rect(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Local((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
			(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
		FVector2D Screen;
		if (!Project(Transform.TransformPosition(Local), Screen))
		{
			return false;
		}
		Rect += Screen;
	}
	// Grown around its centre up to the minimum size on each axis.
	const FVector2D Grow = FVector2D::Max(FVector2D(MinUnitRectSize) - Rect.GetSize(), FVector2D::ZeroVector) * 0.5f;
	OutRect = FBox2D(Rect.Min - Grow, Rect.Max + Grow);
	return true;
}

bool ARTSHUD::GetUnitScreenRect(const ARTSPlayerController& Controller, const ARTSUnit& Unit, FBox2D& OutRect)
{
	return GetUnitScreenRect(Unit, [&Controller](const FVector& World, FVector2D& Screen)
	{
		return Controller.ProjectWorldLocationToScreen(World, Screen);
	}, OutRect);
}

void ARTSHUD::DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit)
{
	FBox2D Rect;
	if (!GetUnitScreenRect(Controller, Unit, Rect))
	{
		return;
	}
	DrawBrackets(Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y);
	DrawUnitHealthBar(Rect, Unit);
}

void ARTSHUD::DrawUnitHealth(const ARTSPlayerController& Controller, const ARTSUnit& Unit)
{
	FBox2D Rect;
	if (GetUnitScreenRect(Controller, Unit, Rect))
	{
		DrawUnitHealthBar(Rect, Unit);
	}
}

void ARTSHUD::DrawUnitHealthBar(const FBox2D& Rect, const ARTSUnit& Unit)
{
	DrawHealthBar(Rect.Min.X, Rect.Min.Y - BarSpacing, Rect.GetSize().X, Unit.GetHealthComponent()->GetHealthFraction());
}

void ARTSHUD::DrawBrackets(float Left, float Top, float Right, float Bottom)
{
	const float Corner = FMath::Min(Right - Left, Bottom - Top) * 0.2f;
	DrawLine(Left, Top, Left + Corner, Top, SelectionColor, 2.f);
	DrawLine(Left, Top, Left, Top + Corner, SelectionColor, 2.f);
	DrawLine(Right, Top, Right - Corner, Top, SelectionColor, 2.f);
	DrawLine(Right, Top, Right, Top + Corner, SelectionColor, 2.f);
	DrawLine(Left, Bottom, Left + Corner, Bottom, SelectionColor, 2.f);
	DrawLine(Left, Bottom, Left, Bottom - Corner, SelectionColor, 2.f);
	DrawLine(Right, Bottom, Right - Corner, Bottom, SelectionColor, 2.f);
	DrawLine(Right, Bottom, Right, Bottom - Corner, SelectionColor, 2.f);
}

void ARTSHUD::DrawBuildingMarker(const ARTSPlayerController& Controller, const ARTSBuilding& Building)
{
	const URTSBuildingData* Data = Building.GetBuildingData();
	if (!Data)
	{
		return;
	}

	// Brackets around the screen bounds of the footprint's ground corners, turned with the building.
	FVector Corners[4];
	Building.GetGroundFootprint().GetCorners(Corners);
	FBox2D Bounds(ForceInit);
	for (const FVector& Corner : Corners)
	{
		FVector2D Screen;
		if (!Controller.ProjectWorldLocationToScreen(Corner, Screen))
		{
			return;
		}
		Bounds += Screen;
	}
	DrawBrackets(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y);
	DrawBuildingHealth(Controller, Building);
}

void ARTSHUD::DrawHealthBar(float Left, float Top, float Width, float HealthFraction)
{
	DrawRect(HealthBackground, Left - 1.f, Top - 1.f, Width + 2.f, 6.f);
	DrawRect(FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::Green, HealthFraction), Left, Top, Width * HealthFraction, 4.f);
}

void ARTSHUD::DrawConstructionBar(const ARTSPlayerController& Controller, const ARTSBuilding& Building)
{
	FVector2D ScreenCenter;
	if (!Controller.ProjectWorldLocationToScreen(Building.GetActorLocation(), ScreenCenter))
	{
		return;
	}
	const float Left = ScreenCenter.X - ConstructionBarWidth * 0.5f;
	DrawRect(HealthBackground, Left - 1.f, ScreenCenter.Y - 1.f, ConstructionBarWidth + 2.f, 6.f);
	DrawRect(ConstructionColor, Left, ScreenCenter.Y, ConstructionBarWidth * Building.GetConstructionProgress(), 4.f);
}

void ARTSHUD::DrawBuildingHealth(const ARTSPlayerController& Controller, const ARTSBuilding& Building)
{
	FVector2D ScreenCenter;
	if (!Controller.ProjectWorldLocationToScreen(Building.GetActorLocation(), ScreenCenter))
	{
		return;
	}
	// Above the construction bar, which own construction sites show at the same spot.
	DrawHealthBar(ScreenCenter.X - ConstructionBarWidth * 0.5f, ScreenCenter.Y - BarSpacing, ConstructionBarWidth,
		Building.GetHealthComponent()->GetHealthFraction());
}

void ARTSHUD::DrawPlacementHint(const ARTSPlayerController& Controller)
{
	if (Controller.GetPlacementData())
	{
		DrawText(TEXT("Left click: build (Shift: keep placing)    R: rotate    Right click / Esc: cancel"), MenuColor, MenuMargin, LeftInset);
	}
}

void ARTSHUD::DrawRallyPoint(const ARTSPlayerController& Controller, const ARTSBuilding& Building)
{
	FVector2D From;
	FVector2D To;
	if (!Controller.ProjectWorldLocationToScreen(Building.GetActorLocation(), From)
		|| !Controller.ProjectWorldLocationToScreen(Building.GetProductionComponent()->GetRallyPoint(), To))
	{
		return;
	}
	DrawLine(From.X, From.Y, To.X, To.Y, SelectionColor, 1.f);
	DrawRect(SelectionColor, To.X - 4.f, To.Y - 4.f, 8.f, 8.f);
}

float ARTSHUD::DrawScoreboard()
{
	// Under the minimap and map buttons, one row per player: its team colour, then its name or AI label.
	float Y = RightInset;
	const ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>();
	if (!GameState)
	{
		return Y;
	}

	TArray<const ARTSPlayerState*> Players;
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		if (const ARTSPlayerState* RTSPlayer = Cast<ARTSPlayerState>(Player))
		{
			Players.Add(RTSPlayer);
		}
	}
	Players.Sort([](const ARTSPlayerState& A, const ARTSPlayerState& B) { return A.GetTeamId() < B.GetTeamId(); });

	for (const ARTSPlayerState* Player : Players)
	{
		const bool bInMatch = GameState->GetAliveTeams().Contains(Player->GetTeamId());
		const FString Text = Player->GetScoreboardLabel(bInMatch);
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Text, Width, Height);
		const float TextLeft = Canvas->ClipX - MenuMargin - Width;
		const float SwatchLeft = TextLeft - ButtonPadding - ScoreboardSwatchSize;
		const float SwatchTop = Y + (MenuLineHeight - ScoreboardSwatchSize) * 0.5f;
		DrawRect(HealthBackground, SwatchLeft - 1.f, SwatchTop - 1.f, ScoreboardSwatchSize + 2.f, ScoreboardSwatchSize + 2.f);
		DrawRect(ARTSUnit::GetTeamColor(Player->GetTeamId()), SwatchLeft, SwatchTop, ScoreboardSwatchSize, ScoreboardSwatchSize);
		DrawText(Text, bInMatch ? MenuColor : UnaffordableColor, TextLeft, Y);
		Y += MenuLineHeight;
	}
	return Y;
}

FString ARTSHUD::FormatCountdown(float Seconds)
{
	if (Seconds <= 0.f)
	{
		return TEXT("Ready");
	}
	const int32 Whole = FMath::CeilToInt(Seconds);
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

void ARTSHUD::DrawSuperweapons(const ARTSPlayerController& Controller, float Y)
{
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		const URTSSuperweaponComponent* Superweapon = It->GetSuperweaponComponent();
		if (!Superweapon->IsOperational())
		{
			continue;
		}

		// A row like the scoreboard's: team colour, then the countdown.
		FString Text = FString::Printf(TEXT("Superweapon  %s"), *FormatCountdown(Superweapon->GetChargeRemaining()));
		if (Superweapon->IsChargePaused())
		{
			Text += TEXT("  (low power)");
		}
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Text, Width, Height);
		const float TextLeft = Canvas->ClipX - MenuMargin - Width;
		const float SwatchLeft = TextLeft - ButtonPadding - ScoreboardSwatchSize;
		const float SwatchTop = Y + (MenuLineHeight - ScoreboardSwatchSize) * 0.5f;
		DrawRect(HealthBackground, SwatchLeft - 1.f, SwatchTop - 1.f, ScoreboardSwatchSize + 2.f, ScoreboardSwatchSize + 2.f);
		DrawRect(ARTSUnit::GetTeamColor(It->GetTeamId()), SwatchLeft, SwatchTop, ScoreboardSwatchSize, ScoreboardSwatchSize);
		const bool bReady = Superweapon->GetChargeRemaining() <= 0.f;
		DrawText(Text, bReady ? DefeatColor : Superweapon->IsChargePaused() ? UnaffordableColor : MenuColor, TextLeft, Y);
		Y += MenuLineHeight;

		// Every player sees where a strike is about to land.
		FVector2D Center;
		if (Superweapon->IsStrikePending() && Controller.ProjectWorldLocationToScreen(Superweapon->GetStrikeLocation(), Center))
		{
			const float Pulse = FMath::Abs(FMath::Sin(GetWorld()->GetTimeSeconds() * UE_PI * 4.f));
			DrawRing(*this, Center, AttackRingRadius * 2.f + AttackRingPulse * Pulse, DefeatColor);
			DrawRing(*this, Center, AttackRingRadius * 0.5f, DefeatColor);
		}
	}
}

void ARTSHUD::DrawSuperweaponMenu(const ARTSPlayerController& Controller)
{
	const ARTSBuilding* Building = Controller.GetSelectedSuperweapon();
	if (!Building)
	{
		return;
	}
	const URTSSuperweaponComponent* Superweapon = Building->GetSuperweaponComponent();
	float Y = LeftInset;
	DrawText(TEXT("Superweapon"), MenuColor, MenuMargin, Y);
	Y += MenuLineHeight;
	FString Text = TEXT("1  Launch");
	if (Superweapon->IsStrikePending())
	{
		Text += TEXT("  striking");
	}
	else if (Superweapon->GetChargeRemaining() > 0.f)
	{
		Text += FString::Printf(TEXT("  charging %s"), *FormatCountdown(Superweapon->GetChargeRemaining()));
	}
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height);
	LaunchButton = FBox2D(FVector2D(MenuMargin, Y), FVector2D(MenuMargin + Width, Y + MenuLineHeight));
	DrawText(Text, Controller.CanRequestLaunch(Building) ? MenuColor : UnaffordableColor, MenuMargin, Y);
}

bool ARTSHUD::IsOnLaunchButton(const FVector2D& Point) const
{
	return LaunchButton.bIsValid && LaunchButton.IsInside(Point);
}

void ARTSHUD::DrawSuperweaponAlert(const ARTSPlayerController& Controller)
{
	ERTSSuperweaponAlert Kind = ERTSSuperweaponAlert::Built;
	bool bOwn = false;
	float Age = 0.f;
	if (!Controller.GetLatestSuperweaponAlert(Kind, bOwn, Age) || Age >= AttackAlertSeconds)
	{
		return;
	}
	const TCHAR* Text = bOwn ? TEXT("Superweapon ready")
		: Kind == ERTSSuperweaponAlert::Built ? TEXT("Enemy superweapon built") : TEXT("Enemy superweapon ready");

	// Under the attack warning, which keeps the top.
	UFont* Font = GEngine->GetLargeFont();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, AttackAlertTextScale);
	const FVector2D Size(Width + ButtonPadding * 2.f, Height + ButtonPadding * 2.f);
	const FVector2D Min((Canvas->ClipX - Size.X) * 0.5f, MenuMargin + SuperweaponAlertTop);
	DrawRect(HealthBackground, Min.X, Min.Y, Size.X, Size.Y);
	DrawText(Text, bOwn ? VictoryColor : DefeatColor, Min.X + ButtonPadding, Min.Y + ButtonPadding, Font, AttackAlertTextScale);
}

void ARTSHUD::DrawAttackAlert(const ARTSPlayerController& Controller)
{
	// Base first: while both are fresh, the message names the base and both spots get a ring.
	const TCHAR* Text = nullptr;
	const FLinearColor* TextColor = nullptr;
	for (const ERTSAttackAlert Kind : { ERTSAttackAlert::Base, ERTSAttackAlert::Units })
	{
		FVector Location;
		float Age = 0.f;
		if (!Controller.GetLastAttackAlert(Kind, Location, Age) || Age >= AttackAlertSeconds)
		{
			continue;
		}
		const bool bBase = Kind == ERTSAttackAlert::Base;
		const FLinearColor& Color = bBase ? DefeatColor : UnitAlertColor;
		if (!Text)
		{
			Text = bBase ? TEXT("Your base is under attack") : TEXT("Your units are under attack");
			TextColor = &Color;
		}

		// A ring that pulses twice a second around the building or unit, while the spot is on screen.
		FVector2D Center;
		if (!Controller.ProjectWorldLocationToScreen(Location, Center))
		{
			continue;
		}
		DrawRing(*this, Center, AttackRingRadius + AttackRingPulse * FMath::Abs(FMath::Sin(Age * UE_PI * 2.f)), Color);
	}
	if (!Text)
	{
		return;
	}

	UFont* Font = GEngine->GetLargeFont();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, AttackAlertTextScale);
	const FVector2D Size(Width + ButtonPadding * 2.f, Height + ButtonPadding * 2.f);
	const FVector2D Min((Canvas->ClipX - Size.X) * 0.5f, MenuMargin);
	DrawRect(HealthBackground, Min.X, Min.Y, Size.X, Size.Y);
	DrawText(Text, *TextColor, Min.X + ButtonPadding, Min.Y + ButtonPadding, Font, AttackAlertTextScale);
}

void ARTSHUD::DrawOrderRefusal(const ARTSPlayerController& Controller)
{
	FVector Location;
	float Age = 0.f;
	if (!Controller.GetLastOrderRefusal(Location, Age) || Age >= OrderRefusalSeconds)
	{
		return;
	}
	FVector2D Center;
	if (Controller.ProjectWorldLocationToScreen(Location, Center))
	{
		DrawRing(*this, Center, AttackRingRadius, DefeatColor);
	}

	// Under the superweapon warning.
	const TCHAR* Text = TEXT("Cannot move there");
	UFont* Font = GEngine->GetLargeFont();
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, AttackAlertTextScale);
	const FVector2D Size(Width + ButtonPadding * 2.f, Height + ButtonPadding * 2.f);
	const FVector2D Min((Canvas->ClipX - Size.X) * 0.5f, MenuMargin + OrderRefusalTop);
	DrawRect(HealthBackground, Min.X, Min.Y, Size.X, Size.Y);
	DrawText(Text, DefeatColor, Min.X + ButtonPadding, Min.Y + ButtonPadding, Font, AttackAlertTextScale);
}

void ARTSHUD::DrawTargetingHint(const ARTSPlayerController& Controller)
{
	// At most one mode is armed at a time: arming one cancels the others.
	FString Text;
	FLinearColor Color = SelectionColor;
	if (Controller.IsAttackMoveArmed())
	{
		Text = TEXT("Attack-move: left click a target (Shift: queue)    Right click / Esc: cancel");
	}
	else if (Controller.IsForceFireArmed())
	{
		Text = TEXT("Force fire: left click a unit, building or the ground (Shift: queue)    Right click / Esc: cancel");
	}
	else if (Controller.IsSuperweaponTargeting())
	{
		Text = TEXT("Launch: left click a target    Right click / Esc: cancel");
		Color = DefeatColor;
	}
	else
	{
		return;
	}
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height);
	const FVector2D Size(Width + ButtonPadding * 2.f, Height + ButtonPadding * 2.f);
	const FVector2D Min((Canvas->ClipX - Size.X) * 0.5f, Canvas->ClipY - BottomInset - Size.Y);
	DrawRect(HealthBackground, Min.X, Min.Y, Size.X, Size.Y);
	DrawText(Text, Color, Min.X + ButtonPadding, Min.Y + ButtonPadding);
}

FString ARTSHUD::GetMatchResultTitle(ERTSMatchResult Result)
{
	switch (Result)
	{
	case ERTSMatchResult::Victory:
		return TEXT("Victory!");
	case ERTSMatchResult::Defeat:
		return TEXT("Defeated!");
	default:
		return FString();
	}
}

FString ARTSHUD::GetMatchResultMessage(ERTSMatchResult Result)
{
	switch (Result)
	{
	case ERTSMatchResult::Victory:
		return TEXT("The enemy has been eliminated.");
	case ERTSMatchResult::Defeat:
		return TEXT("Your forces have been eliminated.");
	default:
		return FString();
	}
}
