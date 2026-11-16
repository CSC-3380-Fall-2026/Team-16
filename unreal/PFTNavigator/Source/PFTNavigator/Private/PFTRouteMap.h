#pragma once

#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "PFTRouteMap.generated.h"

class APlayerController;
class UPFTRoutes;

UCLASS()
class UPFTRouteMap : public UUserWidget
{
    GENERATED_BODY()

public:
    bool Load(UPFTRoutes& Routes, APlayerController& Player);

protected:
    int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
        FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;

private:
    UPROPERTY() FSlateBrush FloorBrush;
    FBox2D FloorBounds;
    TWeakObjectPtr<UPFTRoutes> Routes;
    TWeakObjectPtr<APlayerController> Player;
};
