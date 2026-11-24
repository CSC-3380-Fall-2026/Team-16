#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTNavigatorWidget.h"
#include "PFTRoutes.h"
#include "PFTViewportTest.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

namespace
{
    bool CheckPreviewReuse(FAutomationTestBase& Test, ACharacter& Character, UPFTRoutes& Routes)
    {
        const FString Room = TEXT("PFT_COMMONS");
        const FString Name = TEXT("The Commons");
        TArray<double> FreshTimes;
        TArray<double> PreviewedTimes;
        uint32 PreviewedBuilds = 0;
        TArray<FVector> Preview;
        FString Estimate;
        for (int32 Index = 0; Index < 5; ++Index)
        {
            double StartedAt = FPlatformTime::Seconds();
            const bool bFresh = Routes.Start(Character, Room, Name);
            FreshTimes.Add((FPlatformTime::Seconds() - StartedAt) * 1000.0);
            if (!Test.TestTrue(TEXT("Guidance starts without previewing"), bFresh)) return false;
            Routes.Stop();

            if (!Test.TestTrue(TEXT("The Commons route can be previewed"), Routes.Preview(Character, Room, Preview, Estimate))) return false;
            const uint32 Before = Routes.GetPathBuildCountForTests();
            StartedAt = FPlatformTime::Seconds();
            const bool bStarted = Routes.Start(Character, Room, Name);
            PreviewedTimes.Add((FPlatformTime::Seconds() - StartedAt) * 1000.0);
            if (!Test.TestTrue(TEXT("Guidance starts from the preview"), bStarted)) return false;
            PreviewedBuilds += Routes.GetPathBuildCountForTests() - Before;
            const auto Remaining = Routes.GetRemainingPath();
            Test.TestTrue(TEXT("Guidance keeps the previewed destination"),
                !Remaining.IsEmpty() && Remaining.Last().Equals(Preview.Last()));
            Routes.Stop();
        }
        FreshTimes.Sort();
        PreviewedTimes.Sort();
        Test.AddInfo(FString::Printf(TEXT("Commons median Start without preview: %.2f ms, after preview: %.2f ms"),
            FreshTimes[2], PreviewedTimes[2]));
        Test.TestEqual(TEXT("Starting a valid preview skips a full route search"), PreviewedBuilds, 0u);

        if (!Test.TestTrue(TEXT("The Commons preview refreshes"),
            Routes.Preview(Character, Room, Preview, Estimate) && Preview.Num() > 1)) return false;
        const FVector Position = Character.GetActorLocation();
        const FVector Ahead = (Preview[1] - Position).GetSafeNormal2D() * 45.0;
        Character.SetActorLocation(Position + Ahead, true);
        if (!Test.TestTrue(TEXT("The starting position changes"),
            FVector::Dist2D(Position, Character.GetActorLocation()) > 1.0)) return false;
        const uint32 Before = Routes.GetPathBuildCountForTests();
        const bool bMoved = Routes.Start(Character, Room, Name);
        Test.TestTrue(TEXT("The route starts from the new position"), bMoved);
        Test.TestEqual(TEXT("Moving after a preview triggers a fresh route search"), Routes.GetPathBuildCountForTests(), Before + 1);
        Routes.Stop();
        return bMoved;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTPreviewRouteTest, "PFTFirstFloor.ReuseRoutePreview",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPFTPreviewRouteTest::RunTest(const FString& Parameters)
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
        if (!CheckPreviewReuse(*this, *Character, *Routes)) return true;

        TArray<FVector> Preview;
        FString Estimate;
        if (!TestTrue(TEXT("Room 1218 has a preview"), Routes->Preview(*Character, TEXT("PFT_1218"), Preview, Estimate))) return true;
        auto* Blocker = World->SpawnActor<AActor>();
        if (!TestNotNull(TEXT("Temporary doorway blocker"), Blocker)) return true;
        auto* Box = NewObject<UBoxComponent>(Blocker);
        Box->InitBoxExtent(FVector(40, 40, 120));
        Box->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
        Blocker->SetRootComponent(Box);
        Box->RegisterComponent();
        Blocker->SetActorLocation(Preview.Last() + FVector(0, 0, 100));

        ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Character, Routes, Blocker]()
        {
            const uint32 Before = Routes->GetPathBuildCountForTests();
            TestFalse(TEXT("A newly blocked doorway rejects the preview route"),
                Routes->Start(*Character, TEXT("PFT_1218"), TEXT("PFT 1218")));
            TestEqual(TEXT("A blocked preview triggers a fresh route search"), Routes->GetPathBuildCountForTests(), Before + 1);
            TestTrue(TEXT("A blocked route can be retried"), Routes->CanRetry());
            Blocker->Destroy();
            Routes->Stop();
            return true;
        }));
        return true;
    }));
    return true;
}

#endif
