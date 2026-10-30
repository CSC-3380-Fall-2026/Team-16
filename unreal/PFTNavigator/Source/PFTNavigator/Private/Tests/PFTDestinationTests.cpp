#include "PFTDestinations.h"
#include "PFTRoomMarker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTDestinationTest, "PFTNavigator.Destinations.Selection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPFTDestinationTest::RunTest(const FString& Parameters)
{
    auto* Package = CreatePackage(*FString::Printf(TEXT("/Game/Automation/PFT_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(TEXT("TestWorld")), Package);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    UPFTDestinations* Destinations = World->GetSubsystem<UPFTDestinations>();
    auto* First = World->SpawnActor<APFTRoomMarker>();
    auto* Second = World->SpawnActor<APFTRoomMarker>();
    First->RoomId = TEXT("PFT_1200");
    Second->RoomId = TEXT("PFT_3325");
    // HasLocation is what the panel uses to say "Map location not available yet", so it can't guess
    TestTrue(TEXT("Placed marker has a location"), Destinations->HasLocation(First->RoomId));
    TestFalse(TEXT("Unknown room has no location"), Destinations->HasLocation(TEXT("PFT_9999")));
    TestTrue(TEXT("Select known room"), Destinations->SelectRoom(First->RoomId));
    TestFalse(TEXT("Selected marker shown"), First->IsHidden());
    TestTrue(TEXT("Select other room"), Destinations->SelectRoom(Second->RoomId));
    TestTrue(TEXT("Previous marker hidden"), First->IsHidden());
    TestFalse(TEXT("Missing room not guessed"), Destinations->SelectRoom(TEXT("PFT_9999")));
    TestTrue(TEXT("Old selection cleared on failure"), Second->IsHidden());
    auto* Duplicate = World->SpawnActor<APFTRoomMarker>();
    Duplicate->RoomId = First->RoomId;
    TestFalse(TEXT("Ambiguous markers rejected"), Destinations->SelectRoom(First->RoomId));
    const FString Path = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("PFTBindings"), TEXT(".json"));
    const FString Json = FString::Printf(TEXT("[{\"RoomId\":\"PFT_1120\",\"Map\":\"%s\",\"Location\":{\"X\":10,\"Y\":20,\"Z\":30}}]"), *Package->GetName());
    FFileHelper::SaveStringToFile(Json, *Path);
    TestFalse(TEXT("No location before bindings load"), Destinations->HasLocation(TEXT("PFT_1120")));
    TestTrue(TEXT("Load bindings"), Destinations->LoadBindings(Path));
    TestTrue(TEXT("Bound room has a location"), Destinations->HasLocation(TEXT("PFT_1120")));
    TestTrue(TEXT("Create bound marker"), Destinations->SelectRoom(TEXT("PFT_1120")));
    if (Destinations->ActiveMarker) TestEqual(TEXT("Bound location"), Destinations->ActiveMarker->GetActorLocation(), FVector(10, 20, 30));
    FFileHelper::SaveStringToFile(TEXT("[]"), *Path);
    TestTrue(TEXT("Reload bindings"), Destinations->LoadBindings(Path));
    TestFalse(TEXT("Location gone after reload"), Destinations->HasLocation(TEXT("PFT_1120")));
    TestFalse(TEXT("Reload removes obsolete generated markers"), Destinations->SelectRoom(TEXT("PFT_1120")));
    IFileManager::Get().Delete(*Path);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
