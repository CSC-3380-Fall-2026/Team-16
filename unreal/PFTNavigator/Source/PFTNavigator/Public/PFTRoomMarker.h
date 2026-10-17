#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFTRoomMarker.generated.h"

class UTextRenderComponent;

UCLASS()
class PFTNAVIGATOR_API APFTRoomMarker : public AActor
{
    GENERATED_BODY()
public:
    APFTRoomMarker();
    virtual void Tick(float DeltaTime) override;
    void SetSelected(bool bSelected);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString RoomId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
};
