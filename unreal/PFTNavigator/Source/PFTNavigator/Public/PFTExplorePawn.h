#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "PFTExplorePawn.generated.h"

UCLASS()
class PFTNAVIGATOR_API APFTExplorePawn : public ASpectatorPawn
{
    GENERATED_BODY()
public:
    APFTExplorePawn();
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    void ToggleMouse();
};
