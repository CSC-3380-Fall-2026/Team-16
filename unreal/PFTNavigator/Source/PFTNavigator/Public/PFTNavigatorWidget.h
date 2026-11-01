#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "PFTNavigatorWidget.generated.h"

class UTextBlock;
class UScrollBox;
class UEditableTextBox;

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
    UFUNCTION() void FloorChanged(FString Floor, ESelectInfo::Type SelectionType);
    UFUNCTION() void ClearRoom();
    void RefreshResults();
    UFUNCTION() void ToggleExplore();
    UFUNCTION() void Fly();
    UFUNCTION() void ViewRoom();
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Details;
    UPROPERTY() TObjectPtr<UEditableTextBox> Search;
    UPROPERTY() TObjectPtr<UComboBoxString> Floors;
    UPROPERTY() TObjectPtr<UButton> ViewButton;
    UPROPERTY() TObjectPtr<UButton> ClearButton;
    UPROPERTY() TObjectPtr<UTextBlock> ExploreLabel;
    UPROPERTY() TObjectPtr<UScrollBox> Results;
};
