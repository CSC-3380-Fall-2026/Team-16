#pragma once

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"

namespace PFTViewportTest
{
    // Use the viewport hit path so clipped or covered controls fail the test.
    inline bool ClickVisibleWidget(UWidget* Widget)
    {
        auto Window = GEngine->GameViewport->GetWindow();
        auto SlateWidget = Widget ? Widget->GetCachedWidget() : nullptr;
        if (!Window || !SlateWidget || !Widget->GetIsEnabled()) return false;

        const FGeometry& Geometry = Widget->GetCachedGeometry();
        const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5);
        auto& Slate = FSlateApplication::Get();
        const FWidgetPath Hit = Slate.LocateWindowUnderMouse(Center, { Window.ToSharedRef() });
        if (!Hit.ContainsWidget(SlateWidget.Get())) return false;

        const TSet<FKey> Pressed = { EKeys::LeftMouseButton };
        const TSet<FKey> Released;
        const FPointerEvent Move(0, Center, Slate.GetCursorPos(), Released, EKeys::Invalid, 0, FModifierKeysState());
        const FPointerEvent Down(0, Center, Center, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
        const FPointerEvent Up(0, Center, Center, Released, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Slate.RoutePointerMoveEvent(Hit, Move, false);
        Slate.RoutePointerDownEvent(Hit, Down);
        return Slate.RoutePointerUpEvent(Hit, Up).IsEventHandled();
    }

    inline bool SkipWelcome(UWorld* World)
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UUserWidget::StaticClass(), true);
        for (UUserWidget* Widget : Widgets)
        {
            if (!Widget->WidgetTree) continue;
            if (UWidget* Button = Widget->WidgetTree->FindWidget(TEXT("SkipButton"))) return ClickVisibleWidget(Button);
        }
        return false;
    }
}
