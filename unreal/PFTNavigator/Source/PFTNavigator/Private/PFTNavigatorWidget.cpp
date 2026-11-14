#include "PFTNavigatorWidget.h"
#include "PFTDirectory.h"
#include "PFTDestinations.h"
#include "PFTExploration.h"
#include "PFTExplorePawn.h"
#include "PFTPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"

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
    auto* PanelScroll = WidgetTree->ConstructWidget<UScrollBox>();
    Border->SetContent(PanelScroll);
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    PanelScroll->AddChild(Column);
    auto AddText = [&](const FString& Text)
    {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text));
        Label->SetAutoWrapText(true);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Ink)));
        Column->AddChild(Label);
        return Label;
    };
    // The heading is purple like the rest of the controls in Claire's design
    auto* Title = AddText(TEXT("PFT Navigator"));
    Title->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20));
    Title->SetColorAndOpacity(FSlateColor(FLinearColor(PFTPalette::Purple)));
    AddText(TEXT("Fall 2026 CSC classes in PFT"));
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
    auto* RoomActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChild(RoomActions);
    UTextBlock* Label;
    ViewButton = AddButton(RoomActions, TEXT("View selected room"), Label);
    ViewButton->Rename(TEXT("ViewRoomButton"));
    ViewButton->SetIsEnabled(false);
    ViewButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ViewRoom);
    ClearButton = AddButton(RoomActions, TEXT("Clear"), Label);
    ClearButton->Rename(TEXT("ClearRoomButton"));
    ClearButton->SetIsEnabled(false);
    ClearButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ClearRoom);
    auto* ExploreActions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Column->AddChild(ExploreActions);
    auto* CameraButton = AddButton(ExploreActions, TEXT("Free camera"), Label);
    CameraButton->Rename(TEXT("CameraModeButton"));
    CameraButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleFreeCamera);
    CameraLabel = Label;
    CameraHelp = AddText(TEXT("Free camera lets you fly around the building."));
    CameraHelp->Rename(TEXT("CameraHelp"));
    Details = AddText(TEXT("Select a room to see its details."));
    Details->Rename(TEXT("RoomDetails"));
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

void UPFTNavigatorWidget::RefreshResults()
{
    Results->ClearChildren();
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    const auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const auto Entries = Directory->BrowseRooms(Search->GetText().ToString(), FCString::Atoi(*Floors->GetSelectedOption()));
    FString Message = FString::Printf(TEXT("%d results."), Entries.Num());
    if (Entries.Num() > 50) Message = FString::Printf(TEXT("Showing 50 of %d results."), Entries.Num());
    if (Entries.IsEmpty()) Message = TEXT("No matches. Try another floor or search.");
    for (int32 Index = 0; Index < FMath::Min(Entries.Num(), 50); ++Index)
    {
        auto* Button = WidgetTree->ConstructWidget<UPFTSearchButton>();
        Button->RoomId = Entries[Index].RoomId;
        Button->Panel = this;
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        const auto* Room = Directory->FindRoom(Entries[Index].RoomId);
        const bool bHasPreview = Destinations->HasLocation(Button->RoomId);
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
}

void UPFTNavigatorWidget::ChooseRoom(const FString& RoomId)
{
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    const auto* Room = Directory->FindRoom(RoomId);
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    ClearRoom();
    if (Destinations->IsRoomPreviewActive()) return;
    if (!Room)
    {
        Status->SetText(FText::FromString(TEXT("This room is no longer in the directory.")));
        return;
    }
    SelectedRoomId = RoomId;
    UpdateSelectedTags();
    const bool bHasLocation = Destinations->HasLocation(RoomId);
    const bool bSelected = bHasLocation && Destinations->SelectRoom(RoomId);
    Details->SetText(FText::FromString(Directory->GetRoomDetails(RoomId)));
    ViewButton->SetIsEnabled(bSelected);
    ClearButton->SetIsEnabled(true);
    FString Message = TEXT("Room preview is unavailable on this map.");
    if (bHasLocation) Message = bSelected ? TEXT("Room selected.") : Destinations->Error;
    Status->SetText(FText::FromString(Message));
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
    SelectedRoomId.Empty();
    UpdateSelectedTags();
    Details->SetText(FText::FromString(TEXT("Select a room to see its details.")));
    ViewButton->SetIsEnabled(false);
    ClearButton->SetIsEnabled(false);
    Status->SetText(FText::FromString(TEXT("Room selection cleared.")));
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
        if (SelectedRoomId == RoomId && ViewButton->GetIsEnabled()) ViewRoom();
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
