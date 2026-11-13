#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PFTDestinations.generated.h"

class APFTRoomMarker;

USTRUCT()
struct FPFTRoomBinding
{
    GENERATED_BODY()
    UPROPERTY() FString RoomId;
    UPROPERTY() FString Map;
    UPROPERTY() FVector Location = FVector::ZeroVector;
};

UCLASS()
class PFTNAVIGATOR_API UPFTDestinations : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    UFUNCTION(BlueprintCallable) bool LoadBindings(const FString& Path);
    UFUNCTION(BlueprintCallable) bool SelectRoom(const FString& RoomId);
    UFUNCTION(BlueprintCallable) bool HasLocation(const FString& RoomId) const;
    UFUNCTION(BlueprintCallable) void ClearSelection();
    void SetRoomPreviewActive(bool bActive);
    bool IsRoomPreviewActive() const { return bRoomPreviewActive; }
    UPROPERTY(BlueprintReadOnly) FString Error;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<APFTRoomMarker> ActiveMarker;
private:
    UPROPERTY() TArray<FPFTRoomBinding> Bindings;
    UPROPERTY() TArray<TObjectPtr<APFTRoomMarker>> SpawnedMarkers;
    bool bRoomPreviewActive = false;
};
