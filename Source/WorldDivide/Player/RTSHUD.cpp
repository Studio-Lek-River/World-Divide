#include "Player/RTSHUD.h"

#include "Buildings/RTSBuilding.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
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

	constexpr float ConstructionBarWidth = 60.f;
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

	DrawBuildMenu(*Controller);

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
	const float Corner = Radius * 0.4f;
	const float Left = ScreenCenter.X - Radius;
	const float Right = ScreenCenter.X + Radius;
	const float Top = ScreenCenter.Y - Radius;
	const float Bottom = ScreenCenter.Y + Radius;

	// Corner brackets.
	DrawLine(Left, Top, Left + Corner, Top, SelectionColor, 2.f);
	DrawLine(Left, Top, Left, Top + Corner, SelectionColor, 2.f);
	DrawLine(Right, Top, Right - Corner, Top, SelectionColor, 2.f);
	DrawLine(Right, Top, Right, Top + Corner, SelectionColor, 2.f);
	DrawLine(Left, Bottom, Left + Corner, Bottom, SelectionColor, 2.f);
	DrawLine(Left, Bottom, Left, Bottom - Corner, SelectionColor, 2.f);
	DrawLine(Right, Bottom, Right - Corner, Bottom, SelectionColor, 2.f);
	DrawLine(Right, Bottom, Right, Bottom - Corner, SelectionColor, 2.f);

	// Health bar above the brackets, green to red.
	const float HealthFraction = Unit.GetHealthComponent()->GetHealthFraction();
	const float BarWidth = Radius * 2.f;
	const float BarY = Top - 8.f;
	DrawRect(HealthBackground, Left - 1.f, BarY - 1.f, BarWidth + 2.f, 6.f);
	DrawRect(FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::Green, HealthFraction), Left, BarY, BarWidth * HealthFraction, 4.f);
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

void ARTSHUD::DrawBuildMenu(const ARTSPlayerController& Controller)
{
	const URTSFactionData* Faction = Controller.GetFaction();
	if (!Faction)
	{
		return;
	}

	float Y = MenuMargin;
	DrawText(TEXT("Build (debug)"), MenuColor, MenuMargin, Y);
	const int32 Count = FMath::Min(Faction->Buildings.Num(), ARTSPlayerController::MaxBuildHotkeys);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const URTSBuildingData* Data = Faction->Buildings[Index];
		if (!Data)
		{
			continue;
		}
		Y += MenuLineHeight;
		const bool bPlacing = Controller.GetPlacementData() == Data;
		DrawText(FString::Printf(TEXT("%d  %s"), Index + 1, *Data->DisplayName.ToString()),
			bPlacing ? SelectionColor : MenuColor, MenuMargin, Y);
	}
	if (Controller.GetPlacementData())
	{
		Y += MenuLineHeight;
		DrawText(TEXT("Left click: build    Right click / Esc: cancel"), MenuColor, MenuMargin, Y);
	}
}
