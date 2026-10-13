#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Engine/UserInterfaceSettings.h"
#include "Input/HittestGrid.h"
#include "Misc/App.h"
#include "Rendering/DrawElements.h"

namespace RTSTests
{
	/**
	 * A headless run never paints, so nothing has a layout: lays Widget out and paints it once into a throwaway element list
	 * over a ViewportSize viewport at the DPI scale for it, as a frame would, so its panels have their geometry.
	 */
	inline void LayOutHeadless(UUserWidget& Widget, const FVector2D& ViewportSize)
	{
		const float Scale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(ViewportSize.X, ViewportSize.Y));
		const TSharedRef<SWidget> Root = Widget.TakeWidget();
		Root->SlatePrepass(Scale);
		const FGeometry Geometry = FGeometry::MakeRoot(ViewportSize / Scale, FSlateLayoutTransform(Scale));
		FHittestGrid Grid;
		const FPaintArgs Args(nullptr, Grid, FVector2D::ZeroVector, FApp::GetCurrentTime(), FApp::GetDeltaTime());
		FSlateWindowElementList Elements(nullptr);
		Root->Paint(Args, Geometry, FSlateRect(FVector2D::ZeroVector, ViewportSize), Elements, 0, FWidgetStyle(), true);
	}
}

#endif
