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
    if (Panel) Panel->ChooseRoom(RoomId);
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
    AddText(TEXT("Selected Fall 2026 CSC lectures"));
    Search = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomSearch"));
    auto Style = Search->GetWidgetStyle();
    Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16));
    Style.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
    Search->SetWidgetStyle(Style);
    Search->SetForegroundColor(FLinearColor(PFTPalette::Ink));
    Search->SetHintText(FText::FromString(TEXT("1200, CSC 1350, or CSE")));
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

void UPFTNavigatorWidget::RefreshResults()
{
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
    auto Entries = Directory->BrowseRooms(Search->GetText().ToString(), FCString::Atoi(*Floors->GetSelectedOption()));
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
    }
    for (int32 Index = FirstResult; Index < VisibleCount; ++Index)
    {
        auto* Button = WidgetTree->ConstructWidget<UPFTSearchButton>();
        Button->RoomId = Entries[Index].RoomId;
        Button->Panel = this;
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        const auto* Room = Directory->FindRoom(Entries[Index].RoomId);
        const bool bHasPreview = bPreviewOnly || Destinations->HasLocation(Button->RoomId);
        const TCHAR* Availability = bHasPreview ? TEXT("Preview") : TEXT("Details only");
        Button->ResultText = FString::Printf(TEXT("%s | Floor %d | %s"), *Entries[Index].Label, Room->Floor, Availability);
        if (Entries.Num() == 1)
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

void UPFTNavigatorWidget::ChooseRoom(const FString& RoomId)
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
    UpdateSelectedTags();
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
    UpdateSelectedTags();
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
        const FString RoomId = CastChecked<UPFTSearchButton>(Results->GetChildAt(0))->RoomId;
        ChooseRoom(RoomId);
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
