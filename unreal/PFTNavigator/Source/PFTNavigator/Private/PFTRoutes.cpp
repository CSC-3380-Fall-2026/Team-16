#include "PFTRoutes.h"
#include "PFTPalette.h"
#include "Algo/BinarySearch.h"
#include "Components/CapsuleComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    const FIntPoint Directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    constexpr double WaypointRadius = 27.0;
    constexpr double CornerRadius = 16.0;
    constexpr double PreferredClearance = 150.0;
    constexpr double TurnCostInGridSteps = 2.0;
    constexpr double ArrivalRadius = 65.0;
    constexpr double FloorTolerance = 45.0;
    constexpr int32 StartSearchCells = 3;
    constexpr int32 DoorApproachCells = 4;
    constexpr double ArrowSpacing = 125.0;
    constexpr double ArrowSpeed = 85.0;
    constexpr double TrailLength = 1600.0;
    constexpr double SecondsPerQuarterTurn = 0.25;

    double SegmentProgress(const FVector& Position, const FVector& Start, const FVector& End)
    {
        const FVector2D Edge(End - Start);
        const double SquaredLength = Edge.SizeSquared();
        return SquaredLength > UE_SMALL_NUMBER ? FVector2D::DotProduct(FVector2D(Position - Start), Edge) / SquaredLength : 0.0;
    }

    TArray<FVector> StraightenPath(TArrayView<const FVector> Points,
        TFunctionRef<bool(const FVector&, const FVector&)> CanWalk)
    {
        if (Points.IsEmpty()) return {};
        TArray<FVector> Result = {Points[0]};
        int32 Anchor = 0;
        while (Anchor + 1 < Points.Num())
        {
            // Use the longest clear leg ahead. A grid path may move sideways even in an open hallway.
            int32 Next = Points.Num() - 1;
            while (Next > Anchor + 1 && !CanWalk(Points[Anchor], Points[Next]))
            {
                --Next;
            }
            Result.Add(Points[Next]);
            Anchor = Next;
        }
        return Result;
    }

    TArray<double> MeasurePath(TArrayView<const FVector> Points)
    {
        TArray<double> Distances;
        Distances.Reserve(Points.Num());
        double Distance = 0.0;
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            if (Index > 0) Distance += FVector::Dist2D(Points[Index - 1], Points[Index]);
            Distances.Add(Distance);
        }
        return Distances;
    }

    const TPair<FString, FVector2D>* FindNearbyLandmark(const FVector2D& Point, const TMap<FString, FVector2D>& Landmarks)
    {
        const TPair<FString, FVector2D>* Nearest = nullptr;
        double DistanceSquared = FMath::Square(220.0);
        for (const auto& Landmark : Landmarks)
        {
            const double Candidate = FVector2D::DistSquared(Point, Landmark.Value);
            if (Candidate >= DistanceSquared) continue;
            Nearest = &Landmark;
            DistanceSquared = Candidate;
        }
        return Nearest;
    }

    const TPair<FString, FVector2D>* FindRouteLandmark(TArrayView<const FVector> Points, const TMap<FString, FVector2D>& Landmarks)
    {
        if (Landmarks.IsEmpty()) return nullptr;
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            if (const auto* Landmark = FindNearbyLandmark(FVector2D(Points[Index]), Landmarks)) return Landmark;
            if (Index + 1 == Points.Num()) break;
            const int32 Samples = FMath::CeilToInt(FVector::Dist2D(Points[Index], Points[Index + 1]) / 60.0);
            for (int32 Sample = 1; Sample < Samples; ++Sample)
            {
                const FVector Point = FMath::Lerp(Points[Index], Points[Index + 1], double(Sample) / Samples);
                if (const auto* Landmark = FindNearbyLandmark(FVector2D(Point), Landmarks)) return Landmark;
            }
        }
        return nullptr;
    }

    FString DescribeTurn(double Turn, int32 Metres, const FVector2D& Corner,
        const TMap<FString, FVector2D>& Landmarks)
    {
        const TCHAR* Side = Turn > 0 ? TEXT("right") : TEXT("left");
        if (Metres <= 8)
        {
            if (const auto* Landmark = FindNearbyLandmark(Corner, Landmarks))
            {
                return FString::Printf(TEXT("Turn %s near %s"), Side, *Landmark->Key);
            }
        }
        return Metres <= 1 ? FString::Printf(TEXT("Turn %s ahead"), Side)
            : FString::Printf(TEXT("Turn %s in %d m"), Side, Metres);
    }

    double RouteBend(TArrayView<const FVector> Points, int32 Index)
    {
        const FVector Before = (Points[Index] - Points[Index - 1]).GetSafeNormal2D();
        const FVector After = (Points[Index + 1] - Points[Index]).GetSafeNormal2D();
        return FVector::CrossProduct(Before, After).Z;
    }

    TArray<int32> FindRouteTurns(TArrayView<const FVector> Points)
    {
        TArray<int32> Turns;
        for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
        {
            if (FMath::Abs(RouteBend(Points, Index)) >= 0.5) Turns.Add(Index);
        }
        return Turns;
    }

    FText FormatRouteSteps(const FVector& Position, TArrayView<const FVector> Points, TArrayView<const double> Distances,
        TArrayView<const int32> Turns, int32 Start, const TMap<FString, FVector2D>& Landmarks)
    {
        if (!Points.IsValidIndex(Start)) return FText::GetEmpty();
        TArray<FString> Steps;
        double Approach = FVector::Dist2D(Position, Points[Start]);
        for (int32 Turn : Turns.RightChop(Algo::LowerBound(Turns, Start)))
        {
            const int32 Metres = FMath::RoundToInt((Approach + Distances[Turn] - Distances[Start]) / 100.0);
            const TCHAR* Side = RouteBend(Points, Turn) > 0 ? TEXT("right") : TEXT("left");
            const auto* Landmark = FindNearbyLandmark(FVector2D(Points[Turn]), Landmarks);
            FString Line = Metres > 0 ? FString::Printf(TEXT("Walk %d m, then turn %s"), Metres, Side)
                : FString::Printf(TEXT("Turn %s"), Side);
            if (Landmark) Line += TEXT(" near ") + Landmark->Key;
            Steps.Add(MoveTemp(Line));
            Start = Turn;
            Approach = 0.0;
        }
        const int32 Metres = FMath::RoundToInt((Approach + Distances.Last() - Distances[Start]) / 100.0);
        Steps.Add(Metres > 0 ? FString::Printf(TEXT("Walk %d m to the destination"), Metres) : TEXT("Destination ahead"));
        return FText::FromString(FString::Join(Steps, TEXT("\n\n")));
    }

    FString WalkingTimeLabel(double Seconds)
    {
        if (Seconds < 5.0) return TEXT("Under 5 sec");
        const int32 Rounded = FMath::RoundToInt(Seconds / 5.0) * 5;
        if (Rounded < 60) return FString::Printf(TEXT("About %d sec"), Rounded);
        if (Rounded % 60 == 0) return FString::Printf(TEXT("About %d min"), Rounded / 60);
        return FString::Printf(TEXT("About %d min %d sec"), Rounded / 60, Rounded % 60);
    }

    struct FRouteVisit
    {
        double Seconds;
        FIntVector Previous;
    };

    struct FRouteCandidate
    {
        FIntVector State;
        double Seconds;
        double Estimate;

        bool operator<(const FRouteCandidate& Other) const
        {
            return Estimate < Other.Estimate;
        }
    };

    struct FRouteEntry
    {
        FIntPoint Cell;
        double Seconds;
        int32 Heading = INDEX_NONE;
    };

    double ClearancePenalty(int32 StepsFromBoundary, int32 GridStep)
    {
        const double Clearance = (StepsFromBoundary + 0.5) * GridStep;
        return 1.6 * FMath::Square(FMath::Max(0.0, 1.0 - Clearance / PreferredClearance));
    }

    double WaypointReach(const FVector& Previous, const FVector& Point, const FVector& Next)
    {
        const FVector Incoming = (Point - Previous).GetSafeNormal2D();
        const FVector Outgoing = (Next - Point).GetSafeNormal2D();
        if (Incoming.IsNearlyZero() || Outgoing.IsNearlyZero()) return WaypointRadius;
        return FVector::DotProduct(Incoming, Outgoing) < 0.9 ? CornerRadius : WaypointRadius;
    }

    // A cell reached from different headings can have a different cost for the next turn.
    TArray<FIntPoint> FindFastestPath(const TArray<FRouteEntry>& Entries, const FIntPoint& Target, double SecondsPerStep,
        TFunctionRef<uint8(const FIntPoint&)> GetLinks, TFunctionRef<double(const FIntPoint&)> GetPenalty = [](const FIntPoint&) { return 0.0; },
        double TurnCost = SecondsPerQuarterTurn)
    {
        const auto RemainingTime = [&](const FIntPoint& Cell)
        {
            return (FMath::Abs(Cell.X - Target.X) + FMath::Abs(Cell.Y - Target.Y)) * SecondsPerStep;
        };
        TArray<FRouteCandidate> Open;
        TMap<FIntVector, FRouteVisit> Visits;
        for (const FRouteEntry& Entry : Entries)
        {
            const FIntVector State(Entry.Cell.X, Entry.Cell.Y, Entry.Heading);
            Visits.Add(State, {Entry.Seconds, State});
            Open.HeapPush({State, Entry.Seconds, Entry.Seconds + RemainingTime(Entry.Cell)});
        }
        while (!Open.IsEmpty())
        {
            FRouteCandidate Current;
            Open.HeapPop(Current, EAllowShrinking::No);
            if (Current.Seconds > Visits.FindChecked(Current.State).Seconds) continue;
            const FIntPoint Cell(Current.State.X, Current.State.Y);
            if (Cell == Target)
            {
                TArray<FIntPoint> Reversed = {Cell};
                FIntVector State = Current.State;
                while (Visits.FindChecked(State).Previous != State)
                {
                    State = Visits.FindChecked(State).Previous;
                    Reversed.Emplace(State.X, State.Y);
                }
                return Reversed;
            }
            const uint8 Links = GetLinks(Cell);
            for (int32 Direction = 0; Direction < UE_ARRAY_COUNT(Directions); ++Direction)
            {
                if (!(Links & (1 << Direction))) continue;
                const FIntPoint Next = Cell + Directions[Direction];
                const FIntVector State(Next.X, Next.Y, Direction);
                const int32 Difference = FMath::Abs(Current.State.Z - Direction);
                const int32 Turns = Current.State.Z == INDEX_NONE ? 0 : FMath::Min(Difference, 4 - Difference);
                const double Seconds = Current.Seconds + SecondsPerStep * (1.0 + GetPenalty(Next)) + Turns * TurnCost;
                const auto* Previous = Visits.Find(State);
                if (Previous && Previous->Seconds <= Seconds) continue;
                Visits.Add(State, {Seconds, Current.State});
                Open.HeapPush({State, Seconds, Seconds + RemainingTime(Next)});
            }
        }
        return {};
    }

    void AddArrow(TArray<FBatchedLine>& Lines, const FVector& Center, const FVector& Forward, float Opacity)
    {
        const FVector Side(-Forward.Y, Forward.X, 0);
        const FVector Tip = Center + Forward * 17;
        const FVector Back = Center - Forward * 15;
        const FVector Left = Back - Side * 17;
        const FVector Right = Back + Side * 17;
        const FLinearColor Outline(0.045f, 0.02f, 0.055f, Opacity);
        FLinearColor Gold(PFTPalette::Gold);
        Gold.A = Opacity;
        const FVector Lift(0, 0, 0.5);
        Lines.Emplace(Left, Tip, Outline, -1, 10, SDPG_World);
        Lines.Emplace(Tip, Right, Outline, -1, 10, SDPG_World);
        Lines.Emplace(Left + Lift, Tip + Lift, Gold, -1, 6, SDPG_World);
        Lines.Emplace(Tip + Lift, Right + Lift, Gold, -1, 6, SDPG_World);
    }

    FVector CapsuleCenter(const ACharacter& Character, const FVector& Floor)
    {
        return Floor + FVector(0, 0, Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0);
    }
}

bool UPFTRoutes::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPFTRoutes::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    if (!IsRunningDedicatedServer()) LoadGraph(UWorld::RemovePIEPrefix(World.GetPackage()->GetName()));
}

TStatId UPFTRoutes::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPFTRoutes, STATGROUP_Tickables);
}

void UPFTRoutes::LoadGraph(const FString& Map)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("PFTNavigator"));
    if (!Plugin) return;
    FString Text;
    TSharedPtr<FJsonObject> Graph;
    if (!FFileHelper::LoadFileToString(Text, *(Plugin->GetContentDir() / TEXT("Data/first_floor_routes.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Graph) || !Graph)
    {
        return;
    }

    FString GraphMap;
    int32 GridStep;
    const TArray<TSharedPtr<FJsonValue>>* Rows;
    const TSharedPtr<FJsonObject>* Rooms;
    if (!Graph->TryGetStringField(TEXT("Map"), GraphMap) || GraphMap != Map
        || !Graph->TryGetNumberField(TEXT("Step"), GridStep) || GridStep < 20 || GridStep > 100
        || !Graph->TryGetArrayField(TEXT("Nodes"), Rows) || !Graph->TryGetObjectField(TEXT("Destinations"), Rooms))
    {
        return;
    }

    TMap<FIntPoint, FNode> GraphNodes;
    GraphNodes.Reserve(Rows->Num());
    for (const auto& Row : *Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values;
        double X, Y, Z, Links;
        if (!Row->TryGetArray(Values) || Values->Num() != 4
            || !(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y)
            || !(*Values)[2]->TryGetNumber(Z) || !(*Values)[3]->TryGetNumber(Links)
            || FMath::Abs(X) > 10000 || FMath::Abs(Y) > 10000 || !FMath::IsFinite(Z) || Links < 0 || Links > 15)
        {
            return;
        }
        GraphNodes.Add(FIntPoint(int32(X), int32(Y)), {FVector(X * GridStep, Y * GridStep, Z), uint8(Links)});
    }
    const TSharedPtr<FJsonObject>* RoomStops = nullptr;
    Graph->TryGetObjectField(TEXT("RoomStops"), RoomStops);
    for (const auto& Room : (*Rooms)->Values)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values;
        double X, Y, Z;
        if (Room.Value->TryGetArray(Values) && Values->Num() == 2
            && (*Values)[0]->TryGetNumber(X) && (*Values)[1]->TryGetNumber(Y)
            && FMath::Abs(X) <= 10000 && FMath::Abs(Y) <= 10000)
        {
            const FIntPoint Cell{int32(X), int32(Y)};
            const FNode* Node = GraphNodes.Find(Cell);
            if (!Node) continue;
            FRouteTarget Target{Cell, {}};
            if (RoomStops && (*RoomStops)->HasField(Room.Key))
            {
                if (!(*RoomStops)->TryGetArrayField(Room.Key, Values) || Values->Num() != 3
                    || !(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y) || !(*Values)[2]->TryGetNumber(Z)
                    || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
                    || FVector::Dist2D(Node->Floor, FVector(X, Y, Z)) > GridStep * DoorApproachCells + 0.02
                    || FMath::Abs(Z - Node->Floor.Z) > FloorTolerance) continue;
                Target.RoomStop = FVector(X, Y, Z);
            }
            Destinations.Add(FString(Room.Key), Target);
        }
    }
    Step = GridStep;
    Nodes = MoveTemp(GraphNodes);
    MeasureClearance();
    const TSharedPtr<FJsonObject>* GraphLandmarks;
    if (Graph->TryGetObjectField(TEXT("Landmarks"), GraphLandmarks))
    {
        for (const auto& Landmark : (*GraphLandmarks)->Values)
        {
            const TArray<TSharedPtr<FJsonValue>>* Point;
            double X, Y;
            if (!Landmark.Key.IsEmpty() && Landmark.Value->TryGetArray(Point) && Point->Num() == 2
                && (*Point)[0]->TryGetNumber(X) && (*Point)[1]->TryGetNumber(Y)
                && FMath::Abs(X) <= 1000000 && FMath::Abs(Y) <= 1000000)
            {
                Landmarks.Add(FString(Landmark.Key), FVector2D(X, Y));
            }
        }
    }
}

void UPFTRoutes::MeasureClearance()
{
    const int32 MaxDistance = FMath::CeilToInt(PreferredClearance / Step);
    TArray<FIntPoint> Boundary;
    for (auto& Pair : Nodes)
    {
        FNode& Node = Pair.Value;
        for (int32 Direction = 0; Direction < UE_ARRAY_COUNT(Directions); ++Direction)
        {
            if (!Nodes.Contains(Pair.Key + Directions[Direction])) Node.Links &= ~(1 << Direction);
        }
        Node.StepsFromBoundary = MaxDistance;
        if (Node.Links == 15) continue;
        Node.StepsFromBoundary = 0;
        Boundary.Add(Pair.Key);
    }

    // Count grid steps from blocked edges using walkable links, including at doorways.
    for (int32 Index = 0; Index < Boundary.Num(); ++Index)
    {
        const FIntPoint Cell = Boundary[Index];
        const FNode& Node = Nodes.FindChecked(Cell);
        const int32 Distance = Node.StepsFromBoundary + 1;
        if (Distance >= MaxDistance) continue;
        for (int32 Direction = 0; Direction < UE_ARRAY_COUNT(Directions); ++Direction)
        {
            if (!(Node.Links & (1 << Direction))) continue;
            const FIntPoint Neighbor = Cell + Directions[Direction];
            FNode& Next = Nodes.FindChecked(Neighbor);
            if (Next.StepsFromBoundary <= Distance) continue;
            Next.StepsFromBoundary = Distance;
            Boundary.Add(Neighbor);
        }
    }
}

bool UPFTRoutes::CanCross(const ACharacter& Character, const FVector& Start, const FVector& End) const
{
    const auto* Capsule = Character.GetCapsuleComponent();
    const auto Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PFTWalkingRoute), false, &Character);
    return !GetWorld()->SweepTestByProfile(Start, End, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Query);
}

bool UPFTRoutes::BuildPath(const ACharacter& Character, const FRouteTarget& Target, TArray<FVector>& Points) const
{
    const FVector Position = Character.GetActorLocation();
    const FVector RaisedPosition = Position + FVector(0, 0, 4);
    const FIntPoint Near(FMath::RoundToInt(Position.X / Step), FMath::RoundToInt(Position.Y / Step));
    const double Speed = FMath::Max(1.0, static_cast<double>(Character.GetCharacterMovement()->MaxWalkSpeed));
    const double EntryRange = Step * StartSearchCells;
    TArray<FRouteEntry> Entries;
    FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(PFTWalkingEntry), false, &Character);
    const FName Profile = Character.GetCapsuleComponent()->GetCollisionProfileName();
    const double HalfHeight = Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const auto CanWalk = [&](const FVector& Start, const FVector& End, double HeightTolerance)
    {
        if (!CanCross(Character, Start, End)) return false;
        // A clear capsule sweep can still pass over a gap or a drop.
        const int32 Samples = FMath::CeilToInt(FVector::Dist2D(Start, End) / (Step * 0.5));
        for (int32 Sample = 1; Sample < Samples; ++Sample)
        {
            const FVector Feet = FMath::Lerp(Start, End, double(Sample) / Samples) - FVector(0, 0, HalfHeight + 4.0);
            FHitResult FloorHit;
            if (!GetWorld()->LineTraceSingleByProfile(FloorHit, Feet + FVector(0, 0, FloorTolerance),
                Feet - FVector(0, 0, FloorTolerance), Profile, FloorQuery)
                || FloorHit.ImpactNormal.Z < Character.GetCharacterMovement()->GetWalkableFloorZ()
                || FMath::Abs(FloorHit.ImpactPoint.Z - Feet.Z) > HeightTolerance) return false;
        }
        return true;
    };
    if (Target.RoomStop.IsSet())
    {
        const FVector End = CapsuleCenter(Character, Target.RoomStop.GetValue());
        if (FVector::Dist2D(RaisedPosition, End) <= Step * DoorApproachCells
            && FMath::Abs(RaisedPosition.Z - End.Z) <= FloorTolerance && CanWalk(RaisedPosition, End, 7.0))
        {
            Points = {Position - FVector(0, 0, HalfHeight), Target.RoomStop.GetValue()};
            return true;
        }
        const FVector Approach = CapsuleCenter(Character, Nodes.FindChecked(Target.Cell).Floor);
        if (!CanWalk(Approach, End, 7.0)) return false;
    }
    for (int32 X = -StartSearchCells; X <= StartSearchCells; ++X)
    {
        for (int32 Y = -StartSearchCells; Y <= StartSearchCells; ++Y)
        {
            const FIntPoint Cell = Near + FIntPoint(X, Y);
            const FNode* Node = Nodes.Find(Cell);
            if (!Node) continue;
            const FVector Center = CapsuleCenter(Character, Node->Floor);
            const double Distance = FVector::Dist2D(Position, Center);
            if (Distance > EntryRange || FMath::Abs(Center.Z - RaisedPosition.Z) > FloorTolerance
                || !CanWalk(RaisedPosition, Center, FloorTolerance)) continue;
            const FVector Approach = Center - Position;
            const int32 Heading = Distance < 1.0 ? INDEX_NONE
                : (FMath::RoundToInt(Approach.Rotation().Yaw / 90.0) + 4) % 4;
            const double ApproachSeconds = Distance / Speed * (1.0 + ClearancePenalty(Node->StepsFromBoundary, Step));
            Entries.Add({Cell, ApproachSeconds, Heading});
        }
    }
    if (Entries.IsEmpty()) return false;

    const double SecondsPerStep = Step / Speed;
    // Prefer staying in the walkway over zigzagging between individual high-clearance cells.
    const double TurnCost = SecondsPerQuarterTurn + SecondsPerStep * TurnCostInGridSteps;
    const auto Reversed = FindFastestPath(Entries, Target.Cell, SecondsPerStep, [this](const FIntPoint& Cell)
    {
        return Nodes.FindChecked(Cell).Links;
    }, [this](const FIntPoint& Cell)
    {
        return ClearancePenalty(Nodes.FindChecked(Cell).StepsFromBoundary, Step);
    }, TurnCost);
    if (Reversed.IsEmpty()) return false;
    TArray<FVector> GridPath;
    GridPath.Reserve(Reversed.Num() + 1);
    if (FVector::Dist2D(Position, Nodes.FindChecked(Reversed.Last()).Floor) > 1.0)
    {
        GridPath.Add(Position - FVector(0, 0, HalfHeight));
    }
    for (int32 Index = Reversed.Num() - 1; Index >= 0; --Index) GridPath.Add(Nodes.FindChecked(Reversed[Index]).Floor);
    if (Target.RoomStop.IsSet()) GridPath.Add(Target.RoomStop.GetValue());
    Points = StraightenPath(GridPath, [&](const FVector& Start, const FVector& End)
    {
        return CanWalk(CapsuleCenter(Character, Start), CapsuleCenter(Character, End), 7.0);
    });
    return true;
}

bool UPFTRoutes::Plan(const ACharacter& Character, const FRouteTarget& Target)
{
    if (!BuildPath(Character, Target, Path)) return false;
    PathDistances = MeasurePath(Path);
    NextIndex = FMath::Min(1, Path.Num() - 1);
    TurnPoints = FindRouteTurns(Path);
    ++PlanRevision;
    return true;
}

bool UPFTRoutes::Preview(const ACharacter& Character, const FString& RoomId, TArray<FVector>& Points, FString& Estimate) const
{
    Points.Reset();
    Estimate.Reset();
    const FRouteTarget* Target = Destinations.Find(RoomId);
    if (!Target || !Character.GetCharacterMovement()->IsMovingOnGround()
        || !BuildPath(Character, *Target, Points)) return false;

    const double Speed = FMath::Max(1.0, static_cast<double>(Character.GetCharacterMovement()->MaxWalkSpeed));
    const double Distance = MeasurePath(Points).Last();
    const int32 Metres = FMath::RoundToInt(Distance / 100.0);
    const double TurnTime = FindRouteTurns(Points).Num() * SecondsPerQuarterTurn;
    Estimate = FString::Printf(TEXT("%s walk · %d m"), *WalkingTimeLabel(Distance / Speed + TurnTime), Metres);
    return true;
}

bool UPFTRoutes::Start(ACharacter& Character, const FString& RoomId, const FString& RoomName)
{
    const FRouteTarget* Target = Destinations.Find(RoomId);
    if (!Target) return false;
    if (!Character.GetCharacterMovement()->IsMovingOnGround() || !Plan(Character, *Target))
    {
        bCanRetry = true;
        if (!IsGuiding())
        {
            bArrived = false;
            if (ArrivalLabel) ArrivalLabel->SetVisibility(false);
            Message = FText::FromString(TEXT("Walk to a first-floor hallway, then choose Retry route."));
        }
        return false;
    }

    bArrived = false;
    bCanRetry = false;
    bReplanning = false;
    UpdateTime = 0;
    if (ArrivalLabel) ArrivalLabel->SetVisibility(false);
    Goal = *Target;
    Destination = RoomName;
    Walker = &Character;
    Tick(0);
    return IsGuiding() || HasArrived();
}

bool UPFTRoutes::IsTickable() const
{
    return !Path.IsEmpty() || bArrived;
}

void UPFTRoutes::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    const auto* Player = GetWorld()->GetFirstPlayerController();
    if (bArrived && ArrivalLabel && Player)
    {
        FVector ViewLocation;
        FRotator ViewRotation;
        Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
        ArrivalLabel->SetWorldRotation((ViewLocation - ArrivalLabel->GetComponentLocation()).Rotation());
    }
    if (Path.IsEmpty()) return;
    auto* Character = Walker.Get();
    if (!Character)
    {
        Stop();
        return;
    }
    if (!Player || Player->GetPawn() != Character) return;
    const FVector Position = Character->GetActorLocation();
    if (!bReplanning)
    {
        AdvanceWaypoints(*Character, Position);
        DrawPath(Position);
    }
    UpdateTime -= DeltaTime;
    if (UpdateTime > 0) return;
    UpdateTime = 0.15f;

    if (bReplanning)
    {
        if (!Character->GetCharacterMovement()->IsMovingOnGround() || !Plan(*Character, Goal))
        {
            bCanRetry = true;
            UpdateTime = 1.0f;
            Message = FText::FromString(Destination + TEXT("\nReturn to a first-floor hallway to resume directions."));
            return;
        }
        bReplanning = false;
        bCanRetry = false;
        DrawPath(Position);
    }
    const FVector End = Path.Last();
    if (Character->GetCharacterMovement()->IsMovingOnGround()
        && FVector::Dist2D(Position, End) <= ArrivalRadius
        && FMath::Abs(Position.Z - CapsuleCenter(*Character, End).Z) <= FloorTolerance
        && CanCross(*Character, Position + FVector(0, 0, 4), CapsuleCenter(*Character, End)))
    {
        bArrived = true;
        EndGuidance(FText::FromString(FString::Printf(TEXT("Arrived at %s."), *Destination)));
        ShowArrival(End);
        return;
    }

    const FVector& Previous = Path[FMath::Max(0, NextIndex - 1)];
    const FVector& Next = Path[NextIndex];
    const FVector Nearest = FMath::Lerp(Previous, Next, FMath::Clamp(SegmentProgress(Position, Previous, Next), 0.0, 1.0));
    if (FVector::Dist2D(Position, Nearest) > Step * 3.0
        || FMath::Abs(Position.Z - CapsuleCenter(*Character, Nearest).Z) > FloorTolerance)
    {
        bReplanning = true;
        UpdateTime = 0.45f;
        Message = FText::FromString(FString::Printf(TEXT("%s\nUpdating route…"), *Destination));
        return;
    }
    UpdateDirections(Position, Player->GetControlRotation(), Character->GetCharacterMovement()->MaxWalkSpeed);
}

void UPFTRoutes::AdvanceWaypoints(const ACharacter& Character, const FVector& Position)
{
    while (NextIndex + 1 < Path.Num())
    {
        const FVector& Previous = Path[NextIndex - 1];
        const FVector& Point = Path[NextIndex];
        const FVector& Next = Path[NextIndex + 1];
        const double Progress = SegmentProgress(Position, Point, Next);
        const double CornerDistance = FVector::Dist2D(Position, Point);
        const FVector OnNext = FMath::Lerp(Point, Next, FMath::Clamp(Progress, 0.0, 1.0));
        const bool bPassedCorner = Progress > 0.0 && CornerDistance <= Step * 2.0 && FVector::Dist2D(Position, OnNext) <= Step;
        if (CornerDistance > WaypointReach(Previous, Point, Next) && !bPassedCorner) return;
        const double Length = FVector::Dist2D(Point, Next);
        const double Ahead = Length > Step ? FMath::Clamp(FMath::Max(0.0, Progress) + Step / Length, 0.0, 1.0) : 1.0;
        if (!CanCross(Character, Position + FVector(0, 0, 4), CapsuleCenter(Character, FMath::Lerp(Point, Next, Ahead)))) return;
        ++NextIndex;
    }
}

void UPFTRoutes::UpdateDirections(const FVector& Position, const FRotator& View, double WalkSpeed)
{
    const FVector Toward = (NextPoint() - Position).GetSafeNormal2D();
    const FVector Right = FRotationMatrix(FRotator(0, View.Yaw, 0)).GetUnitAxis(EAxis::Y);
    const double Side = FVector::DotProduct(Right, Toward);
    const double Forward = FVector::CrossProduct(Toward, Right).Z;
    FString Instruction;
    if (Forward < -0.35) Instruction = TEXT("Turn around to follow the arrows");
    else if (Side > 0.55) Instruction = TEXT("Follow the arrows to your right");
    else if (Side < -0.55) Instruction = TEXT("Follow the arrows to your left");
    const double DistanceToPath = FVector::Dist2D(Position, NextPoint());
    const double DistanceLeft = DistanceToPath + PathDistances.Last() - PathDistances[NextIndex];
    const int32 Turn = Algo::LowerBound(TurnPoints, NextIndex);
    if (Instruction.IsEmpty())
    {
        if (TurnPoints.IsValidIndex(Turn))
        {
            const int32 Index = TurnPoints[Turn];
            const int32 Metres = FMath::RoundToInt((DistanceToPath + PathDistances[Index] - PathDistances[NextIndex]) / 100.0);
            Instruction = DescribeTurn(RouteBend(Path, Index), Metres, FVector2D(Path[Index]), Landmarks);
        }
        if (Instruction.IsEmpty())
        {
            const int32 Metres = FMath::RoundToInt(DistanceLeft / 100.0);
            Instruction = FString::Printf(TEXT("Continue to the destination · %d m"), Metres);
        }
    }
    const double SecondsLeft = DistanceLeft / FMath::Max(1.0, WalkSpeed) + (TurnPoints.Num() - Turn) * SecondsPerQuarterTurn;
    Message = FText::FromString(Destination + TEXT("\n") + Instruction + TEXT("\n") + WalkingTimeLabel(SecondsLeft) + TEXT(" left"));
}

void UPFTRoutes::DrawPath(const FVector& Position)
{
    if (!Trail)
    {
        Trail = NewObject<ULineBatchComponent>(this, TEXT("PFTWalkingTrail"));
        Trail->bCalculateAccurateBounds = true;
        Trail->RegisterComponentWithWorld(GetWorld());
        Trail->SetComponentTickEnabled(false);
    }
    // Stationary arrows only change with the player's position or the active route.
    const FStationaryTrail Current{FVector2D(Position), PlanRevision, NextIndex};
    if (bReducedMotion && LastStationaryTrail.IsSet() && LastStationaryTrail.GetValue() == Current) return;

    const int32 FirstIndex = FMath::Max(0, NextIndex - 1);
    const double Progress = FMath::Clamp(SegmentProgress(Position, Path[FirstIndex], Path[NextIndex]), 0.0, 1.0);
    const double VisibleStart = FMath::Lerp(PathDistances[FirstIndex], PathDistances[NextIndex], Progress);
    const double VisibleEnd = FMath::Min(VisibleStart + TrailLength, PathDistances.Last());

    const double AnimationDistance = bReducedMotion ? ArrowSpacing * 0.5 : GetWorld()->GetTimeSeconds() * ArrowSpeed;
    const double Phase = AnimationDistance - VisibleStart;
    double ArrowDistance = VisibleStart + FMath::Fmod(FMath::Fmod(Phase, ArrowSpacing) + ArrowSpacing, ArrowSpacing);
    // This component owns only the current trail. Reuse its buffer as the arrows move.
    TArray<FBatchedLine>& Lines = Trail->BatchedLines;
    Lines.Reset(56);
    const FVector FloorLift(0, 0, 8);
    for (int32 Index = FirstIndex; Index + 1 < Path.Num() && ArrowDistance < VisibleEnd; ++Index)
    {
        const double EdgeStart = PathDistances[Index];
        const double EdgeEnd = PathDistances[Index + 1];
        const FVector Direction = (Path[Index + 1] - Path[Index]).GetSafeNormal2D();
        while (ArrowDistance < EdgeEnd && ArrowDistance < VisibleEnd)
        {
            const double FadeIn = FMath::Clamp((ArrowDistance - VisibleStart) / 65.0, 0.0, 1.0);
            const double FadeOut = FMath::Clamp((VisibleEnd - ArrowDistance) / 100.0, 0.0, 1.0);
            const FVector Center = FMath::Lerp(Path[Index], Path[Index + 1], (ArrowDistance - EdgeStart) / (EdgeEnd - EdgeStart));
            AddArrow(Lines, Center + FloorLift, Direction, static_cast<float>(FMath::Lerp(0.45, 1.0, FadeIn * FadeOut)));
            ArrowDistance += ArrowSpacing;
        }
    }
    if (Lines.IsEmpty())
    {
        const FVector Start = Path.Num() > 1 ? Path[FirstIndex] : Position;
        const FVector Direction = (Path[NextIndex] - Start).GetSafeNormal2D();
        AddArrow(Lines, Path[NextIndex] + FloorLift, Direction.IsNearlyZero() ? FVector::ForwardVector : Direction, 1.0f);
    }
    Trail->MarkRenderStateDirty();
    if (bReducedMotion) LastStationaryTrail = Current;
    else LastStationaryTrail.Reset();
#if WITH_DEV_AUTOMATION_TESTS
    ++TrailDrawCount;
#endif
}

void UPFTRoutes::ShowArrival(const FVector& Position)
{
    if (!ArrivalLabel)
    {
        ArrivalLabel = NewObject<UTextRenderComponent>(this, TEXT("PFTArrivalLabel"));
        ArrivalLabel->SetHorizontalAlignment(EHTA_Center);
        ArrivalLabel->SetTextRenderColor(PFTPalette::Gold);
        ArrivalLabel->SetWorldSize(24);
        ArrivalLabel->RegisterComponentWithWorld(GetWorld());
    }
    ArrivalLabel->SetWorldLocation(Position + FVector(0, 0, 165));
    ArrivalLabel->SetText(FText::FromString(Destination));
    ArrivalLabel->SetVisibility(true);
}

void UPFTRoutes::EndGuidance(const FText& NewMessage)
{
    if (Trail) Trail->Flush();
    LastStationaryTrail.Reset();
    Walker.Reset();
    Path.Reset();
    PathDistances.Reset();
    TurnPoints.Reset();
    UpdateTime = 0;
    bReplanning = false;
    Message = NewMessage;
}

void UPFTRoutes::Stop()
{
    bArrived = false;
    bCanRetry = false;
    if (ArrivalLabel) ArrivalLabel->SetVisibility(false);
    EndGuidance(FText::GetEmpty());
}

TArrayView<const FVector> UPFTRoutes::GetRemainingPath() const
{
    if (!IsGuiding() || bReplanning || !Path.IsValidIndex(NextIndex)) return {};
    return MakeArrayView(Path).RightChop(NextIndex);
}

FText UPFTRoutes::DescribeSteps(const FVector& Position, TArrayView<const FVector> Points) const
{
    return FormatRouteSteps(Position, Points, MeasurePath(Points), FindRouteTurns(Points), 0, Landmarks);
}

FText UPFTRoutes::GetRemainingSteps() const
{
    const auto* Character = Walker.Get();
    if (!Character) return FText::GetEmpty();
    if (bReplanning) return FText::FromString(TEXT("Updating route…"));
    return FormatRouteSteps(Character->GetActorLocation(), Path, PathDistances, TurnPoints, NextIndex, Landmarks);
}

const TPair<FString, FVector2D>* UPFTRoutes::GetRouteLandmark(TArrayView<const FVector> Points) const
{
    return FindRouteLandmark(Points, Landmarks);
}

int32 UPFTRoutes::CopyWalkableCells(TArray<FIntPoint>& Cells) const
{
    Nodes.GenerateKeyArray(Cells);
    return Step;
}

void UPFTRoutes::Deinitialize()
{
    Stop();
    if (Trail) Trail->DestroyComponent();
    if (ArrivalLabel) ArrivalLabel->DestroyComponent();
    Super::Deinitialize();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTRouteStepsTest, "PFTNavigator.Routes.Steps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTRouteStepsTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> Path = {
        {0, 0, 0}, {60, 0, 0}, {120, 0, 0}, {180, 0, 0},
        {180, 60, 0}, {180, 120, 0}, {180, 180, 0}, {180, 240, 0}, {180, 300, 0}, {180, 360, 0},
        {240, 360, 0}, {300, 360, 0}, {360, 360, 0}};
    const TMap<FString, FVector2D> Signs = {{TEXT("Zone 1200 sign"), FVector2D(180, 0)}};
    const auto Turns = FindRouteTurns(Path);
    const auto Distances = MeasurePath(Path);
    TestTrue(TEXT("Each real corner appears once and in order"), Turns == TArray<int32>{3, 9});
    TestEqual(TEXT("The preview includes turns, a landmark, and a final leg"),
        FormatRouteSteps(FVector::ZeroVector, Path, Distances, Turns, 0, Signs).ToString(),
        FString(TEXT("Walk 2 m, then turn right near Zone 1200 sign\n\nWalk 4 m, then turn left\n\nWalk 2 m to the destination")));
    TestEqual(TEXT("The remaining steps omit a passed turn and shorten the current leg"),
        FormatRouteSteps(Path[4], Path, Distances, Turns, 4, Signs).ToString(),
        FString(TEXT("Walk 3 m, then turn left\n\nWalk 2 m to the destination")));
    TestEqual(TEXT("A turn at the next point stays visible"), FormatRouteSteps(Path[9], Path, Distances, Turns, 9, Signs).ToString(),
        FString(TEXT("Turn left\n\nWalk 2 m to the destination")));

    const TArray<FVector> Zigzag = {{0, 0, 0}, {60, 0, 0}, {60, 60, 0}, {120, 60, 0}, {120, 120, 0},
        {180, 120, 0}, {180, 180, 0}};
    const auto Clear = [](const FVector&, const FVector&) { return true; };
    const auto Straight = StraightenPath(Zigzag, Clear);
    TestTrue(TEXT("A grid staircase in a clear corridor becomes one straight leg"),
        Straight == TArray<FVector>{Zigzag[0], Zigzag.Last()} && FindRouteTurns(Straight).IsEmpty());
    TestEqual(TEXT("Directions use the length of the straight leg"),
        FormatRouteSteps(Zigzag[0], Straight, MeasurePath(Straight), {}, 0, {}).ToString(),
        FString(TEXT("Walk 3 m to the destination")));
    const TArray<FVector> HallwayJog = {{0, 0, 0}, {600, 0, 0}, {600, 240, 0}, {1800, 240, 0}, {1800, 0, 0}, {2400, 0, 0}};
    TestTrue(TEXT("A sideways grid detour within a clear hallway leaves one straight leg"),
        StraightenPath(HallwayJog, Clear) == TArray<FVector>{HallwayJog[0], HallwayJog.Last()});
    const auto Hallway = [](const FVector& Start, const FVector& End)
    {
        return Start.Y == End.Y || (Start.X == 180 && End.X == 180);
    };
    const auto Corners = StraightenPath(Path, Hallway);
    TestTrue(TEXT("Actual hallway turns remain when the shortcut is blocked"),
        Corners == TArray<FVector>{Path[0], Path[3], Path[9], Path.Last()});
    const TArray<FVector> ShortRoute = {{0, 0, 0}, {60, 0, 0}, {60, 60, 0}};
    const auto Blocked = [](const FVector&, const FVector&) { return false; };
    TestTrue(TEXT("A blocked shortcut keeps the corner"), StraightenPath(ShortRoute, Blocked) == ShortRoute);
    TestTrue(TEXT("A short route can still have a turn"), FindRouteTurns(ShortRoute) == TArray<int32>{1});
    TestEqual(TEXT("The final waypoint alone shows the destination"),
        FormatRouteSteps(Path.Last(), Path, Distances, Turns, Path.Num() - 1, Signs).ToString(),
        FString(TEXT("Destination ahead")));
    TestTrue(TEXT("An empty route has no steps"), FormatRouteSteps({}, {}, {}, {}, 0, {}).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTFastestPathTest, "PFTNavigator.Routes.FastestPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTFastestPathTest::RunTest(const FString& Parameters)
{
    const TMap<FString, FVector2D> Signs = {{TEXT("Zone 1200 sign"), FVector2D(100, 100)}};
    TestEqual(TEXT("An upcoming turn can use a nearby sign"), DescribeTurn(1.0, 3, FVector2D::ZeroVector, Signs),
        FString(TEXT("Turn right near Zone 1200 sign")));
    TestEqual(TEXT("A distant sign does not describe the turn"), DescribeTurn(-1.0, 3, FVector2D(500, 500), Signs),
        FString(TEXT("Turn left in 3 m")));
    TestEqual(TEXT("A distant turn keeps its distance"), DescribeTurn(-1.0, 12, FVector2D::ZeroVector, Signs),
        FString(TEXT("Turn left in 12 m")));
    TestEqual(TEXT("Graphs without landmarks keep the existing directions"), DescribeTurn(1.0, 1, FVector2D::ZeroVector, {}),
        FString(TEXT("Turn right ahead")));
    const TMap<FString, FVector2D> MapLandmarks = {
        {TEXT("Entrance sign"), FVector2D(50, 50)}, {TEXT("Commons sign"), FVector2D(500, 100)}};
    const TArray<FVector> MapPath = {FVector::ZeroVector, FVector(500, 90, 0), FVector(1000, 0, 0)};
    const auto* FirstSign = FindRouteLandmark(MapPath, MapLandmarks);
    if (TestNotNull(TEXT("A landmark next to the route can be marked"), FirstSign))
        TestEqual(TEXT("The map marks the first landmark along the path"), FirstSign->Key, FString(TEXT("Entrance sign")));
    const auto* NextSign = FindRouteLandmark(MakeArrayView(MapPath).RightChop(1), MapLandmarks);
    if (TestNotNull(TEXT("A second landmark can follow the first"), NextSign))
        TestEqual(TEXT("The map moves on from a passed landmark"), NextSign->Key, FString(TEXT("Commons sign")));
    TestNull(TEXT("Landmarks away from the remaining route are skipped"), FindRouteLandmark(MakeArrayView(MapPath).RightChop(2), MapLandmarks));
    TestNull(TEXT("The map works without any landmarks"), FindRouteLandmark(MapPath, {}));
    TestEqual(TEXT("Short walks use a relative time"), WalkingTimeLabel(3.0), FString(TEXT("Under 5 sec")));
    TestEqual(TEXT("Walking time is rounded to five seconds"), WalkingTimeLabel(23.0), FString(TEXT("About 25 sec")));
    TestEqual(TEXT("Whole minutes omit seconds"), WalkingTimeLabel(60.0), FString(TEXT("About 1 min")));
    TestEqual(TEXT("Long walks show minutes and seconds"), WalkingTimeLabel(72.0), FString(TEXT("About 1 min 10 sec")));
    TMap<FIntPoint, uint8> Graph;
    const auto LinkPath = [&](const TArray<FIntPoint>& Points)
    {
        for (int32 Index = 1; Index < Points.Num(); ++Index)
        {
            const FIntPoint Delta = Points[Index] - Points[Index - 1];
            for (int32 Direction = 0; Direction < UE_ARRAY_COUNT(Directions); ++Direction)
            {
                if (Delta != Directions[Direction]) continue;
                Graph.FindOrAdd(Points[Index - 1]) |= 1 << Direction;
                Graph.FindOrAdd(Points[Index]) |= 1 << ((Direction + 2) % 4);
                break;
            }
        }
    };
    LinkPath({{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}, {3, 2}, {3, 3}, {4, 3}, {4, 4}});
    LinkPath({{0, 0}, {-1, 0}, {-1, 1}, {-1, 2}, {-1, 3}, {-1, 4}, {0, 4}, {1, 4}, {2, 4}, {3, 4}, {4, 4}});
    const auto Links = [&](const FIntPoint& Cell) { return Graph.FindRef(Cell); };
    const TArray<FRouteEntry> Start = {{{0, 0}, 0.0}};
    TestEqual(TEXT("Fast walking favors the longer route with fewer turns"), FindFastestPath(Start, {4, 4}, 0.1, Links).Num(), 11);
    TestEqual(TEXT("Slow walking favors the shorter route"), FindFastestPath(Start, {4, 4}, 1.0, Links).Num(), 9);
    TestEqual(TEXT("The current position can already be the destination"), FindFastestPath({{{4, 4}, 0.0}}, {4, 4}, 0.1, Links).Num(), 1);
    TestTrue(TEXT("Disconnected destinations return no route"), FindFastestPath(Start, {9, 9}, 0.1, Links).IsEmpty());
    const auto FartherEntry = FindFastestPath({{{0, 0}, 0.02}, {{2, 4}, 0.2, 0}}, {4, 4}, 0.1, Links);
    TestTrue(TEXT("A farther entry can reduce total travel time"), !FartherEntry.IsEmpty() && FartherEntry.Last() == FIntPoint(2, 4));
    const auto CostlyEntry = FindFastestPath({{{0, 0}, 0.02}, {{2, 4}, 4.0, 0}}, {4, 4}, 0.1, Links);
    TestTrue(TEXT("An expensive approach does not win"), !CostlyEntry.IsEmpty() && CostlyEntry.Last() == FIntPoint(0, 0));

    Graph.Reset();
    LinkPath({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}});
    LinkPath({{0, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1}, {10, 1}, {10, 0}});
    const auto Penalty = [](const FIntPoint& Cell) { return ClearancePenalty(Cell.Y == 0 ? 0 : 2, 60); };
    const auto WallRoute = FindFastestPath(Start, {10, 0}, 0.25, Links);
    const auto WalkwayRoute = FindFastestPath(Start, {10, 0}, 0.25, Links, Penalty, 0.75);
    TestTrue(TEXT("A slightly longer route can stay in the clear walkway"),
        WallRoute.Contains(FIntPoint(4, 0)) && WalkwayRoute.Contains(FIntPoint(4, 1)) && WalkwayRoute[0] == FIntPoint(10, 0));
    const auto NarrowRoute = FindFastestPath(Start, {4, 0}, 0.25, Links, Penalty);
    TestTrue(TEXT("A required narrow approach remains available"), !NarrowRoute.IsEmpty() && NarrowRoute[0] == FIntPoint(4, 0));
    TestTrue(TEXT("Clearance costs taper toward the middle of the walkway"),
        ClearancePenalty(0, 60) > ClearancePenalty(1, 60) && ClearancePenalty(2, 60) == 0.0);
    const FVector Before(0, 0, 0);
    const FVector Corner(60, 0, 0);
    TestEqual(TEXT("A turn requires getting closer to the waypoint"), WaypointReach(Before, Corner, {60, 60, 0}), CornerRadius);
    TestEqual(TEXT("A straight section keeps its usual waypoint tolerance"), WaypointReach(Before, Corner, {120, 0, 0}), WaypointRadius);
    return true;
}
#endif
