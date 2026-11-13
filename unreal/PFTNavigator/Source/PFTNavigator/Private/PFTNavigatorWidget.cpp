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
    auto* ExploreButton = AddButton(ExploreActions, TEXT("Explore"), Label);
    ExploreButton->Rename(TEXT("ExploreButton"));
    ExploreButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleExplore);
    ExploreLabel = Label;
    AddButton(ExploreActions, TEXT("Fly"), Label)->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::Fly);
    Details = AddText(TEXT("Select a room to see its details."));
    Details->Rename(TEXT("RoomDetails"));
    AddText(TEXT("WASD move | Q down | E up\nMouse look | Tab switches mouse"));
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
    const auto Entries = Directory->BrowseRooms(Search->GetText().ToString(), FCString::Atoi(*Floors->GetSelectedOption()));
    for (int32 Index = 0; Index < FMath::Min(Entries.Num(), 50); ++Index)
    {
        auto* Button = WidgetTree->ConstructWidget<UPFTSearchButton>();
        Button->RoomId = Entries[Index].RoomId;
        Button->Panel = this;
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        const auto* Room = Directory->FindRoom(Entries[Index].RoomId);
        Button->ResultText = FString::Printf(TEXT("%s | Floor %d"), *Entries[Index].Label, Room->Floor);
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
    FString Message = Entries.IsEmpty() ? TEXT("No matches on this floor. Try another floor or search.") : FString::Printf(TEXT("%d results (up to 50 shown)."), Entries.Num());
    if (!Directory->Error.IsEmpty()) Message = Directory->Error;
    Status->SetText(FText::FromString(Message));
}

void UPFTNavigatorWidget::ChooseRoom(const FString& RoomId)
{
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    const auto* Room = Directory->FindRoom(RoomId);
    ClearRoom();
    if (!Room)
    {
        Status->SetText(FText::FromString(TEXT("This room is no longer in the directory.")));
        return;
    }
    SelectedRoomId = RoomId;
    UpdateSelectedTags();
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const bool bHasLocation = Destinations->HasLocation(RoomId);
    const bool bSelected = bHasLocation && Destinations->SelectRoom(RoomId);
    Details->SetText(FText::FromString(Directory->GetRoomDetails(RoomId)));
    ViewButton->SetIsEnabled(bSelected);
    ClearButton->SetIsEnabled(true);
    FString Message = TEXT("Map location not available yet.");
    if (bHasLocation) Message = bSelected ? TEXT("Room selected.") : Destinations->Error;
    Status->SetText(FText::FromString(Message));
}

void UPFTNavigatorWidget::ClearRoom()
{
    GetWorld()->GetSubsystem<UPFTDestinations>()->ClearSelection();
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

void UPFTNavigatorWidget::ToggleExplore()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    const bool bWasExploring = IsValid(Exploration->ExplorePawn);
    const bool bSuccess = bWasExploring ? Exploration->EndExplore() : Exploration->BeginExplore();
    ExploreLabel->SetText(FText::FromString(Exploration->ExplorePawn ? TEXT("Return") : TEXT("Explore")));
    const FString Message = bWasExploring ? TEXT("Returned to the original player.") : TEXT("WASD/QE move. Tab opens the mouse.");
    Status->SetText(FText::FromString(bSuccess ? Message : Exploration->Error));
}

void UPFTNavigatorWidget::Fly()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (!Exploration->BeginExplore()) Status->SetText(FText::FromString(Exploration->Error));
    else
    {
        Exploration->SetCursor(false);
        ExploreLabel->SetText(FText::FromString(TEXT("Return")));
        Status->SetText(FText::FromString(TEXT("WASD/QE move. Tab opens the mouse.")));
    }
}

void UPFTNavigatorWidget::ViewRoom()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (!Exploration->ViewRoom()) Status->SetText(FText::FromString(Exploration->Error));
    else
    {
        ExploreLabel->SetText(FText::FromString(TEXT("Return")));
        Status->SetText(FText::FromString(TEXT("Room preview is open.")));
    }
}

FReply UPFTNavigatorWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (Event.GetKey() == EKeys::Tab && Exploration->ExplorePawn)
    {
        Exploration->SetCursor(!Exploration->bCursorShown);
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
