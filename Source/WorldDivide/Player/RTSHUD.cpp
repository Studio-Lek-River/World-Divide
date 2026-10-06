#include "Player/RTSHUD.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSHealthComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Engine/Canvas.h"
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

	constexpr float ConstructionBarWidth = 60.f;
	constexpr float ButtonPadding = 8.f;
	constexpr float BarSpacing = 8.f;
	constexpr float MenuMargin = 16.f;
	constexpr float MenuLineHeight = 18.f;
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

	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == Controller->GetTeamId() && !It->IsConstructed())
		{
			DrawConstructionBar(*Controller, **It);
		}
	}

	CancelButton = FBox2D(ForceInit);
	const ARTSBuilding* Selected = Controller->GetSelectedBuilding();
	if (Selected)
	{
		DrawBuildingMarker(*Controller, *Selected);
		if (Controller->CanCancelConstruction(Selected))
		{
			DrawCancelButton(*Selected);
		}
	}

	const ARTSBuilding* Hovered = Controller->GetBuildingUnderCursor();
	if (Hovered && Hovered != Selected)
	{
		DrawBuildingHealth(*Controller, *Hovered);
	}

	DrawBuildMenu(*Controller);
	DrawCredits(*Controller);
	DrawPower(*Controller);

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

void ARTSHUD::DrawUnitMarker(const ARTSPlayerController& Controller, const ARTSUnit& Unit)
{
	const FVector Center = Unit.GetActorLocation();
	const FVector CameraRight = Controller.PlayerCameraManager
		? Controller.PlayerCameraManager->GetCameraRotation().RotateVector(FVector::RightVector)
		: FVector::RightVector;

	FVector2D ScreenCenter;
	FVector2D ScreenEdge;
	if (!Controller.ProjectWorldLocationToScreen(Center, ScreenCenter)
		|| !Controller.ProjectWorldLocationToScreen(Center + CameraRight * Unit.GetSelectionRadius(), ScreenEdge))
	{
		return;
	}

	const float Radius = FMath::Max(8.f, FVector2D::Distance(ScreenCenter, ScreenEdge));
	const float Left = ScreenCenter.X - Radius;
	const float Top = ScreenCenter.Y - Radius;
	DrawBrackets(Left, Top, ScreenCenter.X + Radius, ScreenCenter.Y + Radius);
	DrawHealthBar(Left, Top - BarSpacing, Radius * 2.f, Unit.GetHealthComponent()->GetHealthFraction());
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
		const FLinearColor Color = bPlacing ? SelectionColor : Controller.CanAfford(*Data) ? MenuColor : UnaffordableColor;
		DrawText(FString::Printf(TEXT("%d  %s  (%d)"), Index + 1, *Data->DisplayName.ToString(), Data->Cost), Color, MenuMargin, Y);
	}
	if (Controller.GetPlacementData())
	{
		Y += MenuLineHeight;
		DrawText(TEXT("Left click: build (Shift: queue)    Right click / Esc: cancel"), MenuColor, MenuMargin, Y);
	}
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
