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
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    Border->SetContent(Column);
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
    auto* Search = WidgetTree->ConstructWidget<UEditableTextBox>();
    auto Style = Search->GetWidgetStyle();
    Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16));
    Style.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor::Black));
    Search->SetWidgetStyle(Style);
    Search->SetForegroundColor(FLinearColor::Black);
    Search->SetHintText(FText::FromString(TEXT("1200, CSC 3380, or CSE")));
    Search->OnTextChanged.AddDynamic(this, &UPFTNavigatorWidget::SearchChanged);
    Column->AddChild(Search);
    auto* ListSize = WidgetTree->ConstructWidget<USizeBox>();
    ListSize->SetHeightOverride(180);
    Results = WidgetTree->ConstructWidget<UScrollBox>();
    ListSize->SetContent(Results);
    Column->AddChild(ListSize);
    Status = AddText(TEXT("Select a result to mark its room."));
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
    AddButton(TEXT("View selected room"), Label)->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::ViewRoom);
    AddButton(TEXT("Fly (Tab returns to mouse)"), Label)->OnClicked.AddDynamic(this, &UPFTNavigatorWidget::Fly);
    AddText(TEXT("WASD move | Q down | E up\nMouse look | Tab switches mouse"));
    for (auto* Child : Column->GetAllChildren())
        Cast<UVerticalBoxSlot>(Child->Slot)->SetPadding(FMargin(0, 0, 0, 6));
    const auto* Directory = GetGameInstance()->GetSubsystem<UPFTDirectory>();
    if (!Directory->Error.IsEmpty()) Status->SetText(FText::FromString(Directory->Error));
    return Super::RebuildWidget();
}

void UPFTNavigatorWidget::SearchChanged(const FText& Text)
{
    Results->ClearChildren();
    const auto Entries = GetGameInstance()->GetSubsystem<UPFTDirectory>()->Search(Text.ToString());
    // Raised from 25 so a whole department's professors fit (CSE alone has 34 plus the office)
    const int32 MaxShownResults = 50;
    for (int32 Index = 0; Index < FMath::Min(Entries.Num(), MaxShownResults); ++Index)
    {
        auto* Button = WidgetTree->ConstructWidget<UPFTSearchButton>();
        Button->RoomId = Entries[Index].RoomId;
        Button->Panel = this;
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Entries[Index].Label));
        Label->SetAutoWrapText(true);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
        Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
        Button->SetContent(Label);
        Button->OnClicked.AddDynamic(Button, &UPFTSearchButton::SelectRoom);
        Results->AddChild(Button);
    }
    const FString Message = Text.IsEmpty() ? TEXT("Enter a room, course, or department.") : FString::Printf(TEXT("%d matches (up to %d shown)."), Entries.Num(), MaxShownResults);
    Status->SetText(FText::FromString(Message));
}

void UPFTNavigatorWidget::ChooseRoom(const FString& RoomId)
{
    auto* Destinations = GetWorld()->GetSubsystem<UPFTDestinations>();
    const bool bSelected = Destinations->SelectRoom(RoomId);
    Status->SetText(FText::FromString(bSelected ? TEXT("Selected ") + RoomId : Destinations->Error));
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
