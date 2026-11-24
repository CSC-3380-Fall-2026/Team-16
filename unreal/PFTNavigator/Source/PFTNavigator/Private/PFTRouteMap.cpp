#include "PFTRouteMap.h"
#include "PFTPalette.h"
#include "PFTRoutes.h"
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

    bool PlaceLandmarkLabel(const FVector2f& Pin, const FVector2f& Size, const FVector2f& Area,
        TArrayView<const FVector2f> Route, FVector2f& Corner)
    {
        constexpr float Inset = 4.0f;
        if (Size.X + Inset * 2.0f > Area.X || Size.Y + Inset * 2.0f > Area.Y) return false;
        for (const float Gap : {12.0f, 24.0f})
        {
            const FVector2f Offsets[] = {
                {-Size.X * 0.5f, -Size.Y - Gap}, {-Size.X * 0.5f, Gap},
                {Gap, -Size.Y * 0.5f}, {-Size.X - Gap, -Size.Y * 0.5f}};
            for (const FVector2f& Offset : Offsets)
            {
                const FVector2f Candidate = Pin + Offset;
                const FVector2f Start(FMath::Clamp(Candidate.X, Inset, Area.X - Size.X - Inset),
                    FMath::Clamp(Candidate.Y, Inset, Area.Y - Size.Y - Inset));
                const FBox2D Margin(FVector2D(Start - FVector2f(8, 8)), FVector2D(Start + Size + FVector2f(8, 8)));
                if (Margin.IsInsideOrOn(FVector2D(Pin))) continue;
                bool bOverlapsRoute = false;
                for (int32 Index = 1; Index < Route.Num() && !bOverlapsRoute; ++Index)
                {
                    bOverlapsRoute = FMath::LineBoxIntersection2D(Margin, FVector2D(Route[Index - 1]), FVector2D(Route[Index]));
                }
                if (bOverlapsRoute) continue;
                Corner = Start;
                return true;
            }
        }
        return false;
    }

    void DrawLandmark(const FGeometry& Geometry, FSlateWindowElementList& Draw, int32 Layer,
        const FVector2f& Pin, const FString& Name, TArrayView<const FVector2f> Route)
    {
        constexpr float Inset = 4.0f;
        const FVector2f Area(Geometry.GetLocalSize());
        if (Pin.X < Inset || Pin.X > Area.X - Inset || Pin.Y < Inset || Pin.Y > Area.Y - Inset) return;
        const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);
        const auto FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const FVector2f TextSize(FontMeasure->Measure(Name, Font));
        const FVector2f Padding(6, 3);
        const FVector2f LabelSize = TextSize + Padding * 2.0f;

        const auto* Brush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const auto Place = [&](const FVector2f& Size, const FVector2f& Corner)
        {
            return Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Corner));
        };
        const FLinearColor Ink(PFTPalette::Ink);
        FSlateDrawElement::MakeBox(Draw, Layer, Place(FVector2f(8, 8), Pin - FVector2f(4, 4)), Brush);
        FSlateDrawElement::MakeBox(Draw, Layer + 1, Place(FVector2f(4, 4), Pin - FVector2f(2, 2)), Brush,
            ESlateDrawEffect::None, Ink);
        FVector2f LabelCorner;
        if (!PlaceLandmarkLabel(Pin, LabelSize, Area, Route, LabelCorner)) return;

        const FVector2f LabelEdge(FMath::Clamp(Pin.X, LabelCorner.X, LabelCorner.X + LabelSize.X),
            FMath::Clamp(Pin.Y, LabelCorner.Y, LabelCorner.Y + LabelSize.Y));
        FSlateDrawElement::MakeLines(Draw, Layer - 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{Pin, LabelEdge},
            ESlateDrawEffect::None, Ink, true, 1.0f);
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
    FocusBounds = FBox2D(ForceInit);
    LandmarkPathCount = INDEX_NONE;
    FloorBrush.DrawAs = ESlateBrushDrawType::Image;
    auto* Floor = InRoutes.GetFloorMap(FloorBounds);
    FloorBrush.SetResourceObject(Floor);
    return Floor != nullptr;
}

void UPFTRouteMap::SetPreview(const FVector& Origin, TArray<FVector> Points)
{
    PreviewOrigin = Origin;
    PreviewPoints = MoveTemp(Points);
    FocusBounds = FitRouteBounds(FVector2D(Origin), PreviewPoints);
    LandmarkPathCount = INDEX_NONE;
}

bool UPFTRouteMap::ToggleFullFloor()
{
    bFullFloor = !bFullFloor;
    if (!bFullFloor && PreviewPoints.IsEmpty()) FocusBounds = FBox2D(ForceInit);
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
    // Keep the view steady between plans, then frame a reroute from its new starting point.
    const uint32 PlanRevision = Routes->GetPlanRevision();
    if (!bFullFloor && !bPreview && !Path.IsEmpty() && (!FocusBounds.bIsValid || FocusRevision != PlanRevision))
    {
        FocusBounds = FitRouteBounds(Position, Path);
        FocusRevision = PlanRevision;
    }
    const bool bFits = !bFullFloor && FocusBounds.bIsValid && FocusBounds.IsInsideOrOn(Position);
    const FMapProjection FloorProjection(FloorBounds, Geometry.GetLocalSize());
    const FMapProjection FocusProjection(bFits ? FocusBounds : FloorBounds, Geometry.GetLocalSize());
    const auto& Projection = FocusProjection.Scale > FloorProjection.Scale ? FocusProjection : FloorProjection;
    const auto Paint = Geometry.ToPaintGeometry();
    // Fill and outline the whole frame so the map reads as one box even where the floor ends
    const FVector2f Frame(Geometry.GetLocalSize());
    FSlateDrawElement::MakeBox(Draw, Layer, Paint, FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
        ESlateDrawEffect::None, FLinearColor(FColor(245, 243, 249)));
    ++Layer;
    const FVector2f FloorTopLeft = Projection.Point(FVector2D(FloorBounds.Min.X, FloorBounds.Max.Y));
    const auto Map = Geometry.ToPaintGeometry(FVector2f(FloorBounds.GetSize() * Projection.Scale), FSlateLayoutTransform(FloorTopLeft));
    FSlateDrawElement::MakeBox(Draw, Layer, Map, &FloorBrush);
    FSlateDrawElement::MakeLines(Draw, Layer + 8, Paint, TArray<FVector2f>{{1, 1}, {Frame.X - 1, 1}, Frame - FVector2f(1, 1),
        {1, Frame.Y - 1}, {1, 1}}, ESlateDrawEffect::None, FLinearColor(FColor(150, 135, 180)), true, 1.5f);

    const FVector2f Here = Projection.Point(Position);
    const FLinearColor Purple(PFTPalette::Purple);
    if (!Path.IsEmpty())
    {
        TArray<FVector2f> Line;
        Line.Reserve(Path.Num() + 1);
        Line.Add(Here);
        for (const FVector& Point : Path) Line.Add(Projection.Point(FVector2D(Point)));
        const FVector2f Goal = Line.Last();
        // The remaining landmarks change when the player advances a waypoint or gets a new route.
        if (LandmarkPlanRevision != PlanRevision || LandmarkPathCount != Path.Num())
        {
            LandmarkPlanRevision = PlanRevision;
            LandmarkPathCount = Path.Num();
            const auto* Landmark = Routes->GetRouteLandmark(Path);
            CachedLandmark = Landmark ? TOptional<TPair<FString, FVector2D>>(*Landmark) : NullOpt;
        }
        if (CachedLandmark)
        {
            DrawLandmark(Geometry, Draw, Layer + 4, Projection.Point(CachedLandmark->Value), CachedLandmark->Key, Line);
        }
        FSlateDrawElement::MakeLines(Draw, Layer + 1, Paint, MoveTemp(Line), ESlateDrawEffect::None, Purple, true, 5.0f);
        // A gold diamond with a purple edge, large enough to find without hunting for it
        const auto* Solid = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        const auto Diamond = [&](int32 DiamondLayer, float Width, const FLinearColor& Color)
        {
            const auto Square = Geometry.ToPaintGeometry(FVector2f(Width, Width), FSlateLayoutTransform(Goal - FVector2f(Width, Width) * 0.5f));
            FSlateDrawElement::MakeRotatedBox(Draw, DiamondLayer, Square, Solid, ESlateDrawEffect::None, UE_PI / 4.0f,
                TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Color);
        };
        Diamond(Layer + 2, 20.0f, Purple);
        Diamond(Layer + 3, 12.0f, FLinearColor(PFTPalette::Gold));
    }
    if (FloorBounds.IsInside(Position))
    {
        const FVector Facing = FRotator(0, Player->GetControlRotation().Yaw, 0).Vector();
        const FVector2f Ahead(Facing.X, -Facing.Y);
        const FVector2f Side(-Ahead.Y, Ahead.X);
        // A solid arrowhead with a notched tail, so the way you're facing is clear at a glance
        const FVector2f Nose = Here + Ahead * 17.0f;
        const FVector2f Tail = Here - Ahead * 4.0f;
        TArray<FVector2f> Arrow = {Nose, Here - Ahead * 11.0f + Side * 11.0f, Tail, Here - Ahead * 11.0f - Side * 11.0f, Nose};
        const FLinearColor Blue(PFTPalette::You);
        FSlateDrawElement::MakeLines(Draw, Layer + 6, Paint, Arrow, ESlateDrawEffect::None, FLinearColor::White, true, 9.0f);
        FSlateDrawElement::MakeLines(Draw, Layer + 7, Paint, MoveTemp(Arrow), ESlateDrawEffect::None, Blue, true, 5.0f);
        FSlateDrawElement::MakeLines(Draw, Layer + 7, Paint, TArray<FVector2f>{Nose - Ahead * 4.0f, Tail}, ESlateDrawEffect::None, Blue, true, 9.0f);
    }
    return Layer + 8;
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
    const TArray<FVector2f> ScreenRoute = {{25, 88}, {70, 98}, {195, 98}};
    const FVector2f LabelSize(96, 21);
    FVector2f LabelCorner;
    if (TestTrue(TEXT("A landmark label can fit around the route"),
        PlaceLandmarkLabel({70, 98}, LabelSize, {300, 150}, ScreenRoute, LabelCorner)))
    {
        const FBox2f Label(LabelCorner, LabelCorner + LabelSize);
        TestFalse(TEXT("The label leaves the player marker visible"), Label.IsInsideOrOn(ScreenRoute[0]));
        TestFalse(TEXT("The label leaves the destination marker visible"), Label.IsInsideOrOn(ScreenRoute.Last()));
    }
    const TArray<FVector2f> LongLeg = {{12, 77}, {286, 77}};
    if (TestTrue(TEXT("A landmark label can fit beside a long leg"),
        PlaceLandmarkLabel({150, 108}, LabelSize, {300, 150}, LongLeg, LabelCorner)))
    {
        TestTrue(TEXT("The label clears the full route line between waypoints"), LabelCorner.Y > 77);
    }
    return true;
}
#endif
