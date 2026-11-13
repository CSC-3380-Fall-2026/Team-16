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

    /** Offset from the marker to the room preview camera, in world coordinates. */
    UPROPERTY(EditAnywhere, Category = "Room") FVector ViewOffset = FVector(-500, -500, 250);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
};
