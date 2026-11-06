#include "PFTNavigatorWidget.h"
#include "PFTDirectory.h"
#include "PFTDestinations.h"
#include "PFTExploration.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
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
    Border->SetBrushColor(FLinearColor(0.025f, 0.035f, 0.06f, 0.96f));
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
        Column->AddChild(Label);
        return Label;
    };
    AddText(TEXT("PFT Navigator"))->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 20));
    AddText(TEXT("Sample course data"));
    Search = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomSearch"));
    auto Style = Search->GetWidgetStyle();
    Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16));
    Style.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor::Black));
    Search->SetWidgetStyle(Style);
    Search->SetForegroundColor(FLinearColor::Black);
    Search->SetHintText(FText::FromString(TEXT("1200, CSC 3380, or CSE")));
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
    ListSize->SetHeightOverride(180);
    Results = WidgetTree->ConstructWidget<UScrollBox>();
    ListSize->SetContent(Results);
    Column->AddChild(ListSize);
    Status = AddText(TEXT("Select a result to mark its room."));
    Details = AddText(TEXT("Select a room to see its details."));
    Details->Rename(TEXT("RoomDetails"));
    auto AddButton = [&](const FString& Text, UTextBlock*& Label)
    {
        auto* Button = WidgetTree->ConstructWidget<UButton>();
        Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Text));
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
        Button->SetContent(Label);
        Column->AddChild(Button);
        return Button;
    };
    UTextBlock* Label;
    AddButton(TEXT("Explore"), Label)->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ToggleExplore);
    ExploreLabel = Label;
    ViewButton = AddButton(TEXT("View selected room"), Label);
    ViewButton->Rename(TEXT("ViewRoomButton"));
    ViewButton->SetIsEnabled(false);
    ViewButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ViewRoom);
    ClearButton = AddButton(TEXT("Clear selected room"), Label);
    ClearButton->Rename(TEXT("ClearRoomButton"));
    ClearButton->SetIsEnabled(false);
    ClearButton->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ClearRoom);
    AddButton(TEXT("Fly (Tab returns to mouse)"), Label)->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::Fly);
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
        Label->SetText(FText::FromString(FString::Printf(TEXT("%s | Floor %d"), *Entries[Index].Label, Room->Floor)));
        Label->SetAutoWrapText(true);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
        Button->SetContent(Label);
        Button->OnClicked.AddDynamic(Button, &UPFTSearchButton::SelectRoom);
        Results->AddChild(Button);
    }
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
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const bool bHasLocation = Destinations->HasLocation(RoomId);
    const bool bSelected = bHasLocation && Destinations->SelectRoom(RoomId);
    Details->SetText(FText::FromString(Directory->GetRoomDetails(RoomId)));
    ViewButton->SetIsEnabled(bSelected);
    ClearButton->SetIsEnabled(true);
    FString Message = TEXT("Map location not available yet.");
    if (bHasLocation) Message = bSelected ? TEXT("Room marker shown.") : Destinations->Error;
    Status->SetText(FText::FromString(Message));
}

void UPFTNavigatorWidget::ClearRoom()
{
    GetWorld()->GetSubsystem<UPFTDestinations>()->ClearSelection();
    Details->SetText(FText::FromString(TEXT("Select a room to see its details.")));
    ViewButton->SetIsEnabled(false);
    ClearButton->SetIsEnabled(false);
    Status->SetText(FText::FromString(TEXT("Room selection cleared.")));
}

void UPFTNavigatorWidget::ToggleExplore()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    const bool bSuccess = Exploration->ExplorePawn ? Exploration->EndExplore() : Exploration->BeginExplore();
    ExploreLabel->SetText(FText::FromString(Exploration->ExplorePawn ? TEXT("Return to original player") : TEXT("Explore")));
    Status->SetText(FText::FromString(bSuccess ? TEXT("WASD/QE move. Tab opens the mouse.") : Exploration->Error));
}

void UPFTNavigatorWidget::Fly()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (!Exploration->BeginExplore()) Status->SetText(FText::FromString(Exploration->Error));
    else
    {
        Exploration->SetCursor(false);
        ExploreLabel->SetText(FText::FromString(TEXT("Return to original player")));
    }
}

void UPFTNavigatorWidget::ViewRoom()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    if (!Exploration->ViewRoom()) Status->SetText(FText::FromString(Exploration->Error));
    else ExploreLabel->SetText(FText::FromString(TEXT("Return to original player")));
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
