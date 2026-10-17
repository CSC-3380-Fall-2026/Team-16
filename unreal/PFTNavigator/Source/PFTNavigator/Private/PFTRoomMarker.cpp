#include "PFTRoomMarker.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

APFTRoomMarker::APFTRoomMarker()
{
    PrimaryActorTick.bCanEverTick = true;
    auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Mesh->SetStaticMesh(Sphere.Object);
    Mesh->SetRelativeScale3D(FVector(0.4));
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("RoomLabel"));
    Label->SetupAttachment(Mesh);
    Label->SetRelativeLocation(FVector(0, 0, 150));
    Label->SetWorldSize(70);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetTextRenderColor(FColor::Yellow);
    SetSelected(false);
}

void APFTRoomMarker::SetSelected(bool bSelected)
{
    SetActorHiddenInGame(!bSelected);
    SetActorTickEnabled(bSelected);
    Label->SetText(FText::FromString(RoomId));
}

void APFTRoomMarker::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (const auto* Player = GetWorld()->GetFirstPlayerController())
    {
        FVector Position;
        FRotator Rotation;
        Player->GetPlayerViewPoint(Position, Rotation);
        Label->SetWorldRotation((Position - Label->GetComponentLocation()).Rotation());
    }
}
