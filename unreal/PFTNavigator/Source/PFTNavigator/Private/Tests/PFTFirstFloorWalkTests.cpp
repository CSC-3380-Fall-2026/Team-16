#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace
{
    class FWalkPastZone1200 : public IAutomationLatentCommand
    {
    public:
        FWalkPastZone1200(FAutomationTestBase& InTest, APlayerController* InPlayer, ACharacter* InCharacter)
            : Test(InTest), Player(InPlayer), Character(InCharacter)
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
            if (FPlatformTime::Seconds() - StartTime > 30.0)
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
                    Test.AddInfo(FString::Printf(TEXT("Reached all %d route points in %.2f seconds"),
                        Waypoints.Num(), FPlatformTime::Seconds() - StartTime));
                    return Finish(FString());
                }
                Delta = Waypoints[NextPoint] - FVector2D(Position);
            }

            const FVector Direction(Delta.X, Delta.Y, 0);
            Player->SetControlRotation(FMath::RInterpTo(Player->GetControlRotation(), Direction.Rotation(),
                Character->GetWorld()->GetDeltaSeconds(), 7.0));
            Character->AddMovementInput(Direction.GetSafeNormal2D(), FMath::Clamp(Delta.Size() / 120.0, 0.25, 1.0), true);
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
        // Walk from the demo spawn, turn past the Zone 1200 sign, and enter the hallway.
        const TArray<FVector2D> Waypoints = {
            {10300, -6280}, {10300, -6400}, {10660, -6400}, {10660, -6640}, {11860, -6640}, {11860, -6520}
        };
        double StartTime = FPlatformTime::Seconds();
        int32 NextPoint = 0;
        #if CSV_PROFILER
        TSharedFuture<FString> Capture;
        #endif
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTFirstFloorWalkTest, "PFTFirstFloor.WalkPastZone1200",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter)

bool FPFTFirstFloorWalkTest::RunTest(const FString& Parameters)
{
    UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    if (!World || World->GetPackage()->GetName() != TEXT("/Game/Level/PFTFirstFloorComplete"))
    {
        AddError(TEXT("Start the standalone first-floor demo before running this route"));
        return false;
    }
    APlayerController* Player = World->GetFirstPlayerController();
    ACharacter* Character = Player ? Player->GetCharacter() : nullptr;
    if (!TestNotNull(TEXT("First-floor walking pawn"), Character)) return false;
    if (!TestTrue(TEXT("The route starts at the C1003 spawn"),
        FVector::Dist2D(Character->GetActorLocation(), FVector(9714, -6296, 101)) <= 50.0))
    {
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FWalkPastZone1200(*this, Player, Character));
    return true;
}

#endif
