#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PFTNavigatorWidget.generated.h"

class UTextBlock;
class UScrollBox;

UCLASS()
class UPFTSearchButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY() FString RoomId;
    UPROPERTY() TObjectPtr<class UPFTNavigatorWidget> Panel;
    UFUNCTION() void SelectRoom();
};

UCLASS()
class PFTNAVIGATOR_API UPFTNavigatorWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ChooseRoom(const FString& RoomId);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UFUNCTION() void SearchChanged(const FText& Text);
    UFUNCTION() void ToggleExplore();
    UFUNCTION() void Fly();
    UFUNCTION() void ViewRoom();
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> ExploreLabel;
    UPROPERTY() TObjectPtr<UScrollBox> Results;
};
