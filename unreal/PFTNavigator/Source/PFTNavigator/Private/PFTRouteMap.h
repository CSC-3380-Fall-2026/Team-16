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
    void SetPreview(const FVector& Origin, TArray<FVector> Points);
    bool ToggleFullFloor();
#if WITH_DEV_AUTOMATION_TESTS
    const FBox2D& GetFocusBoundsForTests() const { return FocusBounds; }
#endif

protected:
    int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
        FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;

private:
    UPROPERTY() FSlateBrush FloorBrush;
    FBox2D FloorBounds;
    mutable FBox2D FocusBounds;
    mutable uint32 FocusRevision = 0;
    FVector PreviewOrigin = FVector::ZeroVector;
    TArray<FVector> PreviewPoints;
    TWeakObjectPtr<UPFTRoutes> Routes;
    TWeakObjectPtr<APlayerController> Player;
    bool bFullFloor = false;
};
