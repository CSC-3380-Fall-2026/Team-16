#include "PFTRoutes.h"
#include "PFTPalette.h"
#include "Components/CapsuleComponent.h"
#include "Components/LineBatchComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

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

    TArray<FIntPoint> Queue = {Start};
    TMap<FIntPoint, FIntPoint> Parents = {{Start, Start}};
    for (int32 Index = 0; Index < Queue.Num() && !Parents.Contains(Target); ++Index)
    {
        const FIntPoint Cell = Queue[Index];
        const FNode& Node = Nodes.FindChecked(Cell);
        for (int32 Direction = 0; Direction < UE_ARRAY_COUNT(Directions); ++Direction)
        {
            const FIntPoint Neighbor = Cell + Directions[Direction];
            if ((Node.Links & (1 << Direction)) && Nodes.Contains(Neighbor) && !Parents.Contains(Neighbor))
            {
                Parents.Add(Neighbor, Cell);
                Queue.Add(Neighbor);
            }
        }
    }
    if (!Parents.Contains(Target)) return false;

    TArray<FVector> Reversed;
    for (FIntPoint Cell = Target; Cell != Start; Cell = Parents.FindChecked(Cell))
    {
        Reversed.Add(Nodes.FindChecked(Cell).Floor);
    }
    Reversed.Add(Nodes.FindChecked(Start).Floor);
    Path.Reset(Reversed.Num());
    for (int32 Index = Reversed.Num() - 1; Index >= 0; --Index) Path.Add(Reversed[Index]);
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
        Message = FText::FromString(TEXT("Walk onto a connected first-floor hallway and try again."));
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
    if (Path.IsEmpty()) return;
    auto* Character = Walker.Get();
    const auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Character || !Player || Player->GetPawn() != Character)
    {
        Stop();
        return;
    }
    DrawPath();
    UpdateTime -= DeltaTime;
    if (UpdateTime > 0) return;
    UpdateTime = 0.15f;

    const FVector Position = Character->GetActorLocation();
    const FVector End = Path.Last();
    if (Character->GetCharacterMovement()->IsMovingOnGround()
        && FVector::Dist2D(Position, End) <= ArrivalRadius
        && CanCross(*Character, Position + FVector(0, 0, 4), CapsuleCenter(*Character, End)))
    {
        bArrived = true;
        EndGuidance(FText::FromString(FString::Printf(TEXT("Arrived at %s."), *Destination)));
        return;
    }

    // Skip nearby points only if the walking capsule can reach the next point directly.
    while (NextIndex + 1 < Path.Num()
        && FVector::Dist2D(Position, Path[NextIndex]) <= WaypointRadius
        && CanCross(*Character, Position + FVector(0, 0, 4), CapsuleCenter(*Character, Path[NextIndex + 1])))
    {
        ++NextIndex;
    }

    if (FVector::Dist2D(Position, NextPoint()) > Step * 3.0)
    {
        if (!Plan(*Character, Goal))
        {
            EndGuidance(FText::FromString(TEXT("Route lost. Return to a first-floor hallway and try again.")));
            return;
        }
    }

    const double Distance = FVector::Dist2D(Position, NextPoint());
    const FVector Toward = (NextPoint() - Position).GetSafeNormal2D();
    const FVector Right = FRotationMatrix(FRotator(0, Player->GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y);
    const double Side = FVector::DotProduct(Right, Toward);
    const double Forward = FVector::CrossProduct(Toward, Right).Z;
    const TCHAR* Hint = TEXT("Walk ahead");
    if (Forward < -0.35) Hint = TEXT("Turn around");
    else if (Side > 0.55) Hint = TEXT("Turn right");
    else if (Side < -0.55) Hint = TEXT("Turn left");
    Message = FText::FromString(FString::Printf(TEXT("%s\n%s · %.1f m to next marker"), *Destination, Hint, Distance / 100.0));
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
    const double Phase = GetWorld()->GetTimeSeconds() * ArrowSpeed - NextIndex * Step;
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

void UPFTRoutes::EndGuidance(const FText& NewMessage)
{
    if (Trail) Trail->Flush();
    Walker.Reset();
    Path.Reset();
    UpdateTime = 0;
    Message = NewMessage;
}

void UPFTRoutes::Stop()
{
    bArrived = false;
    EndGuidance(FText::GetEmpty());
}

void UPFTRoutes::Deinitialize()
{
    Stop();
    if (Trail) Trail->DestroyComponent();
    Super::Deinitialize();
}
