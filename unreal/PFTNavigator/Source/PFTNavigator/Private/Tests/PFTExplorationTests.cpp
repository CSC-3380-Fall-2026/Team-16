#include "PFTExploration.h"
#include "PFTExplorePawn.h"
#include "PFTDestinations.h"
#include "PFTNavigatorWidget.h"
#include "PFTRoomMarker.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/InputComponent.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTExplorationTest, "PFTNavigator.Exploration.ControlsAndUI", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPFTExplorationTest::RunTest(const FString& Parameters)
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
    auto* Original = World->SpawnActor<APFTExplorePawn>();
    Player->Possess(Original);
    Player->SetControlRotation(FRotator(0, 45, 0));
    Player->bShowMouseCursor = true;
    auto* Exploration = World->GetSubsystem<UPFTExploration>();
    TestFalse(TEXT("No destination does not start exploration"), Exploration->ViewRoom());
    if (!TestTrue(TEXT("Start exploration"), Exploration->BeginExplore()))
    {
        AddError(Exploration->Error);
        Cleanup();
        return false;
    }
    TestTrue(TEXT("Exploration possesses new pawn"), Player->GetPawn() == Exploration->ExplorePawn);
    TestFalse(TEXT("Fly captures mouse"), Player->bShowMouseCursor);
    Exploration->ExplorePawn->MoveForward(1);
    TestFalse(TEXT("Movement input is accepted"), Exploration->ExplorePawn->GetPendingMovementInputVector().IsNearlyZero());
    Exploration->ExplorePawn->ToggleMouse();
    TestTrue(TEXT("Tab enables mouse"), Player->bShowMouseCursor);
    TestTrue(TEXT("Mouse UI pauses movement"), Player->IsMoveInputIgnored());
    Exploration->SetCursor(true);
    Exploration->ExplorePawn->ToggleMouse();
    TestFalse(TEXT("Repeated cursor requests do not stack input locks"), Player->IsMoveInputIgnored());
    TestTrue(TEXT("Return succeeds"), Exploration->EndExplore());
    TestTrue(TEXT("Original pawn restored"), Player->GetPawn() == Original);
    TestEqual(TEXT("Original rotation restored"), Player->GetControlRotation().Yaw, 45.0);
    TestTrue(TEXT("Original cursor restored"), Player->bShowMouseCursor);
    auto* Marker = World->SpawnActor<APFTRoomMarker>(FVector(200, 300, 400), FRotator::ZeroRotator);
    Marker->RoomId = TEXT("PFT_1200");
    auto* Panel = CreateWidget<UPFTNavigatorWidget>(Player, UPFTNavigatorWidget::StaticClass());
    Panel->TakeWidget();
    auto* FloorFilter = Cast<UComboBoxString>(Panel->WidgetTree->FindWidget(TEXT("FloorFilter")));
    auto* Details = Cast<UTextBlock>(Panel->WidgetTree->FindWidget(TEXT("RoomDetails")));
    auto* ViewButton = Cast<UButton>(Panel->WidgetTree->FindWidget(TEXT("ViewRoomButton")));
    auto* ClearButton = Cast<UButton>(Panel->WidgetTree->FindWidget(TEXT("ClearRoomButton")));
    if (!TestNotNull(TEXT("Floor filter exists"), FloorFilter) || !TestNotNull(TEXT("Room details exist"), Details)
        || !TestNotNull(TEXT("View button exists"), ViewButton) || !TestNotNull(TEXT("Clear button exists"), ClearButton))
    {
        Cleanup();
        return false;
    }
    TestFalse(TEXT("Room view disabled without selection"), ViewButton->GetIsEnabled());
    TestFalse(TEXT("Clear disabled without selection"), ClearButton->GetIsEnabled());
    TArray<UWidget*> Widgets;
    Panel->WidgetTree->GetAllWidgets(Widgets);
    for (auto* Item : Widgets)
    {
        if (auto* Search = Cast<UEditableTextBox>(Item))
        {
            Search->SetText(FText::FromString(TEXT("CSC 3380")));
            Search->OnTextChanged.Broadcast(Search->GetText());
        }
    }
    Widgets.Empty();
    Panel->WidgetTree->GetAllWidgets(Widgets);
    int32 ResultCount = 0;
    for (auto* Item : Widgets)
    {
        if (auto* Button = Cast<UPFTSearchButton>(Item))
        {
            ++ResultCount;
            Button->OnClicked.Broadcast();
        }
    }
    TestEqual(TEXT("Course search produces one button"), ResultCount, 1);
    TestTrue(TEXT("Click selects course destination"), World->GetSubsystem<UPFTDestinations>()->ActiveMarker == Marker);
    TestTrue(TEXT("Selected details show floor"), Details->GetText().ToString().Contains(TEXT("Floor 1")));
    TestTrue(TEXT("Selected details show course"), Details->GetText().ToString().Contains(TEXT("CSC 3380")));
    TestTrue(TEXT("Bound room enables view"), ViewButton->GetIsEnabled());
    if (!TestTrue(TEXT("View selected room"), Exploration->ViewRoom()))
    {
        AddError(Exploration->Error);
        Cleanup();
        return false;
    }
    TestTrue(TEXT("Camera moves near destination"), FVector::Dist(Exploration->ExplorePawn->GetActorLocation(), Marker->GetActorLocation()) < 800);
    TestTrue(TEXT("Room view opens mouse controls"), Exploration->bCursorShown);
    TestTrue(TEXT("Return after room view"), Exploration->EndExplore());
    ClearButton->OnClicked.Broadcast();
    TestNull(TEXT("Clear removes active marker"), World->GetSubsystem<UPFTDestinations>()->ActiveMarker.Get());
    TestTrue(TEXT("Clear hides previous marker"), Marker->IsHidden());
    TestFalse(TEXT("Clear disables room view"), ViewButton->GetIsEnabled());
    Panel->ChooseRoom(TEXT("PFT_3307"));
    TestTrue(TEXT("Unbound room still shows details"), Details->GetText().ToString().Contains(TEXT("3307")));
    TestFalse(TEXT("Unbound room cannot move camera"), ViewButton->GetIsEnabled());
    TestTrue(TEXT("Unbound room can be cleared"), ClearButton->GetIsEnabled());
    FloorFilter->SetSelectedOption(TEXT("3"));
    TArray<UWidget*> FilteredWidgets;
    Panel->WidgetTree->GetAllWidgets(FilteredWidgets);
    int32 FilteredCount = 0;
    for (auto* Item : FilteredWidgets) if (Cast<UPFTSearchButton>(Item)) ++FilteredCount;
    TestEqual(TEXT("Course search on third floor is empty"), FilteredCount, 0);
    Cleanup();
    return true;
}
#endif
