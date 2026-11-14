#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTNavigatorWidget.h"
#include "PFTRoutes.h"
#include "PFTViewportTest.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/LineBatchComponent.h"
#include "Components/TextBlock.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Tests/AutomationCommon.h"

namespace
{
    void CheckSettings(FAutomationTestBase& Test, UWidgetTree* Tree)
    {
        auto* SettingsButton = Cast<UButton>(Tree->FindWidget(TEXT("RouteSettingsButton")));
        if (!Test.TestNotNull(TEXT("Settings are available"), SettingsButton)) return;
        SettingsButton->OnClicked.Broadcast();
        Test.TestTrue(TEXT("Settings expand"), Tree->FindWidget(TEXT("RouteSettings"))->IsVisible());
        auto* LargerText = CastChecked<UCheckBox>(Tree->FindWidget(TEXT("LargerDirections")));
        auto* Performance = CastChecked<UCheckBox>(Tree->FindWidget(TEXT("PerformanceMode")));
        auto* Hint = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
        auto* RenderScale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
        if (!Test.TestNotNull(TEXT("Render resolution can be adjusted"), RenderScale)) return;
        const float OriginalScale = RenderScale->GetFloat();
        LargerText->OnCheckStateChanged.Broadcast(true);
        Test.TestTrue(TEXT("Larger directions increases the text size"), Hint->GetFont().Size == 24.0f);
        Performance->OnCheckStateChanged.Broadcast(true);
        Test.TestEqual(TEXT("Performance mode lowers rendering resolution"), RenderScale->GetFloat(), FMath::Min(75.0f, OriginalScale));
        Performance->OnCheckStateChanged.Broadcast(false);
        Test.TestEqual(TEXT("Performance mode can restore the original resolution"), RenderScale->GetFloat(), OriginalScale);
        RenderScale->Set(OriginalScale, ECVF_SetByConsole);
        LargerText->OnCheckStateChanged.Broadcast(LargerText->IsChecked());
        Performance->OnCheckStateChanged.Broadcast(Performance->IsChecked());
        RenderScale->Set(OriginalScale, ECVF_SetByConsole);
        SettingsButton->OnClicked.Broadcast();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTGuidedWalkTest, "PFTFirstFloor.GuidedWalk",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTGuidedWalkTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    if (!TestNotNull(TEXT("First-floor demo world"), World)) return false;
    auto* Player = World->GetFirstPlayerController();
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-person walking pawn"), Character)) return false;
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    if (!TestTrue(TEXT("Skip tour is clickable"), PFTViewportTest::SkipWelcome(World))) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, Character, Routes,
        ReadyBy = FPlatformTime::Seconds() + 10.0]()
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        if (Widgets.IsEmpty() && FPlatformTime::Seconds() < ReadyBy) return false;
        if (!TestEqual(TEXT("Navigator is visible"), Widgets.Num(), 1)) return true;
        auto* Navigator = CastChecked<UPFTNavigatorWidget>(Widgets[0]);
        auto* Tree = Navigator->WidgetTree.Get();
        auto* Search = Cast<UEditableTextBox>(Tree->FindWidget(TEXT("RoomSearch")));
        if (!TestNotNull(TEXT("Room search"), Search)) return true;
        CheckSettings(*this, Tree);
        Search->SetText(FText::FromString(TEXT("Commons")));
        Search->OnTextChanged.Broadcast(Search->GetText());
        UPFTSearchButton* Result = nullptr;
        Tree->ForEachWidget([&](UWidget* Widget)
        {
            auto* Button = Cast<UPFTSearchButton>(Widget);
            if (Button && Button->RoomId == TEXT("PFT_COMMONS")) Result = Button;
        });
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Result]()
        {
            TestTrue(TEXT("Commons search result is clickable"), PFTViewportTest::ClickVisibleWidget(Result));
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Navigator, Tree, Player, Character, Routes]()
        {
            if (!TestTrue(TEXT("Walk there is clickable"),
                PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RouteButton"))))) return true;
            if (!TestTrue(TEXT("The route starts"), Routes->IsGuiding()))
            {
                AddError(Routes->GetMessage().ToString());
                return true;
            }
            TestTrue(TEXT("Guidance keeps the first-person pawn"), Player->GetPawn() == Character);
            TestFalse(TEXT("The player can walk"), Player->IsMoveInputIgnored());
            TestFalse(TEXT("The player can look"), Player->IsLookInputIgnored());
            TestTrue(TEXT("The browser collapses while walking"), Tree->FindWidget(TEXT("RoomBrowser"))->GetVisibility() == ESlateVisibility::Collapsed);
            TestTrue(TEXT("The compact directions remain visible"), Tree->FindWidget(TEXT("DirectionsHud"))->IsVisible());
            TestTrue(TEXT("Stop route remains available"), Tree->FindWidget(TEXT("StopRouteButton"))->IsVisible());

            auto* Trail = FindObject<ULineBatchComponent>(Routes, TEXT("PFTWalkingTrail"));
            if (!TestNotNull(TEXT("The route creates a trail component"), Trail)) return true;
            if (!TestTrue(TEXT("The trail draws multiple arrows"), Trail->BatchedLines.Num() >= 8)) return true;
            const TArray<FBatchedLine> InitialArrows = Trail->BatchedLines;
            auto StationaryTip = MakeShared<FVector>();
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Trail, InitialArrows]()
            {
                if (!TestTrue(TEXT("Arrows stay visible while standing still"), Trail->BatchedLines.Num() >= 8)) return true;
                const auto& Arrow = Trail->BatchedLines[2];
                TestTrue(TEXT("The arrows move while the player is stationary"),
                    !InitialArrows[2].End.Equals(Arrow.End, 2.0));
                TestTrue(TEXT("The two arms meet at the arrow tip"), Arrow.End.Equals(Trail->BatchedLines[3].Start));
                Routes->SetReducedMotion(true);
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Trail, StationaryTip]()
            {
                if (Trail->BatchedLines.IsValidIndex(2)) *StationaryTip = Trail->BatchedLines[2].End;
                return true;
            }));
            ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Trail, StationaryTip]()
            {
                TestTrue(TEXT("Reduced motion keeps the arrows still"),
                    Trail->BatchedLines.IsValidIndex(2) && Trail->BatchedLines[2].End.Equals(*StationaryTip));
                Routes->SetReducedMotion(false);
                return true;
            }));

            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Navigator, Player, Character, Routes, Tree, Trail,
                Deadline = FPlatformTime::Seconds() + 65.0]()
            {
                const FVector Position = Character->GetActorLocation();
                if (Routes->HasArrived())
                {
                    TestTrue(TEXT("The player reaches the Commons"), FVector::Dist2D(Position, FVector(14900, -5160, 0)) < 180);
                    TestTrue(TEXT("Arrival clears the arrow trail"), Trail->BatchedLines.IsEmpty());
                    const auto* ArrivalLabel = FindObject<UTextRenderComponent>(Routes, TEXT("PFTArrivalLabel"));
                    TestTrue(TEXT("Arrival marks the destination in the building"), ArrivalLabel && ArrivalLabel->IsVisible());
                    auto* Hint = Cast<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                    if (TestNotNull(TEXT("Walking instructions"), Hint))
                        TestTrue(TEXT("The arrival message names the Commons"), Hint->GetText().ToString().Contains(TEXT("Arrived at PFT Commons")));
                    AddInfo(FString::Printf(TEXT("Reached the Commons with route guidance at %s"), *Position.ToCompactString()));
                    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
                    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Navigator, Tree, Character, Routes, ArrivalLabel]()
                    {
                        TestTrue(TEXT("Find another room is clickable after arrival"),
                            PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("FindAnotherRoomButton"))));
                        TestTrue(TEXT("Find another room restores the browser"), Tree->FindWidget(TEXT("RoomBrowser"))->IsVisible());
                        TestTrue(TEXT("The previous arrival marker clears"), ArrivalLabel && !ArrivalLabel->IsVisible());
                        Navigator->ChooseRoom(TEXT("PFT_3307"));
                        TestTrue(TEXT("An unsupported room explains walking directions are unavailable"),
                            Tree->FindWidget(TEXT("RouteUnavailable"))->IsVisible());
                        TestFalse(TEXT("An unsupported room cannot start a route"), Tree->FindWidget(TEXT("RouteButton"))->IsVisible());
                        Navigator->ChooseRoom(TEXT("PFT_1200"));
                        CastChecked<UButton>(Tree->FindWidget(TEXT("RouteButton")))->OnClicked.Broadcast();
                        if (!TestTrue(TEXT("A second route starts at the Commons"), Routes->IsGuiding())) return true;
                        const FVector ReturnPosition = Character->GetActorLocation();
                        // Leave the route entirely to exercise recovery without depending on manual input.
                        Character->SetActorLocation(ReturnPosition + FVector(0, 0, 5000), false, nullptr, ETeleportType::TeleportPhysics);
                        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
                        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes]()
                        {
                            TestTrue(TEXT("Leaving the route shows an update state"), Routes->IsReplanning());
                            return true;
                        }));
                        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.7f));
                        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Character, ReturnPosition, Tree]()
                        {
                            TestTrue(TEXT("A disconnected position offers retry"), Routes->CanRetry());
                            TestTrue(TEXT("Retry is visible"), Tree->FindWidget(TEXT("RetryRouteButton"))->IsVisible());
                            Character->SetActorLocation(ReturnPosition, false, nullptr, ETeleportType::TeleportPhysics);
                            Character->GetCharacterMovement()->StopMovementImmediately();
                            Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                            CastChecked<UButton>(Tree->FindWidget(TEXT("RetryRouteButton")))->OnClicked.Broadcast();
                            TestTrue(TEXT("Retry restarts guidance from the hallway"), Routes->IsGuiding());
                            return true;
                        }));
                        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Character, Tree,
                            Deadline = FPlatformTime::Seconds() + 110.0]()
                        {
                            if (Routes->HasArrived())
                            {
                                const auto* Hint = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                                TestTrue(TEXT("The second leg arrives at room 1200"), Hint->GetText().ToString().Contains(TEXT("Arrived at PFT 1200")));
                                return true;
                            }
                            if (!Routes->IsGuiding() || FPlatformTime::Seconds() > Deadline)
                            {
                                AddError(FString::Printf(TEXT("The second route stopped at %s. %s"),
                                    *Character->GetActorLocation().ToCompactString(), *Routes->GetMessage().ToString()));
                                return true;
                            }
                            const FVector Delta = Routes->NextPoint() - Character->GetActorLocation();
                            const FVector Direction = Delta.GetSafeNormal2D();
                            Character->GetController()->SetControlRotation(Direction.Rotation());
                            Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
                            return false;
                        }));
                        return true;
                    }));
                    return true;
                }
                if (!Routes->IsGuiding() || Position.Z < 65 || FPlatformTime::Seconds() > Deadline)
                {
                    AddError(FString::Printf(TEXT("Guided walk stopped at %s with next point %s. %s"),
                        *Position.ToCompactString(), *Routes->NextPoint().ToCompactString(), *Routes->GetMessage().ToString()));
                    return true;
                }
                const FVector Delta = Routes->NextPoint() - Position;
                const FVector Direction = Delta.GetSafeNormal2D();
                Player->SetControlRotation(Direction.Rotation());
                Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
                return false;
            }));
            return true;
        }));
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTWalkToRoom1218Test, "PFTFirstFloor.WalkToRoom1218",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTWalkToRoom1218Test::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    if (!TestNotNull(TEXT("First-floor demo world"), World)) return false;
    auto* Player = World->GetFirstPlayerController();
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-person walking pawn"), Character)) return false;
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    if (!TestTrue(TEXT("Skip tour is clickable"), PFTViewportTest::SkipWelcome(World))) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Character, Routes,
        ReadyBy = FPlatformTime::Seconds() + 10.0]()
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        if (Widgets.IsEmpty() && FPlatformTime::Seconds() < ReadyBy) return false;
        if (!TestEqual(TEXT("Navigator is visible"), Widgets.Num(), 1)) return true;
        auto* Navigator = CastChecked<UPFTNavigatorWidget>(Widgets[0]);
        Navigator->ChooseRoom(TEXT("PFT_1218"));
        auto* Tree = Navigator->WidgetTree.Get();
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Character, Routes]()
        {
            if (!TestTrue(TEXT("Walk there starts directions to 1218"),
                PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RouteButton"))) && Routes->IsGuiding())) return true;
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Character, Routes,
                Deadline = FPlatformTime::Seconds() + 40.0]()
            {
                const FVector Position = Character->GetActorLocation();
                if (Routes->HasArrived())
                {
                    const FVector HallwayStop(10440, -5700, 0);
                    TestTrue(TEXT("The player stops in the hallway by the signed door"),
                        FVector::Dist2D(Position, HallwayStop) <= 65.0 && Position.X < 10500
                        && Character->GetCharacterMovement()->IsMovingOnGround());
                    const auto* Hint = CastChecked<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                    TestTrue(TEXT("The arrival message names PFT 1218"), Hint->GetText().ToString().Contains(TEXT("Arrived at PFT 1218")));
                    AddInfo(FString::Printf(TEXT("Reached the Room 1218 hallway stop at %s"), *Position.ToCompactString()));
                    return true;
                }
                if (!Routes->IsGuiding() || Position.Z < 65 || FPlatformTime::Seconds() > Deadline)
                {
                    AddError(FString::Printf(TEXT("Room 1218 route stopped at %s. %s"),
                        *Position.ToCompactString(), *Routes->GetMessage().ToString()));
                    return true;
                }
                const FVector Delta = Routes->NextPoint() - Position;
                const FVector Direction = Delta.GetSafeNormal2D();
                Character->GetController()->SetControlRotation(Direction.Rotation());
                Character->AddMovementInput(Direction, FMath::Clamp(Delta.Size2D() / 95.0, 0.16, 0.8));
                return false;
            }));
            return true;
        }));
        return true;
    }));
    return true;
}

#endif
