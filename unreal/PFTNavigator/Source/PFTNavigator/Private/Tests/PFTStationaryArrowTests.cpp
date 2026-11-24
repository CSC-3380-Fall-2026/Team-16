#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTNavigatorWidget.h"
#include "PFTRoutes.h"
#include "PFTViewportTest.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTStationaryArrowTest, "PFTFirstFloor.StationaryArrows",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTStationaryArrowTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    auto* Player = World ? World->GetFirstPlayerController() : nullptr;
    auto* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-floor walking pawn"), Character)) return false;
    auto* Routes = World->GetSubsystem<UPFTRoutes>();
    if (!TestTrue(TEXT("Skip tour opens the Navigator"), PFTViewportTest::SkipWelcome(World))) return false;

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Character, Routes,
        ReadyBy = FPlatformTime::Seconds() + 10.0]()
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UPFTNavigatorWidget::StaticClass(), true);
        const bool bReady = !Widgets.IsEmpty() && Character->GetCharacterMovement()->IsMovingOnGround();
        if (!bReady && FPlatformTime::Seconds() < ReadyBy) return false;
        if (!TestTrue(TEXT("The walking pawn and Navigator are ready"), bReady)) return true;
        Routes->SetReducedMotion(true);
        if (!TestTrue(TEXT("The Commons route starts"), Routes->Start(*Character, TEXT("PFT_COMMONS"), TEXT("The Commons"))))
        {
            return true;
        }
        const auto* Trail = FindObject<ULineBatchComponent>(Routes, TEXT("PFTWalkingTrail"));
        if (!TestTrue(TEXT("The route draws stationary arrows"), Trail && !Trail->BatchedLines.IsEmpty())) return true;

        const auto DrawCount = MakeShared<uint32>(Routes->GetTrailDrawCountForTests());
        const FVector InitialTip = Trail->BatchedLines[0].End;
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Character, Routes, Trail, DrawCount, InitialTip]()
        {
            const uint32 IdleDraws = Routes->GetTrailDrawCountForTests() - *DrawCount;
            AddInfo(FString::Printf(TEXT("Stationary arrow redraws in 0.3 seconds: %u"), IdleDraws));
            TestEqual(TEXT("Standing still does not rebuild the trail"), IdleDraws, 0u);
            TestTrue(TEXT("Stationary arrows stay visible at the same position"),
                !Trail->BatchedLines.IsEmpty() && Trail->BatchedLines[0].End.Equals(InitialTip));
            *DrawCount = Routes->GetTrailDrawCountForTests();
            const FVector Position = Character->GetActorLocation();
            const FVector Step = (Routes->NextPoint() - Position).GetSafeNormal2D() * 30.0;
            Character->SetActorLocation(Position + Step, true);
            TestTrue(TEXT("The pawn moves along the active route"), FVector::Dist2D(Position, Character->GetActorLocation()) > 1.0);
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.2f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, DrawCount]()
        {
            TestTrue(TEXT("Moving the pawn redraws the stationary trail"),
                Routes->GetTrailDrawCountForTests() > *DrawCount && Routes->IsGuiding());
            *DrawCount = Routes->GetTrailDrawCountForTests();
            Routes->SetReducedMotion(false);
            return true;
        }));
        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Routes, Trail, DrawCount]()
        {
            TestTrue(TEXT("Turning animation back on resumes redraws"), Routes->GetTrailDrawCountForTests() > *DrawCount);
            Routes->Stop();
            TestTrue(TEXT("Stopping guidance clears the trail"), Trail->BatchedLines.IsEmpty());
            return true;
        }));
        return true;
    }));
    return true;
}

#endif
