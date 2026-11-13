#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "PFTNavigatorWidget.generated.h"

class UTextBlock;
class UScrollBox;
class UEditableTextBox;
class UCheckBox;

UCLASS()
class UPFTSearchButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY() FString RoomId;
    UPROPERTY() TObjectPtr<class UPFTNavigatorWidget> Panel;
    // The result's normal text and label, kept so the Selected tag can be added and removed
    UPROPERTY() FString ResultText;
    UPROPERTY() TObjectPtr<UTextBlock> Label;
    UFUNCTION() void SelectRoom();
};

UCLASS()
class PFTNAVIGATOR_API UPFTNavigatorWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void ChooseRoom(const FString& RoomId);
    void UpdateCameraControls(bool bFreeCamera, bool bCursorShown, bool bRoomPreview);
    void UpdateRouteControls();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UFUNCTION() void SearchChanged(const FText& Text);
    UFUNCTION() void FloorChanged(FString Floor, ESelectInfo::Type SelectionType);
    UFUNCTION() void PreviewFilterChanged(bool bChecked);
    UFUNCTION() void ClearRoom();
    UFUNCTION() void ShowMoreRooms();
    void RefreshResults();
    void AddResultPage();
    void UpdateSelectedTags();
    UFUNCTION() void ToggleFreeCamera();
    UFUNCTION() void ViewRoom();
    UFUNCTION() void ToggleRoute();
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Details;
    UPROPERTY() TObjectPtr<UEditableTextBox> Search;
    UPROPERTY() TObjectPtr<UComboBoxString> Floors;
    UPROPERTY() TObjectPtr<UCheckBox> PreviewOnly;
    UPROPERTY() TObjectPtr<UButton> ViewButton;
    UPROPERTY() TObjectPtr<UButton> ClearButton;
    UPROPERTY() TObjectPtr<UButton> RouteButton;
    UPROPERTY() TObjectPtr<UTextBlock> RouteLabel;
    UPROPERTY() TObjectPtr<UTextBlock> RouteHint;
    UPROPERTY() TObjectPtr<UTextBlock> CameraLabel;
    UPROPERTY() TObjectPtr<UTextBlock> CameraHelp;
    UPROPERTY() TObjectPtr<UScrollBox> Results;
    UPROPERTY() TObjectPtr<UButton> MoreRoomsButton;
    // Room that's currently picked, empty when nothing is selected
    FString SelectedRoomId;
};
