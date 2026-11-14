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
class UVerticalBox;

UCLASS()
class UPFTSearchButton : public UButton
{
    GENERATED_BODY()
public:
    UPROPERTY() FString RoomId;
    // Which course this result is for. Empty unless it came from class search or My classes
    UPROPERTY() FString CourseId;
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
    void ChooseRoom(const FString& RoomId, const FString& CourseId = FString());
    void UpdateCameraControls(bool bFreeCamera, bool bCursorShown, bool bRoomPreview);
    void UpdateRouteControls();
    // Called every frame from the exploration tick, but only redoes My classes when the minute changes
    void UpdateMyClassesClock();
    // Lets a test pick the day and time instead of using the real clock. Param: the time to pretend it is
    void SetClockForTests(const FDateTime& Time);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UFUNCTION() void SearchChanged(const FText& Text);
    UFUNCTION() void FloorChanged(FString Floor, ESelectInfo::Type SelectionType);
    UFUNCTION() void PreviewFilterChanged(bool bChecked);
    UFUNCTION() void ClassFilterChanged(bool bChecked);
    UFUNCTION() void ResetClassSearch();
    UFUNCTION() void ClearRoom();
    UFUNCTION() void ShowMoreRooms();
    void RefreshResults();
    void AddResultPage();
    void UpdateSelectedTags();
    UFUNCTION() void ToggleFreeCamera();
    UFUNCTION() void ViewRoom();
    UFUNCTION() void StartRoute();
    UFUNCTION() void StopRoute();
    UFUNCTION() void FindAnotherRoom();
    UFUNCTION() void ToggleSettings();
    UFUNCTION() void MotionChanged(bool bChecked);
    UFUNCTION() void DirectionsSizeChanged(bool bChecked);
    UFUNCTION() void PerformanceChanged(bool bChecked);
    UFUNCTION() void MyClassesChanged(bool bChecked);
    UFUNCTION() void ToggleSavedClass();
    void RefreshMyClasses();
    void UpdateSaveClassButton();
    FDateTime GetNow() const;
    void SetCompactDirections(bool bCompact);
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Details;
    UPROPERTY() TObjectPtr<UEditableTextBox> Search;
    UPROPERTY() TObjectPtr<UComboBoxString> Floors;
    UPROPERTY() TObjectPtr<UCheckBox> PreviewOnly;
    UPROPERTY() TObjectPtr<UCheckBox> ClassesOnly;
    UPROPERTY() TObjectPtr<UVerticalBox> ClassFilters;
    UPROPERTY() TObjectPtr<UEditableTextBox> InstructorSearch;
    UPROPERTY() TObjectPtr<UComboBoxString> ClassDay;
    UPROPERTY() TObjectPtr<UComboBoxString> ClassStart;
    TArray<FString> ClassStartTimes;
    UPROPERTY() TObjectPtr<UButton> ViewButton;
    UPROPERTY() TObjectPtr<UButton> ClearButton;
    UPROPERTY() TObjectPtr<UButton> RouteButton;
    UPROPERTY() TObjectPtr<UTextBlock> RouteUnavailable;
    UPROPERTY() TObjectPtr<UTextBlock> RouteHint;
    UPROPERTY() TObjectPtr<UScrollBox> Browser;
    UPROPERTY() TObjectPtr<UVerticalBox> Directions;
    UPROPERTY() TObjectPtr<UVerticalBox> Settings;
    UPROPERTY() TObjectPtr<UButton> StopRouteButton;
    UPROPERTY() TObjectPtr<UButton> RetryRouteButton;
    UPROPERTY() TObjectPtr<UButton> FindAnotherRoomButton;
    UPROPERTY() TObjectPtr<UTextBlock> CameraLabel;
    UPROPERTY() TObjectPtr<UTextBlock> CameraHelp;
    UPROPERTY() TObjectPtr<UScrollBox> Results;
    UPROPERTY() TObjectPtr<UButton> MoreRoomsButton;
    UPROPERTY() TObjectPtr<UCheckBox> MyClassesToggle;
    UPROPERTY() TObjectPtr<UVerticalBox> MyClassesBox;
    UPROPERTY() TObjectPtr<UTextBlock> MyClassesStatus;
    UPROPERTY() TObjectPtr<UVerticalBox> MyClassesList;
    UPROPERTY() TObjectPtr<UButton> SaveClassButton;
    UPROPERTY() TObjectPtr<UTextBlock> SaveClassLabel;
    // Course Names the student saved, like "CSC_1350"
    TArray<FString> MyClasses;
    // Course that's currently picked, empty when the pick didn't come from a class result
    FString SelectedCourseId;
    // Zero means use the real clock
    FDateTime TestClock;
    int32 LastClockMinute = -1;
    // Room that's currently picked, empty when nothing is selected
    FString SelectedRoomId;
    bool bCompactDirections = false;
    bool bResettingClassSearch = false;
    float StandardRenderScale = 100.0f;
};
