#include "Player/RTSHUD.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUpgradeData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Player/RTSPlayerController.h"
#include "Units/RTSUnit.h"

namespace
{
	const FLinearColor SelectionColor(0.2f, 1.f, 0.3f, 1.f);
	const FLinearColor SelectionFill(0.2f, 1.f, 0.3f, 0.12f);
	const FLinearColor HealthBackground(0.f, 0.f, 0.f, 0.6f);
	const FLinearColor ConstructionColor(1.f, 0.8f, 0.1f, 1.f);
	const FLinearColor MenuColor(0.9f, 0.9f, 0.9f, 1.f);
	const FLinearColor UnaffordableColor(0.45f, 0.45f, 0.45f, 1.f);
	const FLinearColor CreditsColor(1.f, 0.85f, 0.3f, 1.f);
	const FLinearColor PowerColor(0.4f, 0.9f, 1.f, 1.f);
	const FLinearColor LowPowerColor(1.f, 0.25f, 0.2f, 1.f);
	const FLinearColor ButtonFill(0.35f, 0.08f, 0.05f, 0.85f);
	const FLinearColor ButtonHoverFill(0.6f, 0.12f, 0.08f, 0.9f);
	const FLinearColor ProgressFill(1.f, 0.8f, 0.1f, 0.45f);
	const FLinearColor VictoryColor(0.3f, 1.f, 0.4f, 1.f);
	const FLinearColor DefeatColor(1.f, 0.3f, 0.25f, 1.f);
	const FLinearColor UnitAlertColor(1.f, 0.65f, 0.2f, 1.f);
	const FLinearColor ResultDim(0.f, 0.f, 0.f, 0.45f);
	const FLinearColor ResultBackdrop(0.05f, 0.05f, 0.05f, 0.9f);

	constexpr float ConstructionBarWidth = 60.f;
	constexpr float QueueEntryWidth = 180.f;
	constexpr float ButtonPadding = 8.f;
	constexpr float BarSpacing = 8.f;
	constexpr float MenuMargin = 16.f;
	constexpr float MenuLineHeight = 18.f;
	constexpr float ResultTextScale = 3.f;
	constexpr float ResultPanelMinWidth = 360.f;
	constexpr float ResultPanelPadding = 24.f;
	constexpr float ResultBorder = 2.f;
	constexpr float AttackAlertSeconds = 4.f;
	constexpr float AttackAlertTextScale = 1.5f;
	constexpr float AttackRingRadius = 40.f;
	constexpr float AttackRingPulse = 16.f;
	constexpr int32 AttackRingSegments = 24;
}

void ARTSHUD::DrawHUD()
{
	Super::DrawHUD();

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

	// Once the match is over every order is refused, so the pop-up replaces the menus.
	const bool bMatchOver = Controller->GetMatchResult() != ERTSMatchResult::InProgress;
	CancelButton = FBox2D(ForceInit);
	const ARTSBuilding* Selected = Controller->GetSelectedBuilding();
	if (Selected)
	{
		DrawBuildingMarker(*Controller, *Selected);
		if (!bMatchOver && Controller->CanCancelConstruction(Selected))
		{
			DrawCancelButton(*Selected);
		}
	}

	const ARTSBuilding* Hovered = Controller->GetBuildingUnderCursor();
	if (Hovered && Hovered != Selected)
	{
		DrawBuildingHealth(*Controller, *Hovered);
	}

	ProduceButtons.Reset();
	QueueButtons.Reset();
	if (!bMatchOver)
	{
		DrawBuildMenu(*Controller);
		DrawProduction(*Controller);
		DrawAttackMoveHint(*Controller);
	}
	DrawCredits(*Controller);
	DrawPower(*Controller);
	if (!bMatchOver)
	{
		DrawAttackAlert(*Controller);
	}

	FVector2D Start;
	FVector2D End;
	if (Controller->GetSelectionBox(Start, End))
	{
		DrawSelectionBox(Start, End);
	}

	RestartButton = FBox2D(ForceInit);
	DrawMatchResult(*Controller);
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

bool ARTSHUD::GetUnitScreenCircle(const ARTSPlayerController& Controller, const ARTSUnit& Unit, FVector2D& OutCenter, float& OutRadius)
{
	const FVector Center = Unit.GetActorLocation();
	const FVector CameraRight = Controller.PlayerCameraManager
		? Controller.PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector)
		: FVector::RightVector;

	FVector2D ScreenEdge;
	if (!Controller.ProjectWorldLocationToScreen(Center, OutCenter)
		|| !Controller.ProjectWorldLocationToScreen(Center + CameraRight * Unit.GetSelectionRadius(), ScreenEdge))
	{
		return false;
	}
	OutRadius = FMath::Max(8.f, FVector2D::Distance(OutCenter, ScreenEdge));
	return true;
}

void ARTSHUD::DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit)
{
	FVector2D Center;
	float Radius = 0.f;
	if (!GetUnitScreenCircle(Controller, Unit, Center, Radius))
	{
		return;
	}
	DrawBrackets(Center.X - Radius, Center.Y - Radius, Center.X + Radius, Center.Y + Radius);
	DrawUnitHealthBar(Center, Radius, Unit);
}

void ARTSHUD::DrawUnitHealth(const ARTSPlayerController& Controller, const ARTSUnit& Unit)
{
	FVector2D Center;
	float Radius = 0.f;
	if (GetUnitScreenCircle(Controller, Unit, Center, Radius))
	{
		DrawUnitHealthBar(Center, Radius, Unit);
	}
}

void ARTSHUD::DrawUnitHealthBar(const FVector2D& Center, float Radius, const ARTSUnit& Unit)
{
	DrawHealthBar(Center.X - Radius, Center.Y - Radius - BarSpacing, Radius * 2.f, Unit.GetHealthComponent()->GetHealthFraction());
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

	// Brackets around the screen bounds of the footprint's ground corners.
	const FVector Center = Building.GetActorLocation();
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Data->Footprint);
	FBox2D Bounds(ForceInit);
	for (const FVector2D Corner : { FVector2D(-1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(1.f, 1.f) })
	{
		FVector2D Screen;
		if (!Controller.ProjectWorldLocationToScreen(Center + FVector(Corner.X * Extent.X, Corner.Y * Extent.Y, 0.f), Screen))
		{
			return;
		}
		Bounds += Screen;
	}
	DrawBrackets(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y);
	DrawBuildingHealth(Controller, Building);
}

void ARTSHUD::DrawCancelButton(const ARTSBuilding& Building)
{
	const FString Text = FString::Printf(TEXT("Cancel construction (+%d)   [Del]"), Building.GetCancelRefund());
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height);
	const FVector2D Size(Width + ButtonPadding * 2.f, Height + ButtonPadding * 2.f);
	const FVector2D Min((Canvas->ClipX - Size.X) * 0.5f, Canvas->ClipY - MenuMargin - Size.Y);
	CancelButton = FBox2D(Min, Min + Size);

	FVector2D Mouse;
	const APlayerController* Controller = GetOwningPlayerController();
	const bool bHovered = Controller && Controller->GetMousePosition(Mouse.X, Mouse.Y) && IsOnCancelButton(Mouse);
	DrawRect(bHovered ? ButtonHoverFill : ButtonFill, Min.X, Min.Y, Size.X, Size.Y);
	DrawText(Text, MenuColor, Min.X + ButtonPadding, Min.Y + ButtonPadding);
}

bool ARTSHUD::IsOnCancelButton(const FVector2D& Point) const
{
	return CancelButton.bIsValid && CancelButton.IsInside(Point);
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

void ARTSHUD::DrawBuildMenu(const ARTSPlayerController& Controller)
{
	const TArray<URTSBuildingData*> Menu = Controller.GetBuildMenu();
	if (Menu.IsEmpty())
	{
		return;
	}

	float Y = MenuMargin;
	DrawText(Controller.GetSelectedBuilder() ? TEXT("Build") : TEXT("Build (debug)"), MenuColor, MenuMargin, Y);
	const int32 Count = FMath::Min(Menu.Num(), ARTSPlayerController::MaxBuildHotkeys);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const URTSBuildingData* Data = Menu[Index];
		if (!Data)
		{
			continue;
		}
		Y += MenuLineHeight;
		const bool bPlacing = Controller.GetPlacementData() == Data;
		FString Text = FString::Printf(TEXT("%d  %s  (%d)"), Index + 1, *Data->DisplayName.ToString(), Data->Cost);
		const URTSEntityData* Missing = Controller.GetMissingPrerequisite(*Data);
		const bool bAtLimit = Controller.IsAtBuildLimit(*Data);
		if (Missing)
		{
			Text += FString::Printf(TEXT("  needs %s"), *Missing->DisplayName.ToString());
		}
		else if (bAtLimit)
		{
			Text += TEXT("  limit reached");
		}
		const bool bAvailable = Controller.CanAfford(*Data) && !Missing && !bAtLimit;
		const FLinearColor Color = bPlacing ? SelectionColor : bAvailable ? MenuColor : UnaffordableColor;
		DrawText(Text, Color, MenuMargin, Y);
	}
	if (Controller.GetPlacementData())
	{
		Y += MenuLineHeight;
		DrawText(TEXT("Left click: build (Shift: queue)    Right click / Esc: cancel"), MenuColor, MenuMargin, Y);
	}
}

void ARTSHUD::DrawProduction(const ARTSPlayerController& Controller)
{
	const ARTSBuilding* Building = Controller.GetProductionBuilding();
	if (!Building)
	{
		return;
	}
	DrawRallyPoint(Controller, *Building);

	float Y = MenuMargin;
	DrawText(TEXT("Produce"), MenuColor, MenuMargin, Y);
	const TArray<URTSEntityData*> Menu = Controller.GetProductionMenu();
	const int32 Count = FMath::Min(Menu.Num(), ARTSPlayerController::MaxBuildHotkeys);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Y += MenuLineHeight;
		const URTSEntityData* Data = Menu[Index];
		if (!Data)
		{
			ProduceButtons.Add(FBox2D(ForceInit));
			continue;
		}
		FString Text = FString::Printf(TEXT("%d  %s  (%d)"), Index + 1, *Data->DisplayName.ToString(), Data->Cost);
		const URTSUpgradeData* Upgrade = Cast<URTSUpgradeData>(Data);
		if (Upgrade && Controller.HasUpgrade(*Upgrade))
		{
			Text = FString::Printf(TEXT("%d  %s  done"), Index + 1, *Data->DisplayName.ToString());
		}
		else if (Upgrade && RTSProduction::IsUpgradeQueued(*GetWorld(), Controller.GetTeamId(), *Upgrade))
		{
			Text += TEXT("  researching");
		}
		else if (const URTSEntityData* Missing = Controller.GetMissingPrerequisite(*Data))
		{
			Text += FString::Printf(TEXT("  needs %s"), *Missing->DisplayName.ToString());
		}
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Text, Width, Height);
		ProduceButtons.Add(FBox2D(FVector2D(MenuMargin, Y), FVector2D(MenuMargin + Width, Y + MenuLineHeight)));
		DrawText(Text, Controller.CanRequestProduce(Building, Data) ? MenuColor : UnaffordableColor, MenuMargin, Y);
	}

	const URTSProductionComponent* Production = Building->GetProductionComponent();
	const TArray<TObjectPtr<URTSEntityData>>& Queue = Production->GetQueue();
	Y += MenuLineHeight * 1.5f;
	DrawText(FString::Printf(TEXT("Queue  %d / %d"), Queue.Num(), URTSProductionComponent::MaxQueue), MenuColor, MenuMargin, Y);

	FVector2D Mouse;
	const bool bHasMouse = Controller.GetMousePosition(Mouse.X, Mouse.Y);
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		Y += MenuLineHeight + 2.f;
		const FBox2D Box(FVector2D(MenuMargin, Y), FVector2D(MenuMargin + QueueEntryWidth, Y + MenuLineHeight));
		QueueButtons.Add(Box);
		DrawRect(bHasMouse && Box.IsInside(Mouse) ? ButtonHoverFill : HealthBackground, Box.Min.X, Box.Min.Y, QueueEntryWidth, MenuLineHeight);
		if (Index == 0)
		{
			DrawRect(ProgressFill, Box.Min.X, Box.Min.Y, QueueEntryWidth * Production->GetProgress(), MenuLineHeight);
		}
		DrawText(Queue[Index] ? Queue[Index]->DisplayName.ToString() : FString(), MenuColor, Box.Min.X + 4.f, Box.Min.Y + 1.f);
	}

	Y += MenuLineHeight * 1.5f;
	DrawText(TEXT("1-9 / click: produce    Right click: rally point    Click a queued entry / Del: cancel"), MenuColor, MenuMargin, Y);
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

int32 ARTSHUD::GetProduceButtonAt(const FVector2D& Point) const
{
	return ProduceButtons.IndexOfByPredicate([&Point](const FBox2D& Box) { return Box.bIsValid && Box.IsInside(Point); });
}

int32 ARTSHUD::GetQueueButtonAt(const FVector2D& Point) const
{
	return QueueButtons.IndexOfByPredicate([&Point](const FBox2D& Box) { return Box.bIsValid && Box.IsInside(Point); });
}

void ARTSHUD::DrawCredits(const ARTSPlayerController& Controller)
{
	const FString Text = FString::Printf(TEXT("Credits: %d"), Controller.GetCredits());
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height);
	DrawText(Text, CreditsColor, Canvas->ClipX - MenuMargin - Width, MenuMargin);
}

void ARTSHUD::DrawPower(const ARTSPlayerController& Controller)
{
	const ARTSPlayerState* State = Controller.GetPlayerState<ARTSPlayerState>();
	const URTSFactionData* Faction = State ? State->GetFaction() : nullptr;
	if (!State || (Faction && !Faction->bUsesPower))
	{
		return;
	}
	const FString Text = FString::Printf(TEXT("Power: %d / %d"), State->GetPowerProduced(), State->GetPowerConsumed());
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height);
	DrawText(Text, State->IsLowPower() ? LowPowerColor : PowerColor, Canvas->ClipX - MenuMargin - Width, MenuMargin + MenuLineHeight);
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
		const float Radius = AttackRingRadius + AttackRingPulse * FMath::Abs(FMath::Sin(Age * UE_PI * 2.f));
		for (int32 Index = 0; Index < AttackRingSegments; ++Index)
		{
			const float From = 2.f * UE_PI * Index / AttackRingSegments;
			const float To = 2.f * UE_PI * (Index + 1) / AttackRingSegments;
			DrawLine(Center.X + Radius * FMath::Cos(From), Center.Y + Radius * FMath::Sin(From),
				Center.X + Radius * FMath::Cos(To), Center.Y + Radius * FMath::Sin(To), Color, 2.f);
		}
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

void ARTSHUD::DrawAttackMoveHint(const ARTSPlayerController& Controller)
{
	if (!Controller.IsAttackMoveArmed())
	{
		return;
	}
	const FString Text = TEXT("Attack-move: left click a target (Shift: queue)    Right click / Esc: cancel");
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height);
	const FVector2D Size(Width + ButtonPadding * 2.f, Height + ButtonPadding * 2.f);
	const FVector2D Min((Canvas->ClipX - Size.X) * 0.5f, Canvas->ClipY - MenuMargin - Size.Y);
	DrawRect(HealthBackground, Min.X, Min.Y, Size.X, Size.Y);
	DrawText(Text, SelectionColor, Min.X + ButtonPadding, Min.Y + ButtonPadding);
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

void ARTSHUD::DrawMatchResult(const ARTSPlayerController& Controller)
{
	const ERTSMatchResult Result = Controller.GetMatchResult();
	if (Result == ERTSMatchResult::InProgress)
	{
		return;
	}

	const FLinearColor ResultColor = Result == ERTSMatchResult::Victory ? VictoryColor : DefeatColor;
	const FString Title = GetMatchResultTitle(Result);
	const FString Message = GetMatchResultMessage(Result);
	const bool bCanRestart = Controller.CanRestart();
	const FString Footer = bCanRestart ? TEXT("Restart") : TEXT("Waiting for the host to restart");
	UFont* Font = GEngine->GetLargeFont();
	FVector2f TitleSize;
	FVector2f MessageSize;
	FVector2f FooterSize;
	GetTextSize(Title, TitleSize.X, TitleSize.Y, Font, ResultTextScale);
	GetTextSize(Message, MessageSize.X, MessageSize.Y);
	GetTextSize(Footer, FooterSize.X, FooterSize.Y);
	const FVector2f ButtonSize(FooterSize.X + ButtonPadding * 4.f, FooterSize.Y + ButtonPadding * 2.f);

	// The whole screen dimmed, then a bordered panel in the middle: title, message, and the restart button or wait note.
	DrawRect(ResultDim, 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const float ContentWidth = FMath::Max3(TitleSize.X, MessageSize.X, ButtonSize.X);
	const FVector2D PanelSize(
		FMath::Max(ContentWidth + ResultPanelPadding * 2.f, ResultPanelMinWidth),
		ResultPanelPadding * 2.f + TitleSize.Y + MenuLineHeight + MessageSize.Y + MenuLineHeight * 1.5f + ButtonSize.Y);
	const FVector2D Panel((Canvas->ClipX - PanelSize.X) * 0.5f, (Canvas->ClipY - PanelSize.Y) * 0.5f);
	DrawRect(ResultColor, Panel.X - ResultBorder, Panel.Y - ResultBorder, PanelSize.X + ResultBorder * 2.f, PanelSize.Y + ResultBorder * 2.f);
	DrawRect(ResultBackdrop, Panel.X, Panel.Y, PanelSize.X, PanelSize.Y);

	const float CenterX = Panel.X + PanelSize.X * 0.5f;
	float Y = Panel.Y + ResultPanelPadding;
	DrawText(Title, ResultColor, CenterX - TitleSize.X * 0.5f, Y, Font, ResultTextScale);
	Y += TitleSize.Y + MenuLineHeight;
	DrawText(Message, MenuColor, CenterX - MessageSize.X * 0.5f, Y);
	Y += MessageSize.Y + MenuLineHeight * 1.5f;

	if (!bCanRestart)
	{
		DrawText(Footer, MenuColor, CenterX - FooterSize.X * 0.5f, Y + ButtonPadding);
		return;
	}

	const FVector2D Min(CenterX - ButtonSize.X * 0.5f, Y);
	RestartButton = FBox2D(Min, Min + FVector2D(ButtonSize));

	FVector2D Mouse;
	const bool bHovered = Controller.GetMousePosition(Mouse.X, Mouse.Y) && IsOnRestartButton(Mouse);
	DrawRect(bHovered ? ButtonHoverFill : ButtonFill, Min.X, Min.Y, ButtonSize.X, ButtonSize.Y);
	DrawText(Footer, MenuColor, Min.X + ButtonPadding * 2.f, Min.Y + ButtonPadding);
}

bool ARTSHUD::IsOnRestartButton(const FVector2D& Point) const
{
	return RestartButton.bIsValid && RestartButton.IsInside(Point);
}
