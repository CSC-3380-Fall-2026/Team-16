#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "PFTViewportTest.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace
{
    UTextBlock* FindRoomHud(APlayerController* Player)
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Player, Widgets, UUserWidget::StaticClass(), true);
        for (const auto* Widget : Widgets)
        {
            if (!Widget->WidgetTree) continue;
            if (auto* Room = Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("Room")))) return Room;
        }
        return nullptr;
    }

    class FWalkFirstFloor : public IAutomationLatentCommand
    {
    public:
        FWalkFirstFloor(FAutomationTestBase& InTest, APlayerController* InPlayer, ACharacter* InCharacter,
            TArray<FVector2D> InWaypoints, double InTimeLimit, UTextBlock* InRoomHud, const FString& InDestination)
            : Test(InTest), Player(InPlayer), Character(InCharacter), Waypoints(MoveTemp(InWaypoints)),
              TimeLimit(InTimeLimit), RoomHud(InRoomHud), Destination(InRoomHud ? InDestination : FString())
        {
            #if CSV_PROFILER
            FCsvProfiler::Get()->BeginCapture();
            #endif
        }

        bool Update() override
        {
            #if CSV_PROFILER
            if (Capture.IsValid()) return Capture.IsReady();
            #endif

            const FVector Position = Character.IsValid() ? Character->GetActorLocation() : FVector::ZeroVector;
            if (!Player.IsValid() || !Character.IsValid() || Player->GetPawn() != Character.Get())
            {
                return Finish(TEXT("The walking pawn changed during the route"));
            }
            if (FPlatformTime::Seconds() - StartTime > TimeLimit)
            {
                return Finish(FString::Printf(TEXT("Timed out at %s on the way to point %d"), *Position.ToCompactString(), NextPoint + 1));
            }
            if (Position.Z < 65.0)
            {
                return Finish(FString::Printf(TEXT("The pawn fell below the first floor at %s"), *Position.ToCompactString()));
            }

            FVector2D Delta = Waypoints[NextPoint] - FVector2D(Position);
            if (Delta.Size() <= 20.0)
            {
                ++NextPoint;
                if (NextPoint == Waypoints.Num())
                {
                    if (!Character->GetCharacterMovement()->IsMovingOnGround())
                    {
                        return Finish(TEXT("The route ended off the floor"));
                    }
                    if (!Destination.IsEmpty())
                    {
                        const FString RoomNumber = RoomHud.IsValid() ? RoomHud->GetText().ToString() : FString();
                        Test.TestEqual(TEXT("The HUD shows the destination hallway"), RoomNumber, Destination);
                    }
                    Test.AddInfo(FString::Printf(TEXT("Reached all %d route points in %.2f seconds"),
                        Waypoints.Num(), FPlatformTime::Seconds() - StartTime));
                    return Finish(FString());
                }
                Delta = Waypoints[NextPoint] - FVector2D(Position);
            }

            const FVector Direction(Delta.X, Delta.Y, 0);
            Player->SetControlRotation(FMath::RInterpTo(Player->GetControlRotation(), Direction.Rotation(),
                Character->GetWorld()->GetDeltaSeconds(), 7.0));
            Character->AddMovementInput(Direction.GetSafeNormal2D(), FMath::Clamp(Delta.Size() / 120.0, 0.25, 1.0));
            return false;
        }

    private:
        bool Finish(const FString& Error)
        {
            if (!Error.IsEmpty()) Test.AddError(Error);
            #if CSV_PROFILER
            Capture = FCsvProfiler::Get()->EndCapture();
            return false;
            #else
            return true;
            #endif
        }

        FAutomationTestBase& Test;
        TWeakObjectPtr<APlayerController> Player;
        TWeakObjectPtr<ACharacter> Character;
        const TArray<FVector2D> Waypoints;
        const double TimeLimit;
        TWeakObjectPtr<UTextBlock> RoomHud;
        const FString Destination;
        double StartTime = FPlatformTime::Seconds();
        int32 NextPoint = 0;
        #if CSV_PROFILER
        TSharedFuture<FString> Capture;
        #endif
    };
    const TArray<FVector2D> Zone1200Waypoints = {
        {10300, -6280}, {10300, -6400}, {10660, -6400}, {10660, -6640}, {11860, -6640}, {11860, -6520}
    };
    const TArray<FVector2D> CommonsWaypoints = {{14020, -5720}, {14820, -5160}, {14900, -5160}};

    bool StartWalk(FAutomationTestBase& Test, TArray<FVector2D> Waypoints, double TimeLimit, const FString& Destination = FString())
    {
        UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        FString ExpectedMap = TEXT("/Game/Level/PFTFirstFloorComplete");
        FParse::Value(FCommandLine::Get(), TEXT("PFTWalkMap="), ExpectedMap);
        if (!World || World->GetPackage()->GetName() != ExpectedMap)
        {
            Test.AddError(FString::Printf(TEXT("Start %s before running this route"), *ExpectedMap));
            return false;
        }
        APlayerController* Player = World->GetFirstPlayerController();
        ACharacter* Character = Player ? Player->GetCharacter() : nullptr;
        if (!Test.TestNotNull(TEXT("First-floor walking pawn"), Character)) return false;
        if (!Test.TestTrue(TEXT("Skip tour is clickable before walking"), PFTViewportTest::SkipWelcome(World))) return false;
        if (!Test.TestFalse(TEXT("Walking is unlocked after Skip tour"), Player->IsMoveInputIgnored())) return false;
        if (!Test.TestFalse(TEXT("Looking is unlocked after Skip tour"), Player->IsLookInputIgnored())) return false;
        if (!Test.TestTrue(TEXT("The route starts at the C1003 spawn"),
            FVector::Dist2D(Character->GetActorLocation(), FVector(9714, -6296, 101)) <= 50.0))
        {
            return false;
        }
        UTextBlock* RoomHud = nullptr;
        if (!Destination.IsEmpty() && World->GetWorldSettings()->ActorHasTag(TEXT("PFTLocalLocation")))
        {
            RoomHud = FindRoomHud(Player);
            if (!Test.TestNotNull(TEXT("First-floor room HUD"), RoomHud)) return false;
            Test.TestEqual(TEXT("The entrance HUD shows C1003"), RoomHud->GetText().ToString(), FString(TEXT("C1003")));
        }
        ADD_LATENT_AUTOMATION_COMMAND(FWalkFirstFloor(Test, Player, Character, MoveTemp(Waypoints), TimeLimit, RoomHud, Destination));
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTFirstFloorWalkTest, "PFTFirstFloor.WalkPastZone1200",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter)

bool FPFTFirstFloorWalkTest::RunTest(const FString& Parameters)
{
    return StartWalk(*this, Zone1200Waypoints, 30.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTCommonsWalkTest, "PFTFirstFloor.WalkToCommons",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter)

bool FPFTCommonsWalkTest::RunTest(const FString& Parameters)
{
    TArray<FVector2D> Waypoints = Zone1200Waypoints;
    Waypoints.Append(CommonsWaypoints);
    return StartWalk(*this, MoveTemp(Waypoints), 45.0, TEXT("C1008"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTCrossBuildingWalkTest, "PFTFirstFloor.WalkToC1007",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter)

bool FPFTCrossBuildingWalkTest::RunTest(const FString& Parameters)
{
    TArray<FVector2D> Waypoints = Zone1200Waypoints;
    Waypoints.Append(CommonsWaypoints);
    Waypoints.Append({ {13700, -600}, {13700, 200}, {13380, 1080}, {12980, 920} });
    return StartWalk(*this, MoveTemp(Waypoints), 65.0, TEXT("C1007"));
}

#endif
