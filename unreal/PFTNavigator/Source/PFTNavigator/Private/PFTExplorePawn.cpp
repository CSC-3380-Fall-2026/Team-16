#include "PFTExplorePawn.h"
#include "PFTExploration.h"
#include "Components/InputComponent.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "InputCoreTypes.h"

APFTExplorePawn::APFTExplorePawn()
{
    auto* Movement = Cast<USpectatorPawnMovement>(GetMovementComponent());
    Movement->MaxSpeed = 650;
    Movement->Acceleration = 2400;
    Movement->Deceleration = 2400;
}

void APFTExplorePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &APFTExplorePawn::ToggleMouse);
}

void APFTExplorePawn::ToggleMouse()
{
    auto* Exploration = GetWorld()->GetSubsystem<UPFTExploration>();
    Exploration->SetCursor(!Exploration->bCursorShown);
}
