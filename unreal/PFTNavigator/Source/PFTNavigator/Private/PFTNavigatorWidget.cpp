#include "PFTNavigatorWidget.h"
#include "PFTDirectory.h"
#include "PFTDestinations.h"
#include "PFTExploration.h"
#include "PFTExplorePawn.h"
#include "PFTPalette.h"
#include "PFTRoutes.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"

namespace
{
    constexpr int32 RoomsPerPage = 50;
    const TCHAR* ClassDayCodes = TEXT("MTWRFSU");
    const TCHAR* SettingsSection = TEXT("PFTNavigator");

    void SavePreference(const TCHAR* Name, bool bEnabled)
    {
        GConfig->SetBool(SettingsSection, Name, bEnabled, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }

    void ApplyPerformancePreset(bool bEnabled, float StandardRenderScale)
    {
        if (auto* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
        {
            // The Windows launcher sets this at console priority.
            ScreenPercentage->Set(bEnabled ? FMath::Min(75.0f, StandardRenderScale) : StandardRenderScale, ECVF_SetByConsole);
        }
    }
}

void UPFTSearchButton::SelectRoom()
{
    if (Panel) Panel->ChooseRoom(RoomId, CourseId);
}

TSharedRef<SWidget> UPFTNavigatorWidget::RebuildWidget()
{
    if (WidgetTree->RootWidget) return Super::RebuildWidget();
    auto* Border = WidgetTree->ConstructWidget<UBorder>();
    Border->SetPadding(FMargin(14));
    // Light background so the ink text and purple buttons from the LSU palette are easy to read
    Border->SetBrushColor(FLinearColor(FColor(255, 255, 255, 245)));
    WidgetTree->RootWidget = Border;
    auto* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Border->SetContent(Content);
    Browser = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RoomBrowser"));
    Content->AddChildToVerticalBox(Browser)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Browser->AddChild(Column);
    auto MakeText = [&](const FString& Text)
    {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text));
        Label->SetAutoWrapText(true);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
        return Label;
    };
    auto AddText = [&](const FString& Text)
    {
        auto* Label = MakeText(Text);
        Column->AddChild(Label);
        return Label;
    };
    // The heading is purple like the rest of the controls in Claire's design
    auto* Title = AddText(TEXT("PFT Navigator"));
    Title->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20));
    Title->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Purple)));
    Search = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomSearch"));
    auto Style = Search->GetWidgetStyle();
    Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16));
    Style.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
    Search->SetWidgetStyle(Style);
    Search->SetForegroundColor(FLinearColor(PFTPalette::Ink));
    Search->SetHintText(FText::FromString(TEXT("Room, course, or department")));
    Search->OnTextChanged.AddDynamic(this, &UPFTNavigatorWidget::SearchChanged);
    Column->AddChild(Search);
    AddText(TEXT("Floor"));
    Floors = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("FloorFilter"));
    Floors->AddOption(TEXT("All floors"));
    TArray<int32> FloorNumbers;
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    for (const auto& Room : Directory->Rooms) FloorNumbers.AddUnique(Room.Floor);
    FloorNumbers.Sort();
    for (int32 Floor : FloorNumbers) Floors->AddOption(FString::FromInt(Floor));
    Floors->SetSelectedOption(TEXT("All floors"));
    Floors->OnSelectionChanged.AddDynamic(this, &UPFTNavigatorWidget::FloorChanged);
    Column->AddChild(Floors);
    ClassesOnly = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("ClassSearchFilter"));
    ClassesOnly->SetContent(MakeText(TEXT("Find a class by instructor or time")));
    ClassesOnly->OnCheckStateChanged.AddDynamic(this, &UPFTNavigatorWidget::ClassFilterChanged);
    Column->AddChild(ClassesOnly);
    ClassFilters = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ClassFilters"));
    Column->AddChild(ClassFilters);
    ClassFilters->SetVisibility(ESlateVisibility::Collapsed);
    ClassFilters->AddChild(MakeText(TEXT("Selected Fall 2026 CSC lecture meetings")));
    InstructorSearch = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("InstructorSearch"));
    InstructorSearch->SetWidgetStyle(Style);
    InstructorSearch->SetForegroundColor(FLinearColor(PFTPalette::Ink));
    InstructorSearch->SetHintText(FText::FromString(TEXT("Instructor name")));
    InstructorSearch->OnTextChanged.AddDynamic(this, &UPFTNavigatorWidget::SearchChanged);
    ClassFilters->AddChild(InstructorSearch);
    auto* MeetingFilters = WidgetTree->ConstructWidget<UHorizontalBox>();
    ClassFilters->AddChild(MeetingFilters);
    ClassDay = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("ClassDay"));
    ClassDay->AddOption(TEXT("Any day"));
    for (const TCHAR* Day : {TEXT("Monday"), TEXT("Tuesday"), TEXT("Wednesday"), TEXT("Thursday"),
        TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday")})
    {
        ClassDay->AddOption(Day);
    }
    ClassDay->SetSelectedIndex(0);
    ClassDay->OnSelectionChanged.AddDynamic(this, &UPFTNavigatorWidget::FloorChanged);
    MeetingFilters->AddChildToHorizontalBox(ClassDay)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ClassStart = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("ClassStart"));
    ClassStart->AddOption(TEXT("Any start time"));
    TMap<FString, FString> TimeLabels;
    for (const auto& Course : Directory->Courses)
    {
        if (!Course.StartTime.IsEmpty()) TimeLabels.Add(Course.StartTime, Course.GetStartTimeLabel());
    }
    TimeLabels.GetKeys(ClassStartTimes);
    ClassStartTimes.Sort();
    for (const FString& Time : ClassStartTimes) ClassStart->AddOption(TimeLabels[Time]);
    ClassStart->SetSelectedIndex(0);
    ClassStart->OnSelectionChanged.AddDynamic(this, &UPFTNavigatorWidget::FloorChanged);
    MeetingFilters->AddChildToHorizontalBox(ClassStart)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    PreviewOnly = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("PreviewOnlyFilter"));
    PreviewOnly->SetContent(MakeText(TEXT("Previewable rooms only")));
    PreviewOnly->OnCheckStateChanged.AddDynamic(this, &UPFTNavigatorWidget::PreviewFilterChanged);
    Column->AddChild(PreviewOnly);
    auto* ListSize = WidgetTree->ConstructWidget<USizeBox>();
    ListSize->SetMaxDesiredHeight(180);
    Results = WidgetTree->ConstructWidget<UScrollBox>();
    ListSize->SetContent(Results);
    Column->AddChild(ListSize);
    Status = AddText(TEXT("Select a result to mark its room."));
    auto AddButton = [&](UHorizontalBox* Row, const FString& Text, UTextBlock*& Label)
    {
        auto* Button = WidgetTree->ConstructWidget<UButton>();
        Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text));
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        // Control buttons are LSU purple with white text so they stand out from the results
        Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        Button->SetBackgroundColor(FLinearColor(PFTPalette::Purple));
        Button->SetContent(Label);
        auto* Slot = Row->AddChildToHorizontalBox(Button);
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Slot->SetPadding(FMargin(0, 0, 4, 0));
        return Button;
    };
    UTextBlock* Label;
    auto* ResetFiltersRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    ClassFilters->AddChild(ResetFiltersRow);
    auto* ResetFilters = AddButton(ResetFiltersRow, TEXT("Reset class search"), Label);
    ResetFilters->Rename(TEXT("ResetClassSearchButton"));
    ResetFilters->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ResetClassSearch);
    auto* MoreRoomsRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChild(MoreRoomsRow);
    MoreRoomsButton = AddButton(MoreRoomsRow, TEXT("Show more rooms"), Label);
    MoreRoomsButton->Rename(TEXT("MoreRoomsButton"));
    MoreRoomsButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ShowMoreRooms);
    auto* RoomCard = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SelectedRoomCard"));
    Column->AddChild(RoomCard);
    Details = MakeText(TEXT("Select a room to see its details."));
    Details->Rename(TEXT("RoomDetails"));
    RoomCard->AddChild(Details);
    auto* RoomActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    RoomCard->AddChild(RoomActions);
    ViewButton = AddButton(RoomActions, TEXT("Preview room"), Label);
    ViewButton->Rename(TEXT("ViewRoomButton"));
    ViewButton->SetIsEnabled(false);
    ViewButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ViewRoom);
    RouteButton = AddButton(RoomActions, TEXT("Walk there"), Label);
    RouteButton->Rename(TEXT("RouteButton"));
    RouteButton->SetVisibility(ESlateVisibility::Collapsed);
    RouteButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::StartRoute);
    ClearButton = AddButton(RoomActions, TEXT("Clear"), Label);
    ClearButton->Rename(TEXT("ClearRoomButton"));
    ClearButton->SetIsEnabled(false);
    ClearButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ClearRoom);
    RouteUnavailable = MakeText(TEXT("Walking directions unavailable"));
    RouteUnavailable->Rename(TEXT("RouteUnavailable"));
    RouteUnavailable->SetVisibility(ESlateVisibility::Collapsed);
    RoomCard->AddChild(RouteUnavailable);
    // Only shows up when the pick came from a class result, since that's the only time we know the course
    auto* SaveClassRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    RoomCard->AddChild(SaveClassRow);
    SaveClassButton = AddButton(SaveClassRow, TEXT("Add to my classes"), Label);
    SaveClassButton->Rename(TEXT("SaveClassButton"));
    SaveClassButton->SetVisibility(ESlateVisibility::Collapsed);
    SaveClassButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleSavedClass);
    SaveClassLabel = Label;
    auto* ExploreActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChild(ExploreActions);
    auto* CameraButton = AddButton(ExploreActions, TEXT("Free camera"), Label);
    CameraButton->Rename(TEXT("CameraModeButton"));
    CameraButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleFreeCamera);
    CameraLabel = Label;
    auto* SettingsButton = AddButton(ExploreActions, TEXT("Settings"), Label);
    SettingsButton->Rename(TEXT("RouteSettingsButton"));
    SettingsButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleSettings);
    CameraHelp = AddText(TEXT("Free camera lets you fly around the building."));
    CameraHelp->Rename(TEXT("CameraHelp"));

    // My classes goes at the bottom so opening it doesn't push the search controls around
    MyClassesToggle = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("MyClassesToggle"));
    MyClassesToggle->SetContent(MakeText(TEXT("My classes")));
    MyClassesToggle->OnCheckStateChanged.AddDynamic(this, &UPFTNavigatorWidget::MyClassesChanged);
    Column->AddChild(MyClassesToggle);
    MyClassesBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MyClasses"));
    MyClassesBox->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChild(MyClassesBox);
    MyClassesStatus = MakeText(FString());
    MyClassesStatus->Rename(TEXT("MyClassesStatus"));
    MyClassesBox->AddChild(MyClassesStatus);
    MyClassesList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MyClassesList"));
    MyClassesBox->AddChild(MyClassesList);
    // Saved as course Names in the same settings file as the route options, so the list is still there after a restart
    FString SavedClasses;
    GConfig->GetString(SettingsSection, TEXT("MyClasses"), SavedClasses, GGameUserSettingsIni);
    SavedClasses.ParseIntoArray(MyClasses, TEXT(","));
    RefreshMyClasses();

    Settings = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteSettings"));
    Browser->AddChild(Settings);
    Settings->SetVisibility(ESlateVisibility::Collapsed);
    auto AddSetting = [&](const FName Name, const TCHAR* Text)
    {
        auto* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), Name);
        bool bEnabled = false;
        GConfig->GetBool(SettingsSection, *Name.ToString(), bEnabled, GGameUserSettingsIni);
        Check->SetIsChecked(bEnabled);
        Check->SetContent(MakeText(Text));
        Settings->AddChild(Check);
        return Check;
    };
    auto* Motion = AddSetting(TEXT("StationaryArrows"), TEXT("Stationary route arrows"));
    auto* LargerText = AddSetting(TEXT("LargerDirections"), TEXT("Larger directions text"));
    auto* Performance = AddSetting(TEXT("PerformanceMode"), TEXT("Performance mode"));
    Motion->OnCheckStateChanged.AddDynamic(this, &UPFTNavigatorWidget::MotionChanged);
    LargerText->OnCheckStateChanged.AddDynamic(this, &UPFTNavigatorWidget::DirectionsSizeChanged);
    Performance->OnCheckStateChanged.AddDynamic(this, &UPFTNavigatorWidget::PerformanceChanged);

    Directions = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DirectionsHud"));
    Content->AddChild(Directions);
    Directions->SetVisibility(ESlateVisibility::Collapsed);
    RouteHint = MakeText(FString());
    RouteHint->Rename(TEXT("RouteHint"));
    Directions->AddChildToVerticalBox(RouteHint)->SetPadding(FMargin(0, 0, 0, 8));
    auto* GuidanceActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Directions->AddChild(GuidanceActions);
    StopRouteButton = AddButton(GuidanceActions, TEXT("Stop route"), Label);
    StopRouteButton->Rename(TEXT("StopRouteButton"));
    StopRouteButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::StopRoute);
    RetryRouteButton = AddButton(GuidanceActions, TEXT("Retry route"), Label);
    RetryRouteButton->Rename(TEXT("RetryRouteButton"));
    RetryRouteButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::StartRoute);
    FindAnotherRoomButton = AddButton(GuidanceActions, TEXT("Find another room"), Label);
    FindAnotherRoomButton->Rename(TEXT("FindAnotherRoomButton"));
    FindAnotherRoomButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::FindAnotherRoom);
    GetWorld()->GetSubsystem<UPFTRoutes>()->SetReducedMotion(Motion->IsChecked());
    RouteHint->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), LargerText->IsChecked() ? 24 : 18));
    if (const auto* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
    {
        StandardRenderScale = ScreenPercentage->GetFloat();
    }
    if (Performance->IsChecked()) ApplyPerformancePreset(true, StandardRenderScale);
    for (auto* Child : Column->GetAllChildren())
        Cast<UVerticalBoxSlot>(Child->Slot)->SetPadding(FMargin(0, 0, 0, 6));
    RefreshResults();
    return Super::RebuildWidget();
}

void UPFTNavigatorWidget::SearchChanged(const FText& Text)
{
    RefreshResults();
}

void UPFTNavigatorWidget::FloorChanged(FString Floor, ESelectInfo::Type SelectionType)
{
    RefreshResults();
}

void UPFTNavigatorWidget::PreviewFilterChanged(bool bChecked)
{
    RefreshResults();
}

void UPFTNavigatorWidget::ClassFilterChanged(bool bChecked)
{
    ClassFilters->SetVisibility(bChecked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Search->SetHintText(FText::FromString(bChecked ? TEXT("Optional course code or title") : TEXT("Room, course, or department")));
    RefreshResults();
}

void UPFTNavigatorWidget::ResetClassSearch()
{
    if (!ClassesOnly->IsChecked()) return;
    ClearRoom();
    {
        // Rebuild the results once after all controls have been reset.
        TGuardValue<bool> ResetInProgress(bResettingClassSearch, true);
        Search->SetText(FText::GetEmpty());
        InstructorSearch->SetText(FText::GetEmpty());
        ClassDay->SetSelectedIndex(0);
        ClassStart->SetSelectedIndex(0);
        Floors->SetSelectedIndex(0);
        PreviewOnly->SetIsChecked(false);
    }
    RefreshResults();
    InstructorSearch->SetKeyboardFocus();
}

void UPFTNavigatorWidget::RefreshResults()
{
    if (bResettingClassSearch) return;
    Results->ClearChildren();
    Results->ScrollToStart();
    AddResultPage();
}

void UPFTNavigatorWidget::ShowMoreRooms()
{
    const int32 FirstNewResult = Results->GetChildrenCount();
    AddResultPage();
    if (UWidget* FirstNewRoom = Results->GetChildAt(FirstNewResult))
    {
        Results->ScrollWidgetIntoView(FirstNewRoom, false, EDescendantScrollDestination::TopOrLeft);
    }
}

void UPFTNavigatorWidget::AddResultPage()
{
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    const auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const FString Query = Search->GetText().ToString();
    const int32 Floor = FCString::Atoi(*Floors->GetSelectedOption());
    const bool bClassSearch = ClassesOnly->IsChecked();
    const int32 DayIndex = ClassDay->GetSelectedIndex() - 1;
    const int32 TimeIndex = ClassStart->GetSelectedIndex() - 1;
    const TCHAR Day = DayIndex >= 0 && DayIndex < 7 ? ClassDayCodes[DayIndex] : 0;
    const FString Start = ClassStartTimes.IsValidIndex(TimeIndex) ? ClassStartTimes[TimeIndex] : FString();
    TArray<FPFTSearchResult> Entries;
    if (bClassSearch)
    {
        Entries = Directory->FindClasses(Query, InstructorSearch->GetText().ToString(), Day, Start, Floor);
    }
    else Entries = Directory->BrowseRooms(Query, Floor);
    const bool bPreviewOnly = PreviewOnly->IsChecked();
    if (bPreviewOnly)
    {
        Entries.RemoveAll([Destinations](const FPFTSearchResult& Entry)
        {
            return !Destinations->HasLocation(Entry.RoomId);
        });
    }
    const int32 FirstResult = Results->GetChildrenCount();
    const int32 VisibleCount = FMath::Min(FirstResult + RoomsPerPage, Entries.Num());
    FString Message = FString::Printf(TEXT("%d results."), Entries.Num());
    if (VisibleCount < Entries.Num())
    {
        Message = FString::Printf(TEXT("Showing %d of %d results."), VisibleCount, Entries.Num());
    }
    if (Entries.IsEmpty())
    {
        Message = bPreviewOnly ? TEXT("No previews match. Uncheck the filter to see room details.")
            : TEXT("No matches. Try another floor or search.");
        if (bClassSearch && !bPreviewOnly)
            Message = TEXT("No listed meetings match. Reset class search to see all included meetings.");
    }
    for (int32 Index = FirstResult; Index < VisibleCount; ++Index)
    {
        auto* Button = WidgetTree->ConstructWidget<UPFTSearchButton>();
        Button->RoomId = Entries[Index].RoomId;
        Button->CourseId = Entries[Index].CourseId;
        Button->Panel = this;
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        const auto* Room = Directory->FindRoom(Entries[Index].RoomId);
        const bool bHasPreview = bPreviewOnly || Destinations->HasLocation(Button->RoomId);
        const TCHAR* Availability = bHasPreview ? TEXT("Preview") : TEXT("Details only");
        Button->ResultText = FString::Printf(TEXT("%s | Floor %d | %s"), *Entries[Index].Label, Room->Floor, Availability);
        if (Entries.Num() == 1 && !bClassSearch)
        {
            Message = bHasPreview ? TEXT("Press Enter in search to preview the room.")
                : TEXT("Press Enter in search to see room details.");
        }
        Button->Label = Label;
        Label->SetText(FText::FromString(Button->ResultText));
        Label->SetAutoWrapText(true);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
        Button->SetContent(Label);
        Button->OnClicked.AddDynamic(Button, &UPFTSearchButton::SelectRoom);
        Results->AddChild(Button);
    }
    UpdateSelectedTags();
    if (!Directory->Error.IsEmpty()) Message = Directory->Error;
    Status->SetText(FText::FromString(Message));
    MoreRoomsButton->GetParent()->SetVisibility(VisibleCount < Entries.Num() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UPFTNavigatorWidget::ChooseRoom(const FString& RoomId, const FString& CourseId)
{
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    const auto* Room = Directory->FindRoom(RoomId);
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const bool bHasLocation = Room && Destinations->HasLocation(RoomId);
    const bool bSwitchPreview = bHasLocation && Destinations->IsRoomPreviewActive();
    if (!bSwitchPreview)
    {
        ClearRoom();
        if (Destinations->IsRoomPreviewActive()) return;
    }
    if (!Room)
    {
        Status->SetText(FText::FromString(TEXT("This room is no longer in the directory.")));
        return;
    }
    SelectedRoomId = RoomId;
    SelectedCourseId = CourseId;
    UpdateSelectedTags();
    UpdateSaveClassButton();
    const bool bSelected = bHasLocation && Destinations->SelectRoom(RoomId);
    if (bSwitchPreview && !bSelected) ToggleFreeCamera();
    Details->SetText(FText::FromString(Directory->GetRoomDetails(RoomId)));
    ViewButton->SetIsEnabled(bSelected);
    ClearButton->SetIsEnabled(true);
    FString Message = TEXT("Room preview is unavailable on this map.");
    if (bHasLocation) Message = bSelected ? TEXT("Room selected.") : Destinations->Error;
    Status->SetText(FText::FromString(Message));
    UpdateRouteControls();
    if (bSwitchPreview && bSelected) ViewRoom();
}

void UPFTNavigatorWidget::ClearRoom()
{
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    if (Destinations->IsRoomPreviewActive())
    {
        ToggleFreeCamera();
        if (Destinations->IsRoomPreviewActive()) return;
    }
    Destinations->ClearSelection();
    GetWorld()->GetSubsystem<UPFTRoutes>()->Stop();
    SelectedRoomId.Empty();
    SelectedCourseId.Empty();
    UpdateSelectedTags();
    UpdateSaveClassButton();
    Details->SetText(FText::FromString(TEXT("Select a room to see its details.")));
    ViewButton->SetIsEnabled(false);
    ClearButton->SetIsEnabled(false);
    Status->SetText(FText::FromString(TEXT("Room selection cleared.")));
    UpdateRouteControls();
}

// Puts a gold "Selected" tag on every result that points at the selected room.
// It runs after the list is rebuilt and whenever the selection changes, so the tag
// stays on the right room when you search or switch floors.
// No params. Result: selected results start with "Selected | " and have a gold background,
// everything else goes back to normal.
void UPFTNavigatorWidget::UpdateSelectedTags()
{
    for (auto* Child : Results->GetAllChildren())
    {
        auto* Button = Cast<UPFTSearchButton>(Child);
        if (!Button || !Button->Label) continue;
        const bool bSelected = !SelectedRoomId.IsEmpty() && Button->RoomId == SelectedRoomId;
        const FString Text = bSelected ? TEXT("Selected | ") + Button->ResultText : Button->ResultText;
        Button->Label->SetText(FText::FromString(Text));
        Button->SetBackgroundColor(bSelected ? FLinearColor(PFTPalette::Gold) : FLinearColor::White);
    }
}

void UPFTNavigatorWidget::ToggleFreeCamera()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    const bool bWasExploring = IsValid(Exploration->ExplorePawn);
    const bool bSuccess = bWasExploring ? Exploration->EndExplore() : Exploration->BeginExplore();
    if (!bSuccess) Status->SetText(FText::FromString(Exploration->Error));
    else if (bWasExploring) Status->SetText(FText::FromString(TEXT("Returned to the player.")));
}

void UPFTNavigatorWidget::UpdateCameraControls(bool bFreeCamera, bool bCursorShown, bool bRoomPreview)
{
    CameraLabel->SetText(FText::FromString(bFreeCamera ? TEXT("Return to player") : TEXT("Free camera")));
    const TCHAR* Help = TEXT("Free camera lets you fly around the building.");
    if (bFreeCamera)
    {
        Help = TEXT("WASD move | Q down | E up\nMouse to look | Tab opens the cursor");
        if (bCursorShown) Help = TEXT("Camera paused. Press Tab to fly again.");
        if (bRoomPreview) Help = TEXT("Room preview. Press Tab to fly from here.");
    }
    CameraHelp->SetText(FText::FromString(Help));
}

void UPFTNavigatorWidget::StartRoute()
{
    auto* Routes = GetWorld()->GetSubsystem<UPFTRoutes>();

    const auto* Room = GetGameInstance()->GetSubsystem<UPFTDirectory>()->FindRoom(SelectedRoomId);
    if (!Room || !Routes->HasDestination(SelectedRoomId)) return;
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (IsValid(Exploration->ExplorePawn) && !Exploration->EndExplore()) return;
    auto* Player = GetOwningPlayer();
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!Character) return;

    if (Routes->Start(*Character, SelectedRoomId, Room->DisplayName) && Routes->IsGuiding())
    {
        Character->EnableInput(Player);
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Player->bShowMouseCursor = true;
        Player->SetInputMode(Mode);
        UWidgetBlueprintLibrary::SetFocusToGameViewport();
    }
    UpdateRouteControls();
}

void UPFTNavigatorWidget::StopRoute()
{
    GetWorld()->GetSubsystem<UPFTRoutes>()->Stop();
    UpdateRouteControls();
}

void UPFTNavigatorWidget::FindAnotherRoom()
{
    ClearRoom();
    Search->SetText(FText::GetEmpty());
    Search->SetKeyboardFocus();
}

void UPFTNavigatorWidget::SetCompactDirections(bool bCompact)
{
    if (bCompactDirections == bCompact) return;
    bCompactDirections = bCompact;
    Browser->SetVisibility(bCompact ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    Directions->SetVisibility(bCompact ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    SetDesiredSizeInViewport(FVector2D(400, bCompact ? 160 : 575));
}

void UPFTNavigatorWidget::UpdateRouteControls()
{
    if (!RouteButton) return;
    const auto* Routes = GetWorld()->GetSubsystem<UPFTRoutes>();
    const bool bAvailable = Routes->HasDestination(SelectedRoomId);
    RouteButton->SetVisibility(bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RouteUnavailable->SetVisibility(!bAvailable && !SelectedRoomId.IsEmpty() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    SetCompactDirections(Routes->IsGuiding() || Routes->CanRetry() || Routes->HasArrived());
    StopRouteButton->SetVisibility(Routes->IsGuiding() || Routes->CanRetry() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RetryRouteButton->SetVisibility(Routes->CanRetry() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    FindAnotherRoomButton->SetVisibility(Routes->HasArrived() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    const FText& Message = Routes->GetMessage();
    if (!RouteHint->GetText().EqualTo(Message)) RouteHint->SetText(Message);
}

void UPFTNavigatorWidget::ToggleSettings()
{
    const bool bWasVisible = Settings->GetVisibility() == ESlateVisibility::Visible;
    Settings->SetVisibility(bWasVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    Browser->ScrollWidgetIntoView(bWasVisible ? nullptr : Settings.Get(), false, EDescendantScrollDestination::BottomOrRight);
}

void UPFTNavigatorWidget::MotionChanged(bool bChecked)
{
    GetWorld()->GetSubsystem<UPFTRoutes>()->SetReducedMotion(bChecked);
    SavePreference(TEXT("StationaryArrows"), bChecked);
}

void UPFTNavigatorWidget::DirectionsSizeChanged(bool bChecked)
{
    RouteHint->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bChecked ? 24 : 18));
    SavePreference(TEXT("LargerDirections"), bChecked);
}

void UPFTNavigatorWidget::PerformanceChanged(bool bChecked)
{
    ApplyPerformancePreset(bChecked, StandardRenderScale);
    SavePreference(TEXT("PerformanceMode"), bChecked);
}

void UPFTNavigatorWidget::MyClassesChanged(bool bChecked)
{
    MyClassesBox->SetVisibility(bChecked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RefreshMyClasses();
    if (bChecked) Browser->ScrollWidgetIntoView(MyClassesBox.Get(), false, EDescendantScrollDestination::BottomOrRight);
}

// Adds the picked class to My classes, or takes it off if it's already there, and saves the list right away
void UPFTNavigatorWidget::ToggleSavedClass()
{
    if (SelectedCourseId.IsEmpty()) return;
    if (MyClasses.Contains(SelectedCourseId)) MyClasses.Remove(SelectedCourseId);
    else MyClasses.Add(SelectedCourseId);
    GConfig->SetString(SettingsSection, TEXT("MyClasses"), *FString::Join(MyClasses, TEXT(",")), GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
    UpdateSaveClassButton();
    RefreshMyClasses();
}

void UPFTNavigatorWidget::UpdateSaveClassButton()
{
    if (!SaveClassButton) return;
    SaveClassButton->SetVisibility(SelectedCourseId.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    const bool bSaved = MyClasses.Contains(SelectedCourseId);
    SaveClassLabel->SetText(FText::FromString(bSaved ? TEXT("Remove from my classes") : TEXT("Add to my classes")));
}

FDateTime UPFTNavigatorWidget::GetNow() const
{
    return TestClock.GetTicks() > 0 ? TestClock : FDateTime::Now();
}

void UPFTNavigatorWidget::SetClockForTests(const FDateTime& Time)
{
    TestClock = Time;
    RefreshMyClasses();
}

void UPFTNavigatorWidget::UpdateMyClassesClock()
{
    const FDateTime Now = GetNow();
    if (Now.GetHour() * 60 + Now.GetMinute() != LastClockMinute) RefreshMyClasses();
}

// Rebuilds the My classes list for today. The class going on now (or coming up next) gets a tag in front,
// and the ones that are already over say Done, so you can tell where you are in the day at a glance
void UPFTNavigatorWidget::RefreshMyClasses()
{
    if (!MyClassesList) return;
    const FDateTime Now = GetNow();
    LastClockMinute = Now.GetHour() * 60 + Now.GetMinute();
    const FString Time = Now.ToString(TEXT("%H:%M"));
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    const auto Today = Directory->GetClassesForDay(MyClasses, UPFTDirectory::GetDayCode(Now));
    const int32 Next = UPFTDirectory::FindCurrentOrNextClass(Today, Time);

    FString Message = TEXT("No more classes today.");
    if (MyClasses.IsEmpty()) Message = TEXT("No saved classes yet. Find a class by instructor or time, pick it, then choose Add to my classes.");
    else if (Today.IsEmpty()) Message = TEXT("None of your classes meet today.");
    else if (Next != INDEX_NONE)
    {
        const auto* Course = Today[Next];
        const auto* Room = Directory->FindRoom(Course->RoomId);
        const FString Where = Room ? Room->DisplayName : Course->RoomId;
        if (Course->StartTime <= Time && !Course->EndTime.IsEmpty())
            Message = FString::Printf(TEXT("In class now: %s in %s until %s"), *Course->CourseCode, *Where, *Course->GetEndTimeLabel());
        else Message = FString::Printf(TEXT("Next class: %s at %s in %s"), *Course->CourseCode, *Course->GetStartTimeLabel(), *Where);
    }
    MyClassesStatus->SetText(FText::FromString(Message));

    MyClassesList->ClearChildren();
    for (int32 Index = 0; Index < Today.Num(); ++Index)
    {
        const auto* Course = Today[Index];
        const auto* Room = Directory->FindRoom(Course->RoomId);
        const TCHAR* Tag = TEXT("");
        if (Index == Next) Tag = Course->StartTime <= Time ? TEXT("Now | ") : TEXT("Next | ");
        else if (Next == INDEX_NONE || Index < Next) Tag = TEXT("Done | ");
        auto* Button = WidgetTree->ConstructWidget<UPFTSearchButton>();
        Button->RoomId = Course->RoomId;
        Button->CourseId = Course->Name;
        Button->Panel = this;
        Button->ResultText = FString::Printf(TEXT("%s%s · %s\n%s"), Tag, *Course->CourseCode,
            Room ? *Room->DisplayName : *Course->RoomId, *Course->GetTimeRangeLabel());
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Button->ResultText));
        Label->SetAutoWrapText(true);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
        Button->Label = Label;
        Button->SetContent(Label);
        // The next class is gold like a selected result so it stands out from the rest of the day
        Button->SetBackgroundColor(Index == Next ? FLinearColor(PFTPalette::Gold) : FLinearColor::White);
        Button->OnClicked.AddDynamic(Button, &UPFTSearchButton::SelectRoom);
        MyClassesList->AddChild(Button);
    }
}

void UPFTNavigatorWidget::ViewRoom()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (!Exploration->ViewRoom()) Status->SetText(FText::FromString(Exploration->Error));
}

FReply UPFTNavigatorWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Enter && (Search->HasKeyboardFocus() || Search->HasFocusedDescendants()))
    {
        if (Event.IsRepeat() || Results->GetChildrenCount() != 1) return FReply::Handled();
        const auto* Only = CastChecked<UPFTSearchButton>(Results->GetChildAt(0));
        const FString RoomId = Only->RoomId;
        ChooseRoom(RoomId, Only->CourseId);
        const auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
        if (SelectedRoomId == RoomId && ViewButton->GetIsEnabled() && !Destinations->IsRoomPreviewActive()) ViewRoom();
        return FReply::Handled();
    }
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (Event.GetKey() == EKeys::Tab && Exploration->ExplorePawn)
    {
        if (!Event.IsRepeat()) Exploration->SetCursor(!Exploration->bCursorShown);
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
