#include "PFTDirectory.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTDirectoryTest, "PFTNavigator.Directory.LoadAndSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPFTDirectoryTest::RunTest(const FString& Parameters)
{
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/PFTDirectory") / FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Folder, true);
    const FString Room = TEXT("{\"Name\":\"PFT_1200\",\"RoomNumber\":\"1200\",\"DisplayName\":\"PFT 1200\",\"Floor\":1,\"RoomType\":\"Lecture\"}");
    const FString Course = TEXT("[{\"Name\":\"CSC_3380\",\"CourseCode\":\"CSC 3380\",\"CourseTitle\":\"Object-Oriented Design\",\"DepartmentId\":\"CSE\",\"RoomId\":\"PFT_1200\"}]");
    auto WriteRooms = [&](const FString& Text) { FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("rooms.json"))); };
    WriteRooms(TEXT("[") + Room + TEXT("]"));
    FFileHelper::SaveStringToFile(Course, *(Folder / TEXT("courses.json")));
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UPFTDirectory* Directory = NewObject<UPFTDirectory>(GameInstance);
    TestTrue(TEXT("Load valid data"), Directory->LoadDirectory(Folder));
    TestEqual(TEXT("Course search ignores case and outer spaces"), Directory->Search(TEXT(" csc 3380 ")).Num(), 1);
    const auto Results = Directory->Search(TEXT("Design"));
    TestEqual(TEXT("Title search"), Results.Num(), 1);
    if (!Results.IsEmpty()) TestEqual(TEXT("Course resolves to room"), Results[0].RoomId, FString(TEXT("PFT_1200")));
    TestEqual(TEXT("Room search"), Directory->Search(TEXT("1200")).Num(), 1);
    TestEqual(TEXT("Empty search"), Directory->Search(TEXT("  ")).Num(), 0);
    TestEqual(TEXT("Missing search"), Directory->Search(TEXT("missing")).Num(), 0);
    WriteRooms(TEXT("[") + Room + TEXT(",") + Room + TEXT("]"));
    TestFalse(TEXT("Duplicate rooms rejected"), Directory->LoadDirectory(Folder));
    TestEqual(TEXT("Failed reload keeps valid data"), Directory->Rooms.Num(), 1);
    WriteRooms(TEXT("[") + Room.Replace(TEXT("\"Floor\":1"), TEXT("\"Floor\":true")) + TEXT("]"));
    TestFalse(TEXT("Boolean floor rejected"), Directory->LoadDirectory(Folder));
    WriteRooms(TEXT("[") + Room.Replace(TEXT("\"Floor\":1"), TEXT("\"Floor\":1.5")) + TEXT("]"));
    TestFalse(TEXT("Fractional floor rejected"), Directory->LoadDirectory(Folder));
    WriteRooms(TEXT("[]"));
    TestFalse(TEXT("Dangling course reference rejected"), Directory->LoadDirectory(Folder));
    WriteRooms(TEXT("[null]"));
    TestFalse(TEXT("Invalid row rejected"), Directory->LoadDirectory(Folder));
    WriteRooms(TEXT("["));
    TestFalse(TEXT("Malformed JSON rejected"), Directory->LoadDirectory(Folder));
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    return true;
}
#endif
