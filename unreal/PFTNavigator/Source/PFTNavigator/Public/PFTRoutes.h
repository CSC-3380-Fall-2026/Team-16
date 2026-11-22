#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PFTRoutes.generated.h"

class ACharacter;
class ULineBatchComponent;
class UTextRenderComponent;

UCLASS()
class PFTNAVIGATOR_API UPFTRoutes : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    void OnWorldBeginPlay(UWorld& World) override;
    bool IsTickable() const override;
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    void Deinitialize() override;

    bool HasDestination(const FString& RoomId) const { return Destinations.Contains(RoomId); }
    bool Preview(const ACharacter& Character, const FString& RoomId, TArray<FVector>& Points, FString& Estimate) const;
    bool Start(ACharacter& Character, const FString& RoomId, const FString& RoomName);
    void Stop();

    bool IsGuiding() const { return Walker.IsValid(); }
    bool HasArrived() const { return bArrived; }
    bool CanRetry() const { return bCanRetry; }
    bool IsReplanning() const { return bReplanning; }
    void SetReducedMotion(bool bEnabled) { bReducedMotion = bEnabled; }
    FVector NextPoint() const { return Path.IsValidIndex(NextIndex) ? Path[NextIndex] : FVector::ZeroVector; }
    const FText& GetMessage() const { return Message; }
    uint32 GetPlanRevision() const { return PlanRevision; }
    TArrayView<const FVector> GetRemainingPath() const;
    FText DescribeSteps(const FVector& Position, TArrayView<const FVector> Points) const;
    FText GetRemainingSteps() const;
    const TPair<FString, FVector2D>* GetRouteLandmark(TArrayView<const FVector> Points) const;
    int32 CopyWalkableCells(TArray<FIntPoint>& Cells) const;

private:
    struct FNode
    {
        FVector Floor;
        uint8 Links;
        int32 StepsFromBoundary = 0;
    };

    struct FRouteTarget
    {
        FIntPoint Cell = FIntPoint::ZeroValue;
        TOptional<FVector> RoomStop;
    };

    void LoadGraph(const FString& Map);
    void MeasureClearance();
    bool Plan(const ACharacter& Character, const FRouteTarget& Target);
    bool BuildPath(const ACharacter& Character, const FRouteTarget& Target, TArray<FVector>& Points) const;
    bool CanCross(const ACharacter& Character, const FVector& Start, const FVector& End) const;
    void AdvanceWaypoints(const ACharacter& Character, const FVector& Position);
    void DrawPath(const FVector& Position);
    void ShowArrival(const FVector& Position);
    void UpdateDirections(const FVector& Position, const FRotator& View, double WalkSpeed);
    void EndGuidance(const FText& NewMessage);

    TMap<FIntPoint, FNode> Nodes;
    TMap<FString, FRouteTarget> Destinations;
    TMap<FString, FVector2D> Landmarks;
    TArray<FVector> Path;
    TArray<double> PathDistances;
    TArray<int32> TurnPoints;
    UPROPERTY() TObjectPtr<ULineBatchComponent> Trail;
    UPROPERTY() TObjectPtr<UTextRenderComponent> ArrivalLabel;
    TWeakObjectPtr<ACharacter> Walker;
    FString Destination;
    FRouteTarget Goal;
    FText Message;
    int32 Step = 0;
    int32 NextIndex = 0;
    uint32 PlanRevision = 0;
    float UpdateTime = 0;
    bool bArrived = false;
    bool bCanRetry = false;
    bool bReplanning = false;
    bool bReducedMotion = false;
};
