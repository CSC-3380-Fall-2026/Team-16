#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTNavigatorWidget.h"
#include "PFTRouteMap.h"
#include "PFTRoutes.h"
#include "PFTViewportTest.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTRefocusAfterRerouteTest, "PFTFirstFloor.RefocusAfterReroute",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTRefocusAfterRerouteTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    auto* Player = World ? World->GetFirstPlayerController() : nullptr;
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-floor walking pawn"), Character)) return false;
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    if (!TestTrue(TEXT("Skip tour opens the Navigator"), PFTViewportTest::SkipWelcome(World))) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Player, Character, Routes,
        ReadyBy = FPlatformTime::Seconds() + 10.0]()
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        if (Widgets.IsEmpty() && FPlatformTime::Seconds() < ReadyBy) return false;
        if (!TestEqual(TEXT("The Navigator opens after the welcome screen"), Widgets.Num(), 1)) return true;
        auto* Navigator = CastChecked<UPFTNavigatorWidget>(Widgets[0]);
        Navigator->ChooseRoom(TEXT("PFT_COMMONS"));
        auto* Tree = Navigator->WidgetTree.Get();
        CastChecked<UButton>(Tree->FindWidget(TEXT("RouteButton")))->OnClicked.Broadcast();
        if (!TestTrue(TEXT("The Commons route starts"), Routes->IsGuiding() && !Routes->GetRemainingPath().IsEmpty())) return true;
        CastChecked<UButton>(Tree->FindWidget(TEXT("RouteOverviewButton")))->OnClicked.Broadcast();
        auto* Map = CastChecked<UPFTRouteMap>(Tree->FindWidget(TEXT("WalkingRouteMap")));
        auto* Overview = Tree->FindWidget(TEXT("RouteOverview"));
        const FVector Destination = Routes->GetRemainingPath().Last();
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Player, Character, Routes, Map, Overview, Destination, Tree]()
        {
            const FVector Start = Character->GetActorLocation();
            const FVector Hallway(12960, 960, Start.Z);
            const FBox2D OldBounds = Map->GetFocusBoundsForTests();
            TestTrue(TEXT("The initial frame fits the Commons route"), OldBounds.bIsValid && OldBounds.IsInside(FVector2D(Start)));
            TestFalse(TEXT("The C1007 hallway is outside the original frame"), OldBounds.IsInside(FVector2D(Hallway)));
            const uint32 OriginalPlan = Routes->GetPlanRevision();
            const FString OriginalSteps = Routes->GetRemainingSteps().ToString();
            Character->SetActorLocation(Hallway, false, nullptr, ETeleportType::TeleportPhysics);
            Character->GetCharacterMovement()->StopMovementImmediately();
            Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Player, Routes, Map, Overview, Hallway, Destination, Tree,
                OriginalPlan, OriginalSteps, SawUpdate = MakeShared<bool>(false), Deadline = FPlatformTime::Seconds() + 5.0]()
            {
                if (Routes->IsReplanning())
                    *SawUpdate = Routes->GetRemainingSteps().ToString() == TEXT("Updating route…");
                const auto& Bounds = Map->GetFocusBoundsForTests();
                const bool bRerouted = Routes->GetPlanRevision() != OriginalPlan && !Routes->GetRemainingPath().IsEmpty();
                const bool bRefocused = Bounds.bIsValid && Bounds.IsInside(FVector2D(Hallway));
                if ((!bRerouted || !bRefocused) && FPlatformTime::Seconds() < Deadline) return false;
                if (!TestTrue(TEXT("Guidance reroutes automatically from C1007"), bRerouted)) return true;
                TestTrue(TEXT("The map refocuses to include the new hallway and destination"),
                    bRefocused && Bounds.IsInside(FVector2D(Destination)));
                TestTrue(TEXT("The reroute keeps the Commons destination"), Routes->GetRemainingPath().Last().Equals(Destination));
                TestTrue(TEXT("Replanning hides the obsolete route steps"), *SawUpdate);
                const FString UpdatedSteps = Routes->GetRemainingSteps().ToString();
                TestTrue(TEXT("The reroute replaces the steps and keeps the final leg"),
                    UpdatedSteps != OriginalSteps && UpdatedSteps.EndsWith(TEXT("to the destination")));
                TestTrue(TEXT("The map and walking controls stay available"), Overview->IsVisible() && !Player->IsMoveInputIgnored());
                if (FParse::Param(FCommandLine::Get(), TEXT("PFTQAScreenshots")))
                {
                    FScreenshotRequest::RequestScreenshot(TEXT("PFTQA-MapAfterReroute"), true, true, false);
                    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
                }
                ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Tree]()
                {
                    CastChecked<UButton>(Tree->FindWidget(TEXT("StopRouteButton")))->OnClicked.Broadcast();
                    const auto* Trail = FindObject<ULineBatchComponent>(Routes, TEXT("PFTWalkingTrail"));
                    TestTrue(TEXT("Stopping the route clears the arrows"), !Routes->IsGuiding() && Trail && Trail->BatchedLines.IsEmpty());
                    return true;
                }));
                return true;
            }));
            return true;
        }));
        return true;
    }));
    return true;
}

#endif
