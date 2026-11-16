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
    void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    void Deinitialize() override;

    bool HasDestination(const FString& RoomId) const { return Destinations.Contains(RoomId); }
    bool Start(ACharacter& Character, const FString& RoomId, const FString& RoomName);
    void Stop();

    bool IsGuiding() const { return Walker.IsValid(); }
    bool HasArrived() const { return bArrived; }
    bool CanRetry() const { return bCanRetry; }
    bool IsReplanning() const { return bReplanning; }
    void SetReducedMotion(bool bEnabled) { bReducedMotion = bEnabled; }
    FVector NextPoint() const { return Path.IsValidIndex(NextIndex) ? Path[NextIndex] : FVector::ZeroVector; }
    const FText& GetMessage() const { return Message; }

private:
    struct FNode
    {
        FVector Floor;
        uint8 Links;
    };

    void LoadGraph(const FString& Map);
    bool Plan(ACharacter& Character, const FIntPoint& Goal);
    bool CanCross(ACharacter& Character, const FVector& Start, const FVector& End) const;
    void DrawPath();
    void ShowArrival(const FVector& Position);
    void UpdateDirections(const FVector& Position, const FRotator& View, double WalkSpeed);
    void EndGuidance(const FText& NewMessage);

    TMap<FIntPoint, FNode> Nodes;
    TMap<FString, FIntPoint> Destinations;
    TArray<FVector> Path;
    TArray<double> TurnSeconds;
    UPROPERTY() TObjectPtr<ULineBatchComponent> Trail;
    UPROPERTY() TObjectPtr<UTextRenderComponent> ArrivalLabel;
    TWeakObjectPtr<ACharacter> Walker;
    FString Destination;
    FIntPoint Goal;
    FText Message;
    int32 Step = 0;
    int32 NextIndex = 0;
    float UpdateTime = 0;
    bool bArrived = false;
    bool bCanRetry = false;
    bool bReplanning = false;
    bool bReducedMotion = false;
};
