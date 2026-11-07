#include "PFTNavigatorWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTSelectedTagTest, "PFTNavigator.Panel.SelectedTag", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Gets every result button the panel is showing right now.
// Param: Panel is the search panel. Result: the result buttons in list order.
static TArray<UPFTSearchButton*> GetResults(UPFTNavigatorWidget* Panel)
{
    TArray<UWidget*> Widgets;
    Panel->WidgetTree->GetAllWidgets(Widgets);
    TArray<UPFTSearchButton*> Buttons;
    for (auto* Item : Widgets)
    {
        if (auto* Button = Cast<UPFTSearchButton>(Item)) Buttons.Add(Button);
    }
    return Buttons;
}

// Gets the room IDs of every result that has the Selected tag on it.
// Param: Panel is the search panel. Result: one room ID per tagged result (can repeat).
static TArray<FString> GetTaggedRooms(UPFTNavigatorWidget* Panel)
{
    TArray<FString> Rooms;
    for (auto* Button : GetResults(Panel))
    {
        if (Button->Label->GetText().ToString().StartsWith(TEXT("Selected | "))) Rooms.Add(Button->RoomId);
    }
    return Rooms;
}

// Types into the search box the same way a person would, so the list gets rebuilt.
// Params: Panel is the search panel, Text is what goes in the box. Result: the results list is refreshed.
static void SetSearch(UPFTNavigatorWidget* Panel, const FString& Text)
{
    auto* Search = Cast<UEditableTextBox>(Panel->WidgetTree->FindWidget(TEXT("RoomSearch")));
    Search->SetText(FText::FromString(Text));
    Search->OnTextChanged.Broadcast(Search->GetText());
}

// Checks that picking a room tags its results, that the tag sticks when the list is
// rebuilt by a new search, and that clearing the room takes the tag off.
bool FPFTSelectedTagTest::RunTest(const FString& Parameters)
{
    auto* Instance = NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone();
    auto* World = Instance->GetWorld();
    World->InitializeActorsForPlay(FURL());
    auto Cleanup = [&]()
    {
        Instance->Shutdown();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Player = World->SpawnActor<APlayerController>();
    auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
    Player->SetPlayer(LocalPlayer);
    auto* Panel = CreateWidget<UPFTNavigatorWidget>(Player, UPFTNavigatorWidget::StaticClass());
    Panel->TakeWidget();
    auto* FloorFilter = Cast<UComboBoxString>(Panel->WidgetTree->FindWidget(TEXT("FloorFilter")));
    auto* ClearButton = Cast<UButton>(Panel->WidgetTree->FindWidget(TEXT("ClearRoomButton")));
    if (!TestNotNull(TEXT("Floor filter exists"), FloorFilter) || !TestNotNull(TEXT("Clear button exists"), ClearButton))
    {
        Cleanup();
        return false;
    }

    // Nothing is picked yet, so nothing should be tagged
    FloorFilter->SetSelectedOption(TEXT("1"));
    TestEqual(TEXT("No tags before picking a room"), GetTaggedRooms(Panel).Num(), 0);

    // Pick PFT 1200 from the first floor list
    UPFTSearchButton* Room1200 = nullptr;
    for (auto* Button : GetResults(Panel))
    {
        if (Button->RoomId == TEXT("PFT_1200")) Room1200 = Button;
    }
    if (!TestNotNull(TEXT("PFT 1200 is in the first floor list"), Room1200))
    {
        Cleanup();
        return false;
    }
    Room1200->OnClicked.Broadcast();
    const TArray<FString> Tagged = GetTaggedRooms(Panel);
    TestEqual(TEXT("Only one result is tagged"), Tagged.Num(), 1);
    TestTrue(TEXT("The tag is on PFT 1200"), Tagged.Contains(TEXT("PFT_1200")));
    TestTrue(TEXT("The tagged result keeps its normal text after the tag"), Room1200->Label->GetText().ToString().EndsWith(Room1200->ResultText));

    // A new search rebuilds the list, but the tag should follow the room
    SetSearch(Panel, TEXT("1200"));
    TestTrue(TEXT("Tag stays on PFT 1200 after a new search"), GetTaggedRooms(Panel).Contains(TEXT("PFT_1200")));

    // Clearing the room takes the tag off, and it stays off when the list is rebuilt
    ClearButton->OnClicked.Broadcast();
    TestEqual(TEXT("Clear removes the tag"), GetTaggedRooms(Panel).Num(), 0);
    SetSearch(Panel, TEXT(""));
    TestEqual(TEXT("Tag stays off after the list is rebuilt"), GetTaggedRooms(Panel).Num(), 0);
    Cleanup();
    return true;
}
#endif
