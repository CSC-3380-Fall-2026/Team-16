#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTNavigatorWidget.h"
#include "PFTRoutes.h"
#include "PFTViewportTest.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

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
        auto* Tree = Widgets[0]->WidgetTree.Get();
        auto* Search = Cast<UEditableTextBox>(Tree->FindWidget(TEXT("RoomSearch")));
        if (!TestNotNull(TEXT("Room search"), Search)) return true;
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
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Tree, Player, Character, Routes]()
        {
            if (!TestTrue(TEXT("Start walking route is clickable"),
                PFTViewportTest::ClickVisibleWidget(Tree->FindWidget(TEXT("RouteButton"))))) return true;
            if (!TestTrue(TEXT("The route starts"), Routes->IsGuiding()))
            {
                AddError(Routes->GetMessage().ToString());
                return true;
            }
            TestTrue(TEXT("Guidance keeps the first-person pawn"), Player->GetPawn() == Character);
            TestFalse(TEXT("The player can walk"), Player->IsMoveInputIgnored());
            TestFalse(TEXT("The player can look"), Player->IsLookInputIgnored());

            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Player, Character, Routes, Tree,
                Deadline = FPlatformTime::Seconds() + 65.0]()
            {
                const FVector Position = Character->GetActorLocation();
                if (Routes->HasArrived())
                {
                    TestTrue(TEXT("The player reaches the Commons"), FVector::Dist2D(Position, FVector(14900, -5160, 0)) < 180);
                    auto* Hint = Cast<UTextBlock>(Tree->FindWidget(TEXT("RouteHint")));
                    if (TestNotNull(TEXT("Walking instructions"), Hint))
                        TestTrue(TEXT("The arrival message names the Commons"), Hint->GetText().ToString().Contains(TEXT("Arrived at PFT Commons")));
                    AddInfo(FString::Printf(TEXT("Reached the Commons with route guidance at %s"), *Position.ToCompactString()));
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

#endif
