#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PFTExploration.generated.h"

class APFTExplorePawn;
class UPFTNavigatorWidget;

UCLASS()
class PFTNAVIGATOR_API UPFTExploration : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable) bool BeginExplore();
    UFUNCTION(BlueprintCallable) bool EndExplore();
    UFUNCTION(BlueprintCallable) bool ViewRoom();
    UFUNCTION(BlueprintCallable) void SetCursor(bool bShow);
    UPROPERTY(BlueprintReadOnly) bool bCursorShown = false;
    UPROPERTY(BlueprintReadOnly) FString Error;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<APFTExplorePawn> ExplorePawn;
private:
    UPROPERTY() TObjectPtr<APawn> PreviousPawn;
    UPROPERTY() TObjectPtr<UPFTNavigatorWidget> Widget;
    FRotator PreviousRotation;
    bool bPreviousCursor = false;
};
