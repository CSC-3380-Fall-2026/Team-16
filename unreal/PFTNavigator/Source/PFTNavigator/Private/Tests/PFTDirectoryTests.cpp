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
    // The directory loads departments.json and faculty.json too now, so the test folder needs both or loading fails
    const FString Department = TEXT("[{\"Name\":\"DEPT_CSE\",\"DisplayName\":\"Computer Science and Engineering\",\"OfficeRoomId\":\"PFT_1200\"}]");
    const FString Professor = TEXT("[{\"Name\":\"FAC_TIGER\",\"FullName\":\"Mike Tiger\",\"DepartmentId\":\"DEPT_CSE\",\"OfficeRoomId\":\"PFT_1200\"}]");
    auto WriteRooms = [&](const FString& Text) { FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("rooms.json"))); };
    WriteRooms(TEXT("[") + Room + TEXT("]"));
    FFileHelper::SaveStringToFile(Course, *(Folder / TEXT("courses.json")));
    FFileHelper::SaveStringToFile(Department, *(Folder / TEXT("departments.json")));
    FFileHelper::SaveStringToFile(Professor, *(Folder / TEXT("faculty.json")));
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

    // Department search. A match should give the department office first, then each professor in it
    const auto DepartmentResults = Directory->Search(TEXT("computer science"));
    TestEqual(TEXT("Department name search finds office and professor"), DepartmentResults.Num(), 2);
    if (DepartmentResults.Num() == 2)
    {
        TestEqual(TEXT("Department office is first"), DepartmentResults[0].Label, FString(TEXT("Computer Science and Engineering - Department Office")));
        TestEqual(TEXT("Professor label shows office"), DepartmentResults[1].Label, FString(TEXT("Mike Tiger - Office 1200")));
        TestEqual(TEXT("Professor goes to their office"), DepartmentResults[1].RoomId, FString(TEXT("PFT_1200")));
    }
    TestEqual(TEXT("Department code search"), Directory->Search(TEXT("CSE")).Num(), 2);
    TestEqual(TEXT("Department code has to match exactly"), Directory->Search(TEXT("se")).Num(), 0);

    // Room details should list everything we know about the room, and nothing for a room we don't have
    const FString Details = Directory->GetRoomDetails(TEXT("PFT_1200"));
    TestTrue(TEXT("Details show type and floor"), Details.Contains(TEXT("Lecture | Floor 1")));
    TestTrue(TEXT("Details show department office"), Details.Contains(TEXT("Department office: Computer Science and Engineering")));
    TestTrue(TEXT("Details show professor"), Details.Contains(TEXT("Office of Mike Tiger")));
    TestTrue(TEXT("Details show class"), Details.Contains(TEXT("Class here: CSC 3380")));
    TestTrue(TEXT("No details for unknown room"), Directory->GetRoomDetails(TEXT("PFT_9999")).IsEmpty());

    // A professor in a department that doesn't exist should fail the load, then put the good file back
    FFileHelper::SaveStringToFile(Professor.Replace(TEXT("DEPT_CSE"), TEXT("DEPT_NOPE")), *(Folder / TEXT("faculty.json")));
    TestFalse(TEXT("Professor with unknown department rejected"), Directory->LoadDirectory(Folder));
    FFileHelper::SaveStringToFile(Department.Replace(TEXT("PFT_1200"), TEXT("PFT_9999")), *(Folder / TEXT("departments.json")));
    TestFalse(TEXT("Department with unknown office rejected"), Directory->LoadDirectory(Folder));
    FFileHelper::SaveStringToFile(Professor, *(Folder / TEXT("faculty.json")));
    FFileHelper::SaveStringToFile(Department, *(Folder / TEXT("departments.json")));
    TestTrue(TEXT("Loads again with good files"), Directory->LoadDirectory(Folder));

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
