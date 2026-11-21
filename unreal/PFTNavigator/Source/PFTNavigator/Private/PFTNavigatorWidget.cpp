#include "PFTNavigatorWidget.h"
#include "PFTDirectory.h"
#include "PFTDestinations.h"
#include "PFTExploration.h"
#include "PFTExplorePawn.h"
#include "PFTPalette.h"
#include "PFTRouteMap.h"
#include "PFTRoutes.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "InputCoreTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SDPIScaler.h"

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

    void ApplyPerformancePreset(bool bEnabled)
    {
        if (auto* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
        {
            // Remove only our override so the launcher or engine setting is restored.
            static const FName PerformanceTag(TEXT("PFTNavigatorPerformance"));
            ScreenPercentage->Unset(ECVF_SetByTemp, PerformanceTag);
            if (bEnabled)
            {
                const float CurrentScale = ScreenPercentage->GetFloat();
                const float BaseScale = CurrentScale > 0.0f ? CurrentScale : 100.0f;
                ScreenPercentage->Set(FMath::Min(75.0f, BaseScale * 0.8f), ECVF_SetByTemp, PerformanceTag);
            }
        }
    }
}

void UPFTSearchButton::SelectRoom()
{
    if (Panel) Panel->ChooseRoom(RoomId, CourseId);
}

TSharedRef<SWidget> UPFTNavigatorWidget::RebuildWidget()
{
    // Text, buttons, and the map all grow together, so nothing has to be resized one by one
    const auto ScalePanel = [](TSharedRef<SWidget> Panel) -> TSharedRef<SWidget>
    {
        return SNew(SDPIScaler).DPIScale(PanelScale)[Panel];
    };
    if (WidgetTree->RootWidget) return ScalePanel(Super::RebuildWidget());
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
    auto AddRouteViews = [&](UVerticalBox* Parent, float Height, FName StepsName, TObjectPtr<UWidgetSwitcher>& Views,
        TObjectPtr<UTextBlock>& Steps)
    {
        Views = WidgetTree->ConstructWidget<UWidgetSwitcher>();
        Parent->AddChild(Views);
        auto* MapBox = WidgetTree->ConstructWidget<UVerticalBox>();
        Views->AddChild(MapBox);
        auto* MapArea = WidgetTree->ConstructWidget<USizeBox>();
        MapArea->SetHeightOverride(Height);
        auto* Map = WidgetTree->ConstructWidget<UPFTRouteMap>();
        Map->ForceVolatile(true);
        Map->SetClipping(EWidgetClipping::ClipToBounds);
        MapArea->SetContent(Map);
        MapBox->AddChild(MapArea);
        // Each legend symbol is the same color as its marker on the map
        auto* Legend = WidgetTree->ConstructWidget<UHorizontalBox>();
        const auto AddLegend = [&](const TCHAR* Text, const FColor& Color, float Gap, int32 Size = 14)
        {
            auto* Label = MakeText(Text);
            Label->SetAutoWrapText(false);
            Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size));
            Label->SetColorAndOpacity(FSlateColor(FLinearColor(Color)));
            auto* Slot = Legend->AddChildToHorizontalBox(Label);
            Slot->SetPadding(FMargin(Gap, 0, 0, 0));
            Slot->SetVerticalAlignment(VAlign_Center);
        };
        // The symbols are drawn larger than the words because these glyphs are small in the default font
        AddLegend(TEXT("Floor 1"), PFTPalette::Ink, 0);
        AddLegend(TEXT("▲"), PFTPalette::You, 16, 22);
        AddLegend(TEXT("You"), PFTPalette::Ink, 4);
        AddLegend(TEXT("◆"), PFTPalette::Purple, 16, 22);
        AddLegend(TEXT("Destination"), PFTPalette::Ink, 4);
        MapBox->AddChildToVerticalBox(Legend)->SetHorizontalAlignment(HAlign_Center);

        auto* StepsArea = WidgetTree->ConstructWidget<USizeBox>();
        StepsArea->SetHeightOverride(Height + 20);
        auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
        Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
        Steps = MakeText(FString());
        Steps->Rename(*StepsName.ToString());
        Scroll->AddChild(Steps);
        StepsArea->SetContent(Scroll);
        Views->AddChild(StepsArea);
        return Map;
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
    Search->SetHintText(FText::FromString(TEXT("Room or course, like CSC 1350")));
    Search->OnTextChanged.AddDynamic(this, &UPFTNavigatorWidget::SearchChanged);
    Column->AddChild(Search);
    SelectedRoomLabel = AddText(FString());
    SelectedRoomLabel->Rename(TEXT("SelectedRoomLabel"));
    SelectedRoomLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14));
    SelectedRoomLabel->SetVisibility(ESlateVisibility::Collapsed);
    SelectedCourseLabel = AddText(FString());
    SelectedCourseLabel->Rename(TEXT("SelectedCourseLabel"));
    SelectedCourseLabel->SetVisibility(ESlateVisibility::Collapsed);
    auto* RoomActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChild(RoomActions);
    RoomActions->SetVisibility(ESlateVisibility::Collapsed);
    auto* RoutePreviewActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChild(RoutePreviewActions);
    RoutePreviewActions->SetVisibility(ESlateVisibility::Collapsed);
    RoutePreview = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoutePreview"));
    RoutePreview->SetVisibility(ESlateVisibility::Collapsed);
    Column->AddChild(RoutePreview);
    RoutePreviewMessage = MakeText(FString());
    RoutePreviewMessage->Rename(TEXT("RoutePreviewMessage"));
    RoutePreview->AddChildToVerticalBox(RoutePreviewMessage)->SetPadding(FMargin(0, 0, 0, 4));
    RoutePreviewMapBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RoutePreviewMap"));
    RoutePreview->AddChild(RoutePreviewMapBox);
    PreviewMap = AddRouteViews(RoutePreviewMapBox, 230, TEXT("PreviewRouteSteps"), PreviewRouteViews, PreviewSteps);
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
    auto AddRouteViewButtons = [&](UVerticalBox* Parent, const TCHAR* StepsName, const TCHAR* ScaleName,
        TObjectPtr<UTextBlock>& StepsLabel, TObjectPtr<UTextBlock>& ScaleLabel)
    {
        auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
        Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 4, 0, 0));
        UTextBlock* Label;
        auto* Scale = AddButton(Row, TEXT("Full floor"), Label);
        ScaleLabel = Label;
        Scale->Rename(ScaleName);
        CastChecked<UHorizontalBoxSlot>(Scale->Slot)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        Scale->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleRouteMapScale);
        auto* Steps = AddButton(Row, TEXT("Show steps"), Label);
        StepsLabel = Label;
        Steps->Rename(StepsName);
        CastChecked<UHorizontalBoxSlot>(Steps->Slot)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        Steps->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleRouteSteps);
    };
    AddRouteViewButtons(RoutePreviewMapBox, TEXT("PreviewStepsButton"), TEXT("RoutePreviewScaleButton"),
        PreviewStepsLabel, PreviewMapScaleLabel);
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
    RouteButton = AddButton(RoomActions, TEXT("Walk there"), Label);
    RouteButton->SetBackgroundColor(FLinearColor(PFTPalette::Gold));
    Label->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
    RouteButton->Rename(TEXT("RouteButton"));
    RouteButton->SetVisibility(ESlateVisibility::Collapsed);
    RouteButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::StartRoute);
    RoutePreviewButton = AddButton(RoutePreviewActions, TEXT("Preview route"), Label);
    CastChecked<UHorizontalBoxSlot>(RoutePreviewButton->Slot)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    RoutePreviewLabel = Label;
    RoutePreviewButton->Rename(TEXT("RoutePreviewButton"));
    RoutePreviewButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleRoutePreview);
    ViewButton = AddButton(RoomActions, TEXT("Preview room"), Label);
    ViewButton->Rename(TEXT("ViewRoomButton"));
    // Sized to its label so the text is never cut off; Walk there takes the rest of the row
    CastChecked<UHorizontalBoxSlot>(ViewButton->Slot)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    ViewButton->SetIsEnabled(false);
    ViewButton->SetVisibility(ESlateVisibility::Collapsed);
    ViewButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleRoomPreview);
    PreviewLabel = Label;
    ClearButton = AddButton(RoomActions, TEXT("Clear"), Label);
    ClearButton->Rename(TEXT("ClearRoomButton"));
    ClearButton->SetIsEnabled(false);
    CastChecked<UHorizontalBoxSlot>(ClearButton->Slot)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
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
    // The destination is a small purple caption and the time is plain text, so the bold instruction is what you read first
    RouteDestination = MakeText(FString());
    RouteDestination->Rename(TEXT("RouteDestination"));
    RouteDestination->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14));
    RouteDestination->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Purple)));
    RouteDestination->SetVisibility(ESlateVisibility::Collapsed);
    Directions->AddChild(RouteDestination);
    RouteHint = MakeText(FString());
    RouteHint->Rename(TEXT("RouteHint"));
    Directions->AddChildToVerticalBox(RouteHint)->SetPadding(FMargin(0, 0, 0, 4));
    RouteTime = MakeText(FString());
    RouteTime->Rename(TEXT("RouteTime"));
    RouteTime->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16));
    RouteTime->SetVisibility(ESlateVisibility::Collapsed);
    Directions->AddChildToVerticalBox(RouteTime)->SetPadding(FMargin(0, 0, 0, 4));
    auto* GuidanceActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Directions->AddChildToVerticalBox(GuidanceActions)->SetPadding(FMargin(0, 4, 0, 0));
    // The walking buttons get extra padding because they're clicked while moving
    const auto MakeEasyToClick = [](UButton* Button)
    {
        CastChecked<UButtonSlot>(Button->GetContent()->Slot)->SetPadding(FMargin(8, 6));
    };
    StopRouteButton = AddButton(GuidanceActions, TEXT("Stop route"), Label);
    StopRouteButton->Rename(TEXT("StopRouteButton"));
    StopRouteButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::StopRoute);
    MakeEasyToClick(StopRouteButton);
    RouteOverviewButton = AddButton(GuidanceActions, TEXT("Show map"), Label);
    RouteOverviewLabel = Label;
    RouteOverviewButton->Rename(TEXT("RouteOverviewButton"));
    RouteOverviewButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleRouteOverview);
    MakeEasyToClick(RouteOverviewButton);
    RetryRouteButton = AddButton(GuidanceActions, TEXT("Retry route"), Label);
    RetryRouteButton->Rename(TEXT("RetryRouteButton"));
    RetryRouteButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::StartRoute);
    MakeEasyToClick(RetryRouteButton);
    FindAnotherRoomButton = AddButton(GuidanceActions, TEXT("Find another room"), Label);
    FindAnotherRoomButton->Rename(TEXT("FindAnotherRoomButton"));
    FindAnotherRoomButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::FindAnotherRoom);
    MakeEasyToClick(FindAnotherRoomButton);
    RouteOverview = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RouteOverview"));
    RouteOverview->SetVisibility(ESlateVisibility::Collapsed);
    Directions->AddChildToVerticalBox(RouteOverview)->SetPadding(FMargin(0, 8, 0, 0));
    // The walking map is the larger one because it's read at a glance while moving
    RouteMap = AddRouteViews(RouteOverview, 300, TEXT("WalkingRouteSteps"), WalkingRouteViews, WalkingSteps);
    RouteMap->Rename(TEXT("WalkingRouteMap"));
    AddRouteViewButtons(RouteOverview, TEXT("WalkingStepsButton"), TEXT("RouteOverviewScaleButton"),
        WalkingStepsLabel, RouteMapScaleLabel);
    GetWorld()->GetSubsystem<UPFTRoutes>()->SetReducedMotion(Motion->IsChecked());
    SetDirectionsTextSize(LargerText->IsChecked());
    ApplyPerformancePreset(Performance->IsChecked());
    for (auto* Child : Column->GetAllChildren())
        Cast<UVerticalBoxSlot>(Child->Slot)->SetPadding(FMargin(0, 0, 0, 6));
    RefreshResults();
    return ScalePanel(Super::RebuildWidget());
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
    Search->SetHintText(FText::FromString(bChecked ? TEXT("Optional course code or title") : TEXT("Room or course, like CSC 1350")));
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
    const auto* Course = Directory->FindCourse(CourseId);
    if (Course && Course->RoomId != RoomId) Course = nullptr;
    SelectedCourseId = Course ? Course->Name : FString();
    FString Summary;
    if (Course)
    {
        Summary = Course->CourseCode;
        const FString Schedule = Course->GetScheduleLabel();
        if (!Schedule.IsEmpty()) Summary += TEXT("\n") + Schedule;
    }
    SelectedCourseLabel->SetText(FText::FromString(Summary));
    SelectedCourseLabel->SetVisibility(Course ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    UpdateSelectedTags();
    UpdateSaveClassButton();
    const bool bSelected = bHasLocation && Destinations->SelectRoom(RoomId);
    if (bSwitchPreview && !bSelected) ToggleFreeCamera();
    SelectedRoomLabel->SetText(FText::FromString(Room->DisplayName));
    const FString RoomDetails = Directory->GetRoomDetailsForSelection(RoomId, SelectedCourseId);
    Details->SetText(FText::FromString(RoomDetails.RightChop(Room->DisplayName.Len() + 1)));
    ViewButton->SetIsEnabled(bSelected);
    ClearButton->SetIsEnabled(true);
    FString Message = TEXT("Room preview is unavailable on this map.");
    if (bHasLocation) Message = bSelected ? TEXT("Room selected.") : Destinations->Error;
    Status->SetText(FText::FromString(Message));
    UpdateRouteControls();
    if (bSwitchPreview && bSelected) ViewRoom();
    Browser->ScrollToStart();
}

void UPFTNavigatorWidget::ClearRoom()
{
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const bool bWasPreview = Destinations->IsRoomPreviewActive();
    if (Destinations->IsRoomPreviewActive())
    {
        ToggleFreeCamera();
        if (Destinations->IsRoomPreviewActive()) return;
    }
    Destinations->ClearSelection();
    GetWorld()->GetSubsystem<UPFTRoutes>()->Stop();
    SetRoutePreviewVisible(false);
    SelectedRoomId.Empty();
    SelectedCourseId.Empty();
    SelectedCourseLabel->SetText(FText::GetEmpty());
    SelectedCourseLabel->SetVisibility(ESlateVisibility::Collapsed);
    UpdateSelectedTags();
    UpdateSaveClassButton();
    Details->SetText(FText::FromString(TEXT("Select a room to see its details.")));
    ViewButton->SetIsEnabled(false);
    ClearButton->SetIsEnabled(false);
    Status->SetText(FText::FromString(TEXT("Room selection cleared.")));
    UpdateRouteControls();
    if (bWasPreview) Search->SetKeyboardFocus();
}

// Tags the selected room and the exact class when a class is picked.
void UPFTNavigatorWidget::UpdateSelectedTags()
{
    for (auto* Child : Results->GetAllChildren())
    {
        auto* Button = Cast<UPFTSearchButton>(Child);
        if (!Button || !Button->Label) continue;
        const bool bSelected = !SelectedRoomId.IsEmpty() && Button->RoomId == SelectedRoomId
            && (Button->CourseId.IsEmpty() || Button->CourseId == SelectedCourseId);
        const FString Text = bSelected ? TEXT("Selected | ") + Button->ResultText : Button->ResultText;
        Button->Label->SetText(FText::FromString(Text));
        Button->SetBackgroundColor(bSelected ? FLinearColor(PFTPalette::Gold) : FLinearColor::White);
    }
}

void UPFTNavigatorWidget::ToggleFreeCamera()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    const bool bWasExploring = IsValid(Exploration->ExplorePawn);
    const bool bWasPreview = GetWorld()->GetSubsystem<UPFTDestinations>()->IsRoomPreviewActive();
    const bool bSuccess = bWasExploring ? Exploration->EndExplore() : Exploration->BeginExplore();
    if (!bSuccess) Status->SetText(FText::FromString(Exploration->Error));
    else if (bWasExploring)
    {
        Status->SetText(FText::FromString(TEXT("Returned to the player.")));
        if (bWasPreview)
        {
            UWidget* Focus = ViewButton->GetIsEnabled() && ViewButton->IsVisible() ? static_cast<UWidget*>(ViewButton.Get()) : Search.Get();
            Browser->ScrollToStart();
            Focus->SetKeyboardFocus();
        }
    }
}

void UPFTNavigatorWidget::UpdateCameraControls(bool bFreeCamera, bool bCursorShown, bool bRoomPreview)
{
    CameraLabel->SetText(FText::FromString(bFreeCamera ? TEXT("Return to player") : TEXT("Free camera")));
    PreviewLabel->SetText(FText::FromString(bRoomPreview ? TEXT("Exit preview") : TEXT("Preview room")));
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
    Browser->ScrollToStart();
    if (RouteButton->IsVisible()) RouteButton->SetKeyboardFocus();
    else Search->SetKeyboardFocus();
}

void UPFTNavigatorWidget::FindAnotherRoom()
{
    ClearRoom();
    Search->SetText(FText::GetEmpty());
    Search->SetKeyboardFocus();
}

void UPFTNavigatorWidget::ToggleRoutePreview()
{
    if (RoutePreview->IsVisible())
    {
        SetRoutePreviewVisible(false);
        return;
    }
    auto* Player = GetOwningPlayer();
    const auto* Character = Player ? Player->GetCharacter() : nullptr;
    auto* Routes = GetWorld()->GetSubsystem<UPFTRoutes>();
    if (!Character || !Routes->HasDestination(SelectedRoomId)) return;

    RoutePreviewOrigin = Character->GetActorLocation();
    TArray<FVector> Points;
    FString Estimate;
    const bool bHasRoute = Routes->Preview(*Character, SelectedRoomId, Points, Estimate);
    const bool bShowMap = bHasRoute && PreviewMap->Load(*Routes, *Player);
    if (bShowMap)
    {
        PreviewSteps->SetText(Routes->DescribeSteps(RoutePreviewOrigin, Points));
        PreviewMap->SetPreview(RoutePreviewOrigin, MoveTemp(Points));
        PreviewMapScaleLabel->SetText(FText::FromString(TEXT("Full floor")));
    }
    RoutePreviewMapBox->SetVisibility(bShowMap ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    RoutePreviewMessage->SetText(FText::FromString(bHasRoute ? Estimate
        : FString(TEXT("Walk to a first-floor hallway to preview this route."))));
    SetRoutePreviewVisible(true);
}

void UPFTNavigatorWidget::SetRoutePreviewVisible(bool bVisible)
{
    if (bVisible == RoutePreview->IsVisible()) return;
    if (!bVisible) SetRouteStepsVisible(true, false);
    RoutePreview->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    RoutePreviewLabel->SetText(FText::FromString(bVisible ? TEXT("Hide route") : TEXT("Preview route")));
}

void UPFTNavigatorWidget::SetCompactDirections(bool bCompact)
{
    if (bCompactDirections == bCompact) return;
    bCompactDirections = bCompact;
    Browser->SetVisibility(bCompact ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    Directions->SetVisibility(bCompact ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    CompactHeight = 0.0f;
    CompactLayoutFrame = GFrameCounter;
    SetDesiredSizeInViewport(FVector2D(GetPanelWidth(), bCompact ? 180 : 575) * PanelScale);
}

// The walking panel is wider while its map is open so the map is big enough to read
float UPFTNavigatorWidget::GetPanelWidth() const
{
    return bCompactDirections && RouteOverview->IsVisible() ? 520.0f : 400.0f;
}

void UPFTNavigatorWidget::ToggleRouteOverview()
{
    auto* Routes = GetWorld()->GetSubsystem<UPFTRoutes>();
    auto* Player = GetOwningPlayer();
    if (!Routes->IsGuiding() || !Player) return;
    if (RouteOverview->IsVisible()) SetRouteOverviewVisible(false);
    else if (RouteMap->Load(*Routes, *Player))
    {
        RouteMapScaleLabel->SetText(FText::FromString(TEXT("Full floor")));
        SetRouteOverviewVisible(true);
    }
}

void UPFTNavigatorWidget::ToggleRouteMapScale()
{
    const bool bPreview = RoutePreview->IsVisible();
    auto* Map = bPreview ? PreviewMap.Get() : RouteMap.Get();
    auto* Label = bPreview ? PreviewMapScaleLabel.Get() : RouteMapScaleLabel.Get();
    Label->SetText(FText::FromString(Map->ToggleFullFloor() ? TEXT("Focus route") : TEXT("Full floor")));
}

void UPFTNavigatorWidget::ToggleRouteSteps()
{
    const bool bPreview = RoutePreview->IsVisible();
    const auto* Views = bPreview ? PreviewRouteViews.Get() : WalkingRouteViews.Get();
    SetRouteStepsVisible(bPreview, Views->GetActiveWidgetIndex() == 0);
    UpdateRouteControls();
}

void UPFTNavigatorWidget::SetRouteStepsVisible(bool bPreview, bool bVisible)
{
    auto* Views = bPreview ? PreviewRouteViews.Get() : WalkingRouteViews.Get();
    auto* Label = bPreview ? PreviewStepsLabel.Get() : WalkingStepsLabel.Get();
    auto* Scale = bPreview ? PreviewMapScaleLabel.Get() : RouteMapScaleLabel.Get();
    Views->SetActiveWidgetIndex(bVisible ? 1 : 0);
    Label->SetText(FText::FromString(bVisible ? TEXT("Show map") : TEXT("Show steps")));
    Scale->GetParent()->SetVisibility(bVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    if (bVisible)
    {
        auto* Steps = bPreview ? PreviewSteps.Get() : WalkingSteps.Get();
        CastChecked<UScrollBox>(Steps->GetParent())->ScrollToStart();
        if (!bPreview) NextStepsUpdate = 0.0;
    }
}

void UPFTNavigatorWidget::SetRouteOverviewVisible(bool bVisible)
{
    if (bVisible == RouteOverview->IsVisible()) return;
    if (!bVisible) SetRouteStepsVisible(false, false);
    RouteOverview->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    RouteOverviewLabel->SetText(FText::FromString(bVisible ? TEXT("Hide route") : TEXT("Show map")));
    if (bCompactDirections)
    {
        CompactHeight = 0.0f;
        CompactLayoutFrame = GFrameCounter;
        SetDesiredSizeInViewport(FVector2D(GetPanelWidth(), bVisible ? 560 : 180) * PanelScale);
    }
}

void UPFTNavigatorWidget::UpdateRouteControls()
{
    if (!RouteButton) return;
    const auto* Routes = GetWorld()->GetSubsystem<UPFTRoutes>();
    const bool bAvailable = Routes->HasDestination(SelectedRoomId);
    const bool bSelected = !SelectedRoomId.IsEmpty();
    SelectedRoomLabel->SetVisibility(bSelected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RouteButton->GetParent()->SetVisibility(bSelected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RouteButton->SetVisibility(bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    const auto* Player = GetOwningPlayer();
    const auto* Character = Player ? Player->GetCharacter() : nullptr;
    RoutePreviewButton->GetParent()->SetVisibility(bAvailable && Character ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    const bool bCanPreview = ViewButton->GetIsEnabled();
    ViewButton->SetVisibility(bCanPreview ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RouteUnavailable->SetVisibility(!bAvailable && bSelected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!bAvailable && bSelected)
    {
        RouteUnavailable->SetText(FText::FromString(bCanPreview ? TEXT("Walking directions unavailable. You can preview this room.")
            : TEXT("Walking directions and preview unavailable. Room details are below.")));
    }
    const bool bShowDirections = Routes->IsGuiding() || Routes->CanRetry() || Routes->HasArrived();
    if (RoutePreview->IsVisible() && (bShowDirections || !bAvailable || !Character
        || FVector::DistSquared(RoutePreviewOrigin, Character->GetActorLocation()) > FMath::Square(100.0)))
    {
        SetRoutePreviewVisible(false);
    }
    SetCompactDirections(bShowDirections);
    StopRouteButton->SetVisibility(Routes->IsGuiding() || Routes->CanRetry() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    RouteOverviewButton->SetVisibility(Routes->IsGuiding() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!Routes->IsGuiding()) SetRouteOverviewVisible(false);
    RetryRouteButton->SetVisibility(Routes->CanRetry() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    FindAnotherRoomButton->SetVisibility(Routes->HasArrived() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    const FString Message = Routes->GetMessage().ToString();
    if (Message != ShownRouteMessage)
    {
        ShownRouteMessage = Message;
        // While walking the message is "destination / instruction / time left". Other messages are a single line
        TArray<FString> Lines;
        Message.ParseIntoArrayLines(Lines);
        const int32 HintLine = Lines.Num() > 1 ? 1 : 0;
        RouteDestination->SetText(FText::FromString(HintLine ? TEXT("To ") + Lines[0] : FString()));
        RouteDestination->SetVisibility(HintLine ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        RouteHint->SetText(FText::FromString(Lines.IsValidIndex(HintLine) ? Lines[HintLine] : FString()));
        RouteTime->SetText(FText::FromString(Lines.IsValidIndex(2) ? Lines[2] : FString()));
        RouteTime->SetVisibility(Lines.IsValidIndex(2) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    // Wait for a full frame to be drawn after a change, since the size is measured when the panel is drawn
    if (bCompactDirections && GFrameCounter > CompactLayoutFrame + 1)
    {
        // Fit the walking panel to its lines so a wrapped instruction or the map is never cut off
        const float Height = FMath::Min(WidgetTree->RootWidget->GetDesiredSize().Y, 620.0f);
        if (Height > 0 && !FMath::IsNearlyEqual(Height, CompactHeight, 0.5f))
        {
            CompactHeight = Height;
            SetDesiredSizeInViewport(FVector2D(GetPanelWidth(), Height) * PanelScale);
        }
    }
    RefreshWalkingSteps(*Routes);
}

void UPFTNavigatorWidget::RefreshWalkingSteps(const UPFTRoutes& Routes)
{
    if (!RouteOverview->IsVisible() || WalkingRouteViews->GetActiveWidgetIndex() != 1) return;
    const uint32 Revision = Routes.GetPlanRevision();
    if (StepsPlanRevision != Revision)
    {
        CastChecked<UScrollBox>(WalkingSteps->GetParent())->ScrollToStart();
        StepsPlanRevision = Revision;
        NextStepsUpdate = 0.0;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now < NextStepsUpdate) return;
    const FText Steps = Routes.GetRemainingSteps();
    if (!WalkingSteps->GetText().EqualTo(Steps)) WalkingSteps->SetText(Steps);
    NextStepsUpdate = Now + 0.2;
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
    SetDirectionsTextSize(bChecked);
    SavePreference(TEXT("LargerDirections"), bChecked);
}

void UPFTNavigatorWidget::SetDirectionsTextSize(bool bLarge)
{
    RouteHint->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bLarge ? 24 : 18));
    const auto StepsFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), bLarge ? 18 : 14);
    PreviewSteps->SetFont(StepsFont);
    WalkingSteps->SetFont(StepsFont);
}

void UPFTNavigatorWidget::PerformanceChanged(bool bChecked)
{
    ApplyPerformancePreset(bChecked);
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
    const bool bSaved = MyClasses.Contains(SelectedCourseId);
    const auto* Course = GetGameInstance()->GetSubsystem<UPFTDirectory>()->FindCourse(SelectedCourseId);
    const bool bCanSave = Course && !Course->StartTime.IsEmpty();
    SaveClassButton->SetVisibility(bCanSave || bSaved ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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

void UPFTNavigatorWidget::ToggleRoomPreview()
{
    if (GetWorld()->GetSubsystem<UPFTDestinations>()->IsRoomPreviewActive()) ToggleFreeCamera();
    else ViewRoom();
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
