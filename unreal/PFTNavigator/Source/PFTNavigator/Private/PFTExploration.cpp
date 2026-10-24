#include "PFTExploration.h"
#include "PFTDestinations.h"
#include "PFTExplorePawn.h"
#include "PFTNavigatorWidget.h"
#include "PFTRoomMarker.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UPFTExploration::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPFTExploration::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPFTExploration, STATGROUP_Tickables);
}

void UPFTExploration::Tick(float DeltaTime)
{
    if (Widget || !GetWorld()->HasBegunPlay() || IsRunningDedicatedServer()) return;
    auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Player || !Player->IsLocalPlayerController()) return;
    Widget = CreateWidget<UPFTNavigatorWidget>(Player, UPFTNavigatorWidget::StaticClass());
    Widget->AddToViewport(20);
    Widget->SetPositionInViewport(FVector2D(24, 108));
    Widget->SetDesiredSizeInViewport(FVector2D(360, 500));
    Player->bShowMouseCursor = true;
    Player->SetInputMode(FInputModeGameAndUI());
}

bool UPFTExploration::BeginExplore()
{
    if (IsValid(ExplorePawn)) return true;
    auto* Player = GetWorld()->GetFirstPlayerController();
    Error = TEXT("Exploration needs a local standalone player with a pawn");
    if (!Player || !Player->IsLocalController() || !Player->HasAuthority() || !Player->GetPawn()) return false;
    FVector Position;
    FRotator Rotation;
    Player->GetPlayerViewPoint(Position, Rotation);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ExplorePawn = GetWorld()->SpawnActor<APFTExplorePawn>(Position, Rotation, Spawn);
    if (!ExplorePawn) return false;
    PreviousPawn = Player->GetPawn();
    PreviousRotation = Player->GetControlRotation();
    bPreviousCursor = Player->bShowMouseCursor;
    Player->Possess(ExplorePawn);
    if (Player->GetPawn() != ExplorePawn)
    {
        ExplorePawn->Destroy();
        ExplorePawn = nullptr;
        PreviousPawn = nullptr;
        return false;
    }
    Player->SetControlRotation(Rotation);
    SetCursor(false);
    Error.Empty();
    return true;
}

bool UPFTExploration::EndExplore()
{
    Error = TEXT("The original player is unavailable; exploration is still active");
    auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Player || !IsValid(PreviousPawn) || !IsValid(ExplorePawn)) return false;
    SetCursor(false);
    Player->Possess(PreviousPawn);
    Player->SetControlRotation(PreviousRotation);
    Player->bShowMouseCursor = bPreviousCursor;
    if (bPreviousCursor) Player->SetInputMode(FInputModeGameAndUI());
    ExplorePawn->Destroy();
    ExplorePawn = nullptr;
    PreviousPawn = nullptr;
    Error.Empty();
    return true;
}

void UPFTExploration::SetCursor(bool bShow)
{
    auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Player || !IsValid(ExplorePawn)) return;
    if (bCursorShown != bShow)
    {
        Player->SetIgnoreMoveInput(bShow);
        Player->SetIgnoreLookInput(bShow);
    }
    bCursorShown = bShow;
    Player->bShowMouseCursor = bShow;
    if (bShow)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Player->SetInputMode(Mode);
    }
    else Player->SetInputMode(FInputModeGameOnly());
}

bool UPFTExploration::ViewRoom()
{
    auto* Marker = GetWorld()->GetSubsystem<UPFTDestinations>()->ActiveMarker.Get();
    Error = TEXT("Select a room with a bound location first");
    if (!IsValid(Marker) || !BeginExplore()) return false;
    const FVector Position = Marker->GetActorLocation() + FVector(-500, -500, 250);
    ExplorePawn->SetActorLocation(Position);
    GetWorld()->GetFirstPlayerController()->SetControlRotation((Marker->GetActorLocation() - Position).Rotation());
    SetCursor(true);
    Error.Empty();
    return true;
}

void UPFTExploration::Deinitialize()
{
    SetCursor(false);
    if (Widget) Widget->RemoveFromParent();
    Super::Deinitialize();
}
