#include "PFTRouteMap.h"
#include "PFTPalette.h"
#include "PFTRoutes.h"
#include "Algo/AllOf.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
    FBox2D FitRouteBounds(const FVector2D& Origin, TArrayView<const FVector> Path)
    {
        if (Path.IsEmpty()) return FBox2D(ForceInit);
        FBox2D Bounds(Origin, Origin);
        for (const FVector& Point : Path) Bounds += FVector2D(Point);
        const FVector2D Extra = (FVector2D(2000.0) - Bounds.GetSize()).ComponentMax(FVector2D::ZeroVector) * 0.5;
        return Bounds.ExpandBy(Extra + FVector2D(400.0));
    }

    struct FMapProjection
    {
        FMapProjection(const FBox2D& Bounds, const FVector2D& Area)
        {
            const FVector2D Extent = Bounds.GetSize();
            Scale = FMath::Min(Area.X / Extent.X, Area.Y / Extent.Y);
            Size = Extent * Scale;
            Offset = (Area - Size) * 0.5;
            Origin = FVector2D(Bounds.Min.X, Bounds.Max.Y);
        }

        FVector2f Point(const FVector2D& Position) const
        {
            const FVector2D Delta = Position - Origin;
            return FVector2f(Offset + FVector2D(Delta.X, -Delta.Y) * Scale);
        }

        FVector2D Size;
        FVector2D Offset;
        FVector2D Origin;
        double Scale;
    };

    void DrawLandmark(const FGeometry& Geometry, FSlateWindowElementList& Draw, int32 Layer,
        const FVector2f& Pin, const FString& Name)
    {
        constexpr float Inset = 4.0f;
        const FVector2f Area(Geometry.GetLocalSize());
        if (Pin.X < Inset || Pin.X > Area.X - Inset || Pin.Y < Inset || Pin.Y > Area.Y - Inset) return;

        const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);
        const auto FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const FVector2f TextSize(FontMeasure->Measure(Name, Font));
        const FVector2f Padding(6, 3);
        const FVector2f LabelSize = TextSize + Padding * 2.0f;
        if (LabelSize.X + Inset * 2.0f > Area.X || LabelSize.Y + Inset * 2.0f > Area.Y) return;

        const float AbovePin = Pin.Y - LabelSize.Y - 7.0f;
        const FVector2f LabelCorner(
            FMath::Clamp(Pin.X - LabelSize.X * 0.5f, Inset, Area.X - LabelSize.X - Inset),
            FMath::Clamp(AbovePin >= Inset ? AbovePin : Pin.Y + 7.0f, Inset, Area.Y - LabelSize.Y - Inset));
        const auto* Brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const auto Place = [&](const FVector2f& Size, const FVector2f& Corner)
        {
            return Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Corner));
        };
        const FLinearColor Ink(PFTPalette::Ink);
        FSlateDrawElement::MakeBox(Draw, Layer, Place(FVector2f(8, 8), Pin - FVector2f(4, 4)), Brush);
        FSlateDrawElement::MakeBox(Draw, Layer + 1, Place(FVector2f(4, 4), Pin - FVector2f(2, 2)), Brush,
            ESlateDrawEffect::None, Ink);
        FSlateDrawElement::MakeBox(Draw, Layer, Place(LabelSize, LabelCorner), Brush);
        FSlateDrawElement::MakeText(Draw, Layer + 1, Place(TextSize, LabelCorner + Padding), Name, Font,
            ESlateDrawEffect::None, Ink);
    }
}

bool UPFTRouteMap::Load(UPFTRoutes& InRoutes, APlayerController& InPlayer)
{
    Routes = &InRoutes;
    Player = &InPlayer;
    bFullFloor = false;
    const APawn* Pawn = InPlayer.GetPawn();
    FocusBounds = Pawn ? FitRouteBounds(FVector2D(Pawn->GetActorLocation()), InRoutes.GetRemainingPath()) : FBox2D(ForceInit);
    if (FloorBrush.GetResourceObject()) return true;

    TArray<FIntPoint> Cells;
    const int32 Step = InRoutes.CopyWalkableCells(Cells);
    if (Cells.IsEmpty() || Step <= 0) return false;

    FIntPoint Min = Cells[0];
    FIntPoint Max = Cells[0];
    for (const FIntPoint& Cell : Cells)
    {
        Min = Min.ComponentMin(Cell);
        Max = Max.ComponentMax(Cell);
    }
    const FIntPoint Span = Max - Min + FIntPoint(1, 1);
    const int32 CellsPerPixel = FMath::DivideAndRoundUp(FMath::Max(Span.X, Span.Y), 512);
    const int32 Width = FMath::DivideAndRoundUp(Span.X, CellsPerPixel);
    const int32 Height = FMath::DivideAndRoundUp(Span.Y, CellsPerPixel);
    TArray<FColor> Pixels;
    Pixels.Init(FColor(245, 243, 249), Width * Height);
    for (const FIntPoint& Cell : Cells)
    {
        const FIntPoint Pixel = (Cell - Min) / CellsPerPixel;
        Pixels[(Height - 1 - Pixel.Y) * Width + Pixel.X] = FColor(206, 198, 222);
    }

    const TConstArrayView64<uint8> Image(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor));
    auto* Floor = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None, Image);
    if (!Floor) return false;
    Floor->Filter = TF_Bilinear;
    Floor->UpdateResource();
    FloorBrush.DrawAs = ESlateBrushDrawType::Image;
    FloorBrush.SetResourceObject(Floor);
    const FVector2D WorldMin = FVector2D(Min) * Step - FVector2D(Step * 0.5);
    FloorBounds = FBox2D(WorldMin, WorldMin + FVector2D(Width, Height) * (Step * CellsPerPixel));
    return true;
}

void UPFTRouteMap::SetPreview(const FVector& Origin, TArray<FVector> Points)
{
    PreviewOrigin = Origin;
    PreviewPoints = MoveTemp(Points);
    FocusBounds = FitRouteBounds(FVector2D(Origin), PreviewPoints);
}

bool UPFTRouteMap::ToggleFullFloor()
{
    bFullFloor = !bFullFloor;
    return bFullFloor;
}

int32 UPFTRouteMap::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
    FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
    if (!FloorBounds.bIsValid || !Routes.IsValid() || !Player.IsValid()) return Layer;
    const APawn* Pawn = Player->GetPawn();
    if (!Pawn) return Layer;
    const bool bPreview = !PreviewPoints.IsEmpty();
    const TArrayView<const FVector> Path = bPreview ? MakeArrayView(PreviewPoints) : Routes->GetRemainingPath();
    const FVector2D Position(bPreview ? PreviewOrigin : Pawn->GetActorLocation());
    // Keep the view steady during walking. The full floor still fits when a reroute leaves this view.
    const bool bFits = !bFullFloor && FocusBounds.bIsValid && FocusBounds.IsInsideOrOn(Position)
        && Algo::AllOf(Path, [this](const FVector& Point) { return FocusBounds.IsInsideOrOn(FVector2D(Point)); });
    const FMapProjection FloorProjection(FloorBounds, Geometry.GetLocalSize());
    const FMapProjection FocusProjection(bFits ? FocusBounds : FloorBounds, Geometry.GetLocalSize());
    const auto& Projection = FocusProjection.Scale > FloorProjection.Scale ? FocusProjection : FloorProjection;
    const FVector2f FloorTopLeft = Projection.Point(FVector2D(FloorBounds.Min.X, FloorBounds.Max.Y));
    const auto Map = Geometry.ToPaintGeometry(FVector2f(FloorBounds.GetSize() * Projection.Scale), FSlateLayoutTransform(FloorTopLeft));
    FSlateDrawElement::MakeBox(Draw, Layer, Map, &FloorBrush);

    const auto Paint = Geometry.ToPaintGeometry();
    const FVector2f Here = Projection.Point(Position);
    const FLinearColor Purple(PFTPalette::Purple);
    if (!Path.IsEmpty())
    {
        TArray<FVector2f> Line;
        Line.Reserve(Path.Num() + 1);
        Line.Add(Here);
        for (const FVector& Point : Path) Line.Add(Projection.Point(FVector2D(Point)));
        const FVector2f Goal = Line.Last();
        FSlateDrawElement::MakeLines(Draw, Layer + 1, Paint, MoveTemp(Line), ESlateDrawEffect::None, Purple, true, 3.0f);
        const FVector2f Top = Goal + FVector2f(0, -5);
        TArray<FVector2f> Diamond = {Top, Goal + FVector2f(5, 0), Goal + FVector2f(0, 5), Goal - FVector2f(5, 0), Top};
        FSlateDrawElement::MakeLines(Draw, Layer + 2, Paint, Diamond, ESlateDrawEffect::None, Purple, true, 4.0f);
        FSlateDrawElement::MakeLines(Draw, Layer + 3, Paint, MoveTemp(Diamond), ESlateDrawEffect::None,
            FLinearColor(PFTPalette::Gold), true, 2.0f);

        if (const auto* Landmark = Routes->GetRouteLandmark(Path))
        {
            DrawLandmark(Geometry, Draw, Layer + 4, Projection.Point(Landmark->Value), Landmark->Key);
        }
    }
    if (FloorBounds.IsInside(Position))
    {
        const FVector Facing = FRotator(0, Player->GetControlRotation().Yaw, 0).Vector();
        const FVector2f Ahead(Facing.X, -Facing.Y);
        const FVector2f Side(-Ahead.Y, Ahead.X);
        const FVector2f Nose = Here + Ahead * 7.0f;
        TArray<FVector2f> Arrow = {Nose, Here - Ahead * 4.0f + Side * 5.0f, Here - Ahead * 4.0f - Side * 5.0f, Nose};
        FSlateDrawElement::MakeLines(Draw, Layer + 6, Paint, Arrow, ESlateDrawEffect::None, FLinearColor::White, true, 5.0f);
        FSlateDrawElement::MakeLines(Draw, Layer + 7, Paint, MoveTemp(Arrow), ESlateDrawEffect::None,
            FLinearColor(FColor(30, 77, 142)), true, 2.8f);
    }
    return Layer + 7;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTRouteMapProjectionTest, "PFTNavigator.Routes.OverviewProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPFTRouteMapProjectionTest::RunTest(const FString& Parameters)
{
    const FMapProjection Projection(FBox2D(FVector2D(-100, -100), FVector2D(100, 0)), FVector2D(240, 200));
    TestEqual(TEXT("Floor map preserves aspect ratio"), Projection.Size, FVector2D(240, 120));
    TestEqual(TEXT("The higher Y edge is at the top"), Projection.Point(FVector2D(-100, 0)), FVector2f(0, 40));
    TestEqual(TEXT("The lower Y edge is at the bottom"), Projection.Point(FVector2D(100, -100)), FVector2f(240, 160));
    TestEqual(TEXT("A world point stays aligned with the floor"), Projection.Point(FVector2D(0, -50)), FVector2f(120, 100));
    const FVector2D Start(0, 0);
    const TArray<FVector> Path = {FVector(3000, 0, 0), FVector(3000, -1000, 0)};
    const FBox2D Bounds = FitRouteBounds(Start, Path);
    TestTrue(TEXT("The route frame includes the start and destination with padding"),
        Bounds.IsInside(Start) && Bounds.IsInside(FVector2D(Path.Last())));
    TestEqual(TEXT("A narrow route leaves room for the surrounding halls"), Bounds.GetSize(), FVector2D(3800, 2800));
    TestFalse(TEXT("An empty route uses the full floor"), FitRouteBounds(Start, {}).bIsValid);
    return true;
}
#endif
