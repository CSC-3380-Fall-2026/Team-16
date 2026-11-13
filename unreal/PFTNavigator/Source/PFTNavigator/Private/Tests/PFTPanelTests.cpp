#include "PFTNavigatorWidget.h"
#include "PFTDestinations.h"
#include "PFTDirectory.h"
#include "PFTExploration.h"
#include "PFTExplorePawn.h"
#include "PFTRoomMarker.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "PFTViewportTest.h"

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

namespace
{
    using PFTViewportTest::ClickVisibleWidget;

    bool PressKey(FKey Key)
    {
        auto& Slate = FSlateApplication::Get();
        const FKeyEvent Event(Key, FModifierKeysState(), Slate.GetUserIndexForKeyboard(), false, 0, 0);
        const bool bHandled = Slate.ProcessKeyDownEvent(Event);
        Slate.ProcessKeyUpEvent(Event);
        return bHandled;
    }

    UPFTNavigatorWidget* FindNavigator(UWorld* World)
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        return Widgets.Num() == 1 ? Cast<UPFTNavigatorWidget>(Widgets[0]) : nullptr;
    }

    FString GetCameraHelp(UPFTNavigatorWidget* Panel)
    {
        const auto* Help = Cast<UTextBlock>(Panel->WidgetTree->FindWidget(TEXT("CameraHelp")));
        return Help ? Help->GetText().ToString() : FString();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTNavigatorAfterOnboardingTest, "PFTNavigator.Panel.AfterOnboarding",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTNavigatorAfterOnboardingTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    if (!TestNotNull(TEXT("The packaged demo has a local player"), Player)) return false;
    TestNull(TEXT("The welcome screen opens before the Navigator"), FindNavigator(World));

    TestTrue(TEXT("Walking is locked on the welcome screen"), Player->IsMoveInputIgnored());
    TestTrue(TEXT("Looking is locked on the welcome screen"), Player->IsLookInputIgnored());
    if (!TestTrue(TEXT("Skip tour is clickable"), PFTViewportTest::SkipWelcome(World))) return false;
    TestFalse(TEXT("Walking is unlocked after Skip tour"), Player->IsMoveInputIgnored());
    TestFalse(TEXT("Looking is unlocked after Skip tour"), Player->IsLookInputIgnored());

    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player]()
    {
        auto* Panel = FindNavigator(World);
        if (TestNotNull(TEXT("Navigator opens after Skip tour"), Panel))
        {
            TestTrue(TEXT("The cursor is available"), Player->bShowMouseCursor);
            TestFalse(TEXT("The walking view hides flight shortcuts"), GetCameraHelp(Panel).Contains(TEXT("Q down")));
            const auto* Directory = World->GetGameInstance()->GetSubsystem<UPFTDirectory>();
            const auto Matches = Directory->BrowseRooms(TEXT(""));
            TestTrue(TEXT("The demo directory has a second page"), Matches.Num() > 50);
            TestEqual(TEXT("The room browser starts with 50 rooms"), GetResults(Panel).Num(), 50);
            TestTrue(TEXT("Show more rooms is clickable"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("MoreRoomsButton"))));
            const auto Results = GetResults(Panel);
            if (TestEqual(TEXT("Show more rooms adds the remaining matches"), Results.Num(), Matches.Num()))
            {
                for (int32 Index = 0; Index < Results.Num(); ++Index)
                {
                    TestEqual(TEXT("Room order is preserved"), Results[Index]->RoomId, Matches[Index].RoomId);
                }
            }
        }
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        const auto Results = GetResults(Panel);
        if (Results.IsValidIndex(50))
        {
            TestTrue(TEXT("The first newly loaded room is clickable"), ClickVisibleWidget(Results[50]));
            TestTrue(TEXT("The newly loaded room is selected"), GetTaggedRooms(Panel).Contains(Results[50]->RoomId));
        }
        SetSearch(Panel, TEXT("CSC 1350"));
        TestEqual(TEXT("Changing the search resets the result list"), GetResults(Panel).Num(), 1);
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        for (auto* Result : GetResults(Panel))
        {
            if (Result->RoomId != TEXT("PFT_1200")) continue;
            TestTrue(TEXT("The mapped result advertises its preview"), Result->ResultText.EndsWith(TEXT(" | Preview")));
            TestTrue(TEXT("The course result is clickable"), ClickVisibleWidget(Result));
            break;
        }
        TestTrue(TEXT("The course selects PFT 1200"), GetTaggedRooms(Panel).Contains(TEXT("PFT_1200")));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        auto* Details = Panel->WidgetTree->FindWidget(TEXT("RoomDetails"));
        if (TestNotNull(TEXT("Selected room details exist"), Details))
        {
            const auto& DetailsGeometry = Details->GetCachedGeometry();
            const auto& PanelGeometry = Panel->WidgetTree->RootWidget->GetCachedGeometry();
            const float DetailsBottom = DetailsGeometry.LocalToAbsolute(FVector2D(0, DetailsGeometry.GetLocalSize().Y)).Y;
            const float PanelBottom = PanelGeometry.LocalToAbsolute(FVector2D(0, PanelGeometry.GetLocalSize().Y)).Y;
            TestTrue(TEXT("A single search result leaves the selected details in view"), DetailsBottom <= PanelBottom);
        }
        TestTrue(TEXT("View room is clickable"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("ViewRoomButton"))));
        TestNotNull(TEXT("The preview possesses the exploration pawn"), Cast<APFTExplorePawn>(Player->GetPawn()));
        TestTrue(TEXT("The preview keeps the cursor available"), Player->bShowMouseCursor);
        TestTrue(TEXT("Preview explains how to leave its fixed view"), GetCameraHelp(Panel).StartsWith(TEXT("Room preview")));
        auto* Marker = World->GetSubsystem<UPFTDestinations>()->ActiveMarker.Get();
        if (TestNotNull(TEXT("The preview keeps the room selected"), Marker))
        {
            TestTrue(TEXT("The preview keeps the room marker out of view"), Marker->IsHidden());
        }
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, OriginalPawn = Player->GetPawn()]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        TestTrue(TEXT("Return is clickable"),
            ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("CameraModeButton"))));
        TestEqual(TEXT("The original pawn is restored"), Player->GetPawn(), OriginalPawn);
        TestTrue(TEXT("Clear is clickable"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("ClearRoomButton"))));
        TestEqual(TEXT("Clear removes the room tag"), GetTaggedRooms(Panel).Num(), 0);
        SetSearch(Panel, TEXT("CSC"));
        TestTrue(TEXT("The search box is clickable"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("RoomSearch"))));
        TestTrue(TEXT("A broad search has multiple results"), GetResults(Panel).Num() > 1);
        TestTrue(TEXT("The search handles Enter"), PressKey(EKeys::Enter));
        TestEqual(TEXT("Enter leaves multiple matches unselected"), GetTaggedRooms(Panel).Num(), 0);

        SetSearch(Panel, TEXT("CSC 1350"));
        TestTrue(TEXT("Enter opens the single result"), PressKey(EKeys::Enter));
        TestTrue(TEXT("The keyboard selects PFT 1200"), GetTaggedRooms(Panel).Contains(TEXT("PFT_1200")));
        TestNotNull(TEXT("Enter opens the room preview"), Cast<APFTExplorePawn>(Player->GetPawn()));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, OriginalPawn = Player->GetPawn()]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        TestTrue(TEXT("Clear is clickable during a preview"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("ClearRoomButton"))));
        TestEqual(TEXT("Clear restores the original pawn"), Player->GetPawn(), OriginalPawn);
        TestNull(TEXT("Clear removes the active destination"), World->GetSubsystem<UPFTDestinations>()->ActiveMarker.Get());
        SetSearch(Panel, TEXT("3307"));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, OriginalPawn = Player->GetPawn()]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        const auto Results = GetResults(Panel);
        if (!TestEqual(TEXT("PFT 3307 returns one result"), Results.Num(), 1)) return true;
        TestTrue(TEXT("The upper-floor result advertises details only"), Results[0]->ResultText.EndsWith(TEXT(" | Details only")));
        TestTrue(TEXT("The details-only result is clickable"), ClickVisibleWidget(Results[0]));
        TestTrue(TEXT("The upper-floor result can be selected"), GetTaggedRooms(Panel).Contains(TEXT("PFT_3307")));
        TestEqual(TEXT("The details-only selection keeps the walking pawn"), Player->GetPawn(), OriginalPawn);
        const auto* ViewButton = Panel->WidgetTree->FindWidget(TEXT("ViewRoomButton"));
        if (TestNotNull(TEXT("The preview button exists"), ViewButton))
        {
            TestFalse(TEXT("The preview button is disabled for details-only rooms"), ViewButton->GetIsEnabled());
        }
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        TestTrue(TEXT("Free camera is clickable"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("CameraModeButton"))));
        TestNotNull(TEXT("Free camera possesses the exploration pawn"), Cast<APFTExplorePawn>(Player->GetPawn()));
        TestFalse(TEXT("Free camera captures the cursor"), Player->bShowMouseCursor);
        TestTrue(TEXT("Flight shortcuts are visible"), GetCameraHelp(Panel).Contains(TEXT("Q down")));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
    {
        PressKey(EKeys::Tab);
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, OriginalPawn = Player->GetPawn()]()
    {
        auto* Panel = FindNavigator(World);
        if (!Panel) return true;
        TestTrue(TEXT("Tab releases the cursor"), Player->bShowMouseCursor);
        TestTrue(TEXT("The paused camera shows how to resume"), GetCameraHelp(Panel).StartsWith(TEXT("Camera paused")));
        TestTrue(TEXT("Return is clickable after Tab"), ClickVisibleWidget(Panel->WidgetTree->FindWidget(TEXT("CameraModeButton"))));
        TestEqual(TEXT("Returning restores the walking pawn"), Player->GetPawn(), OriginalPawn);
        TestFalse(TEXT("Returning hides flight shortcuts"), GetCameraHelp(Panel).Contains(TEXT("Q down")));
        return true;
    }));
    return true;
}
#endif
