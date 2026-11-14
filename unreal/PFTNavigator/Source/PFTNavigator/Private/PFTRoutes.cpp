#include "PFTRoutes.h"
#include "PFTPalette.h"
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
    constexpr double ArrivalRadius = 65.0;
    constexpr double FloorTolerance = 45.0;
    constexpr int32 StartSearchCells = 3;
    constexpr double ArrowSpacing = 125.0;
    constexpr double ArrowSpeed = 85.0;
    constexpr double TrailLength = 1600.0;
    constexpr double SecondsPerQuarterTurn = 0.25;

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

    // A cell reached from different headings can have a different cost for the next turn.
    TArray<FIntPoint> FindFastestPath(const FIntPoint& Start, const FIntPoint& Target, double SecondsPerStep,
        TFunctionRef<uint8(const FIntPoint&)> GetLinks)
    {
        const auto RemainingTime = [&](const FIntPoint& Cell)
        {
            return (FMath::Abs(Cell.X - Target.X) + FMath::Abs(Cell.Y - Target.Y)) * SecondsPerStep;
        };
        const FIntVector First(Start.X, Start.Y, INDEX_NONE);
        TArray<FRouteCandidate> Open = {{First, 0.0, RemainingTime(Start)}};
        TMap<FIntVector, FRouteVisit> Visits = {{First, {0.0, First}}};
        while (!Open.IsEmpty())
        {
            FRouteCandidate Current;
            Open.HeapPop(Current, EAllowShrinking::No);
            if (Current.Seconds > Visits.FindChecked(Current.State).Seconds) continue;
            const FIntPoint Cell(Current.State.X, Current.State.Y);
            if (Cell == Target)
            {
                TArray<FIntPoint> Reversed = {Cell};
                for (FIntVector State = Current.State; State != First;)
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
                const double Seconds = Current.Seconds + SecondsPerStep + Turns * SecondsPerQuarterTurn;
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
    for (const auto& Room : (*Rooms)->Values)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values;
        double X, Y;
        if (Room.Value->TryGetArray(Values) && Values->Num() == 2
            && (*Values)[0]->TryGetNumber(X) && (*Values)[1]->TryGetNumber(Y)
            && FMath::Abs(X) <= 10000 && FMath::Abs(Y) <= 10000)
        {
            const FIntPoint Cell{int32(X), int32(Y)};
            if (GraphNodes.Contains(Cell)) Destinations.Add(FString(Room.Key), Cell);
        }
    }
    Step = GridStep;
    Nodes = MoveTemp(GraphNodes);
}

bool UPFTRoutes::CanCross(ACharacter& Character, const FVector& Start, const FVector& End) const
{
    const auto* Capsule = Character.GetCapsuleComponent();
    const auto Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PFTWalkingRoute), false, &Character);
    return !GetWorld()->SweepTestByProfile(Start, End, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Query);
}

bool UPFTRoutes::Plan(ACharacter& Character, const FIntPoint& Target)
{
    const FVector Position = Character.GetActorLocation();
    const FVector RaisedPosition = Position + FVector(0, 0, 4);
    const FIntPoint Near(FMath::RoundToInt(Position.X / Step), FMath::RoundToInt(Position.Y / Step));
    FIntPoint Start = Near;
    double Best = TNumericLimits<double>::Max();
    for (int32 X = -StartSearchCells; X <= StartSearchCells; ++X)
    {
        for (int32 Y = -StartSearchCells; Y <= StartSearchCells; ++Y)
        {
            const FIntPoint Cell = Near + FIntPoint(X, Y);
            const FNode* Node = Nodes.Find(Cell);
            if (!Node) continue;
            const FVector Center = CapsuleCenter(Character, Node->Floor);
            const double Distance = FVector::DistSquared(RaisedPosition, Center);
            if (FMath::Abs(Center.Z - RaisedPosition.Z) <= FloorTolerance && Distance < Best
                && CanCross(Character, RaisedPosition, Center))
            {
                Start = Cell;
                Best = Distance;
            }
        }
    }
    if (Best == TNumericLimits<double>::Max()) return false;

    const double Speed = FMath::Max(1.0, static_cast<double>(Character.GetCharacterMovement()->MaxWalkSpeed));
    const auto Reversed = FindFastestPath(Start, Target, Step / Speed, [this](const FIntPoint& Cell)
    {
        const auto* Node = Nodes.Find(Cell);
        if (!Node) return uint8(0);
        uint8 Links = Node->Links;
        for (int32 Direction = 0; Direction < UE_ARRAY_COUNT(Directions); ++Direction)
        {
            if (!Nodes.Contains(Cell + Directions[Direction])) Links &= ~(1 << Direction);
        }
        return Links;
    });
    if (Reversed.IsEmpty()) return false;
    Path.Reset(Reversed.Num());
    for (int32 Index = Reversed.Num() - 1; Index >= 0; --Index) Path.Add(Nodes.FindChecked(Reversed[Index]).Floor);
    NextIndex = 0;
    return true;
}

bool UPFTRoutes::Start(ACharacter& Character, const FString& RoomId, const FString& RoomName)
{
    Stop();
    const FIntPoint* Target = Destinations.Find(RoomId);
    if (!Target) return false;
    if (!Character.GetCharacterMovement()->IsMovingOnGround() || !Plan(Character, *Target))
    {
        bCanRetry = true;
        Message = FText::FromString(TEXT("Walk to a first-floor hallway, then choose Retry route."));
        return false;
    }

    Goal = *Target;
    Destination = RoomName;
    Walker = &Character;
    Tick(0);
    return IsGuiding() || HasArrived();
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
    if (!Character || !Player || Player->GetPawn() != Character)
    {
        Stop();
        return;
    }
    if (!bReplanning) DrawPath();
    UpdateTime -= DeltaTime;
    if (UpdateTime > 0) return;
    UpdateTime = 0.15f;

    const FVector Position = Character->GetActorLocation();
    if (bReplanning)
    {
        bReplanning = false;
        if (!Character->GetCharacterMovement()->IsMovingOnGround() || !Plan(*Character, Goal))
        {
            bCanRetry = true;
            EndGuidance(FText::FromString(TEXT("Route lost. Walk to a first-floor hallway, then choose Retry route.")));
            return;
        }
    }
    const FVector End = Path.Last();
    if (Character->GetCharacterMovement()->IsMovingOnGround()
        && FVector::Dist2D(Position, End) <= ArrivalRadius
        && CanCross(*Character, Position + FVector(0, 0, 4), CapsuleCenter(*Character, End)))
    {
        bArrived = true;
        EndGuidance(FText::FromString(FString::Printf(TEXT("Arrived at %s."), *Destination)));
        ShowArrival(End);
        return;
    }

    // Skip nearby points only if the walking capsule can reach the next point directly.
    while (NextIndex + 1 < Path.Num()
        && FVector::Dist2D(Position, Path[NextIndex]) <= WaypointRadius
        && CanCross(*Character, Position + FVector(0, 0, 4), CapsuleCenter(*Character, Path[NextIndex + 1])))
    {
        ++NextIndex;
    }

    if (FVector::Dist2D(Position, NextPoint()) > Step * 3.0
        || FMath::Abs(Position.Z - CapsuleCenter(*Character, NextPoint()).Z) > FloorTolerance)
    {
        bReplanning = true;
        UpdateTime = 0.45f;
        if (Trail) Trail->Flush();
        Message = FText::FromString(FString::Printf(TEXT("%s\nUpdating route…"), *Destination));
        return;
    }
    UpdateDirections(Position, Player->GetControlRotation());
}

void UPFTRoutes::UpdateDirections(const FVector& Position, const FRotator& View)
{
    const FVector Toward = (NextPoint() - Position).GetSafeNormal2D();
    const FVector Right = FRotationMatrix(FRotator(0, View.Yaw, 0)).GetUnitAxis(EAxis::Y);
    const double Side = FVector::DotProduct(Right, Toward);
    const double Forward = FVector::CrossProduct(Toward, Right).Z;
    FString Instruction;
    if (Forward < -0.35) Instruction = TEXT("Turn around to follow the arrows");
    else if (Side > 0.55) Instruction = TEXT("Follow the arrows to your right");
    else if (Side < -0.55) Instruction = TEXT("Follow the arrows to your left");
    if (Instruction.IsEmpty())
    {
        const int32 LookAhead = FMath::Max(2, FMath::CeilToInt(180.0 / Step));
        const double DistanceToPath = FVector::Dist2D(Position, NextPoint());
        for (int32 Index = NextIndex + LookAhead; Index + LookAhead < Path.Num(); ++Index)
        {
            const FVector Incoming = (Path[Index] - Path[Index - LookAhead]).GetSafeNormal2D();
            const FVector Outgoing = (Path[Index + LookAhead] - Path[Index]).GetSafeNormal2D();
            const double Turn = FVector::CrossProduct(Incoming, Outgoing).Z;
            if (FMath::Abs(Turn) < 0.85) continue;
            const int32 Metres = FMath::RoundToInt((DistanceToPath + (Index - NextIndex) * Step) / 100.0);
            const TCHAR* SideName = Turn > 0 ? TEXT("right") : TEXT("left");
            Instruction = Metres <= 1 ? FString::Printf(TEXT("Turn %s ahead"), SideName)
                : FString::Printf(TEXT("Turn %s in %d m"), SideName, Metres);
            break;
        }
        if (Instruction.IsEmpty())
        {
            const int32 Metres = FMath::RoundToInt((DistanceToPath + (Path.Num() - 1 - NextIndex) * Step) / 100.0);
            Instruction = FString::Printf(TEXT("Continue to the destination · %d m"), Metres);
        }
    }
    Message = FText::FromString(Destination + TEXT("\n") + Instruction);
}

void UPFTRoutes::DrawPath()
{
    if (!Trail)
    {
        Trail = NewObject<ULineBatchComponent>(this, TEXT("PFTWalkingTrail"));
        Trail->bCalculateAccurateBounds = true;
        Trail->RegisterComponentWithWorld(GetWorld());
    }
    Trail->Flush();

    // Grid edges are one Step long in XY. Keep the animation phase when the first edge is skipped.
    const double AnimationDistance = bReducedMotion ? ArrowSpacing * 0.5 : GetWorld()->GetTimeSeconds() * ArrowSpeed;
    const double Phase = AnimationDistance - NextIndex * Step;
    double ArrowDistance = FMath::Fmod(FMath::Fmod(Phase, ArrowSpacing) + ArrowSpacing, ArrowSpacing);
    TArray<FBatchedLine> Lines;
    Lines.Reserve(56);
    const FVector FloorLift(0, 0, 8);
    const double VisibleLength = FMath::Min(TrailLength, static_cast<double>(Path.Num() - NextIndex - 1) * Step);
    double EdgeStart = 0;
    for (int32 Index = NextIndex; EdgeStart < VisibleLength; ++Index)
    {
        const double EdgeEnd = EdgeStart + Step;
        const FVector Direction = (Path[Index + 1] - Path[Index]).GetSafeNormal2D();
        while (ArrowDistance < EdgeEnd && ArrowDistance < VisibleLength)
        {
            const double FadeIn = FMath::Clamp(ArrowDistance / 65.0, 0.0, 1.0);
            const double FadeOut = FMath::Clamp((VisibleLength - ArrowDistance) / 100.0, 0.0, 1.0);
            const FVector Center = FMath::Lerp(Path[Index], Path[Index + 1], (ArrowDistance - EdgeStart) / Step);
            AddArrow(Lines, Center + FloorLift, Direction, static_cast<float>(FadeIn * FadeOut));
            ArrowDistance += ArrowSpacing;
        }
        EdgeStart = EdgeEnd;
    }
    if (Lines.IsEmpty() && Path.Num() >= 2)
    {
        const FVector Direction = (Path.Last() - Path[Path.Num() - 2]).GetSafeNormal2D();
        AddArrow(Lines, Path.Last() + FloorLift, Direction, 1.0f);
    }
    Trail->DrawLines(Lines);
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
    Walker.Reset();
    Path.Reset();
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

void UPFTRoutes::Deinitialize()
{
    Stop();
    if (Trail) Trail->DestroyComponent();
    if (ArrivalLabel) ArrivalLabel->DestroyComponent();
    Super::Deinitialize();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTFastestPathTest, "PFTNavigator.Routes.FastestPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTFastestPathTest::RunTest(const FString& Parameters)
{
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
    TestEqual(TEXT("Fast walking favors the longer route with fewer turns"), FindFastestPath({0, 0}, {4, 4}, 0.1, Links).Num(), 11);
    TestEqual(TEXT("Slow walking favors the shorter route"), FindFastestPath({0, 0}, {4, 4}, 1.0, Links).Num(), 9);
    TestEqual(TEXT("The current position can already be the destination"), FindFastestPath({4, 4}, {4, 4}, 0.1, Links).Num(), 1);
    TestTrue(TEXT("Disconnected destinations return no route"), FindFastestPath({0, 0}, {9, 9}, 0.1, Links).IsEmpty());
    return true;
}
#endif
