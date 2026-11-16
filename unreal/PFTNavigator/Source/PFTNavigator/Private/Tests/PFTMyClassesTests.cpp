#include "PFTDirectory.h"
#include "PFTNavigatorWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTMyClassesDayTest, "PFTNavigator.Directory.MyClassesDay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Checks the day and "what's next" logic with made up classes, so it doesn't depend on the real schedule
bool FPFTMyClassesDayTest::RunTest(const FString& Parameters)
{
    auto* Directory = NewObject<UPFTDirectory>(NewObject<UGameInstance>());
    FPFTRoom Room;
    Room.Name = TEXT("PFT_1200");
    Room.RoomNumber = TEXT("1200");
    Room.DisplayName = TEXT("PFT 1200");
    Directory->Rooms.Add(Room);
    auto AddCourse = [&](const TCHAR* Name, const TCHAR* Days, const TCHAR* Start, const TCHAR* End)
    {
        FPFTCourse Course;
        Course.Name = Name;
        Course.CourseCode = Name;
        Course.RoomId = Room.Name;
        Course.Instructor = TEXT("Test Professor");
        Course.MeetingDays = Days;
        Course.StartTime = Start;
        Course.EndTime = End;
        Directory->Courses.Add(Course);
    };
    AddCourse(TEXT("LATE"), TEXT("TR"), TEXT("15:00"), TEXT("16:20"));
    AddCourse(TEXT("EARLY"), TEXT("TR"), TEXT("09:00"), TEXT("10:20"));
    AddCourse(TEXT("MONDAY"), TEXT("M"), TEXT("17:00"), TEXT(""));
    AddCourse(TEXT("NO_TIME"), TEXT(""), TEXT(""), TEXT(""));

    // GONE isn't in the data anymore and EARLY is saved twice, neither should break the list
    const TArray<FString> Saved = { TEXT("LATE"), TEXT("EARLY"), TEXT("MONDAY"), TEXT("NO_TIME"), TEXT("GONE"), TEXT("EARLY") };
    const auto Tuesday = Directory->GetClassesForDay(Saved, TEXT('T'));
    if (TestEqual(TEXT("Only Tuesday classes with a time are listed"), Tuesday.Num(), 2))
    {
        TestEqual(TEXT("The earliest class is first"), Tuesday[0]->Name, FString(TEXT("EARLY")));
        TestEqual(TEXT("The later class is second"), Tuesday[1]->Name, FString(TEXT("LATE")));
    }
    TestEqual(TEXT("Thursday has the same classes"), Directory->GetClassesForDay(Saved, TEXT('R')).Num(), 2);
    TestEqual(TEXT("Nothing on Wednesday"), Directory->GetClassesForDay(Saved, TEXT('W')).Num(), 0);
    TestEqual(TEXT("Nothing saved means nothing listed"), Directory->GetClassesForDay({}, TEXT('T')).Num(), 0);

    TestEqual(TEXT("Before the first class, it's next"), UPFTDirectory::FindCurrentOrNextClass(Tuesday, TEXT("08:00")), 0);
    TestEqual(TEXT("During the first class, it's still the one"), UPFTDirectory::FindCurrentOrNextClass(Tuesday, TEXT("09:30")), 0);
    TestEqual(TEXT("Right when it ends, the next one takes over"), UPFTDirectory::FindCurrentOrNextClass(Tuesday, TEXT("10:20")), 1);
    TestEqual(TEXT("Between classes, the later one is next"), UPFTDirectory::FindCurrentOrNextClass(Tuesday, TEXT("12:00")), 1);
    TestEqual(TEXT("After the last class the day is done"), UPFTDirectory::FindCurrentOrNextClass(Tuesday, TEXT("16:30")), INDEX_NONE);
    const auto Monday = Directory->GetClassesForDay(Saved, TEXT('M'));
    TestEqual(TEXT("A class with no end time is next until it starts"), UPFTDirectory::FindCurrentOrNextClass(Monday, TEXT("17:00")), 0);
    TestEqual(TEXT("After it starts we can't tell if it's still going"), UPFTDirectory::FindCurrentOrNextClass(Monday, TEXT("17:01")), INDEX_NONE);

    // Nov 16 2026 is a Monday
    TestEqual(TEXT("Monday is M"), FString::Chr(UPFTDirectory::GetDayCode(FDateTime(2026, 11, 16))), FString(TEXT("M")));
    TestEqual(TEXT("Tuesday is T"), FString::Chr(UPFTDirectory::GetDayCode(FDateTime(2026, 11, 17))), FString(TEXT("T")));
    TestEqual(TEXT("Thursday is R"), FString::Chr(UPFTDirectory::GetDayCode(FDateTime(2026, 11, 19))), FString(TEXT("R")));
    TestEqual(TEXT("Sunday is U"), FString::Chr(UPFTDirectory::GetDayCode(FDateTime(2026, 11, 22))), FString(TEXT("U")));

    if (Tuesday.Num() == 2)
    {
        TestEqual(TEXT("Time range shows both ends"), Tuesday[1]->GetTimeRangeLabel(), FString(TEXT("3:00 PM - 4:20 PM")));
    }
    if (Monday.Num() == 1)
    {
        TestEqual(TEXT("No end time shows just the start"), Monday[0]->GetTimeRangeLabel(), FString(TEXT("5:00 PM")));
        TestTrue(TEXT("No end time has no end label"), Monday[0]->GetEndTimeLabel().IsEmpty());
    }

    // Class search results need the course so the panel can save the right one
    const auto Found = Directory->FindClasses(TEXT(""), TEXT(""), 0, TEXT(""), 0);
    TestTrue(TEXT("Class results carry their course"), Found.ContainsByPredicate([](const FPFTSearchResult& Result)
    {
        return Result.CourseId == TEXT("EARLY");
    }));
    const auto RoomResults = Directory->Search(TEXT("1200"));
    if (TestEqual(TEXT("Room search finds the room"), RoomResults.Num(), 1))
    {
        TestTrue(TEXT("Room search results don't have a course"), RoomResults[0].CourseId.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFTMyClassesPanelTest, "PFTNavigator.Panel.MyClasses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PFTMyClassesTest
{
    const TCHAR* Section = TEXT("PFTNavigator");

    UPFTNavigatorWidget* MakePanel(APlayerController* Player)
    {
        auto* Panel = CreateWidget<UPFTNavigatorWidget>(Player, UPFTNavigatorWidget::StaticClass());
        Panel->TakeWidget();
        return Panel;
    }

    // Gets the class buttons in My classes, in the order they're shown
    TArray<UPFTSearchButton*> GetToday(UPFTNavigatorWidget* Panel)
    {
        TArray<UPFTSearchButton*> Buttons;
        auto* List = Cast<UVerticalBox>(Panel->WidgetTree->FindWidget(TEXT("MyClassesList")));
        if (!List) return Buttons;
        for (auto* Child : List->GetAllChildren())
        {
            if (auto* Button = Cast<UPFTSearchButton>(Child)) Buttons.Add(Button);
        }
        return Buttons;
    }

    // Gets the search results, which are every result button that isn't in My classes
    TArray<UPFTSearchButton*> GetResults(UPFTNavigatorWidget* Panel)
    {
        const auto Today = GetToday(Panel);
        TArray<UWidget*> Widgets;
        Panel->WidgetTree->GetAllWidgets(Widgets);
        TArray<UPFTSearchButton*> Buttons;
        for (auto* Item : Widgets)
        {
            auto* Button = Cast<UPFTSearchButton>(Item);
            if (Button && !Today.Contains(Button)) Buttons.Add(Button);
        }
        return Buttons;
    }

    FString GetLabel(UWidget* Widget)
    {
        if (auto* Button = Cast<UButton>(Widget)) Widget = Button->GetContent();
        const auto* Text = Cast<UTextBlock>(Widget);
        return Text ? Text->GetText().ToString() : FString();
    }

    FString GetSaved()
    {
        FString Saved;
        GConfig->GetString(Section, TEXT("MyClasses"), Saved, GGameUserSettingsIni);
        return Saved;
    }
}

// Goes through what a student would do: find two classes, save them, check the day at a few times,
// reopen the panel to make sure they were saved, then take one off. Uses the real course data,
// same as the Selected tag test (CSC 2730 is TR 1:30-2:50 PM and CSC 4330 is TR 3:00-4:20 PM)
bool FPFTMyClassesPanelTest::RunTest(const FString& Parameters)
{
    using namespace PFTMyClassesTest;
    // Keep whatever is saved on this computer so the test doesn't wipe someone's real list
    FString Original;
    const bool bHadOriginal = GConfig->GetString(Section, TEXT("MyClasses"), Original, GGameUserSettingsIni);
    GConfig->SetString(Section, TEXT("MyClasses"), TEXT(""), GGameUserSettingsIni);

    auto* Instance = NewObject<UGameInstance>(GEngine);
    Instance->InitializeStandalone();
    auto* World = Instance->GetWorld();
    World->InitializeActorsForPlay(FURL());
    auto Cleanup = [&]()
    {
        if (bHadOriginal) GConfig->SetString(Section, TEXT("MyClasses"), *Original, GGameUserSettingsIni);
        else GConfig->RemoveKey(Section, TEXT("MyClasses"), GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
        Instance->Shutdown();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Player = World->SpawnActor<APlayerController>();
    auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    Instance->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
    Player->SetPlayer(LocalPlayer);

    // Nov 17 2026 is a Tuesday
    auto* Panel = MakePanel(Player);
    Panel->SetClockForTests(FDateTime(2026, 11, 17, 14, 0, 0));
    auto* Tree = Panel->WidgetTree.Get();
    auto* Status = Tree->FindWidget(TEXT("MyClassesStatus"));
    auto* SaveButton = Cast<UButton>(Tree->FindWidget(TEXT("SaveClassButton")));
    auto* ClassFilter = Cast<UCheckBox>(Tree->FindWidget(TEXT("ClassSearchFilter")));
    auto* Instructor = Cast<UEditableTextBox>(Tree->FindWidget(TEXT("InstructorSearch")));
    if (!TestNotNull(TEXT("My classes status exists"), Status) || !TestNotNull(TEXT("Save button exists"), SaveButton)
        || !TestNotNull(TEXT("Class search exists"), ClassFilter) || !TestNotNull(TEXT("Instructor box exists"), Instructor))
    {
        Cleanup();
        return false;
    }
    TestEqual(TEXT("Nothing saved at the start"), GetToday(Panel).Num(), 0);
    TestTrue(TEXT("Empty list explains how to add a class"), GetLabel(Status).StartsWith(TEXT("No saved classes yet")));
    TestTrue(TEXT("Save button is hidden until a class is picked"), SaveButton->GetVisibility() == ESlateVisibility::Collapsed);

    // Find a class by instructor and pick the one result, the same way a person would
    ClassFilter->SetIsChecked(true);
    ClassFilter->OnCheckStateChanged.Broadcast(true);
    auto PickClass = [&](const TCHAR* Name)
    {
        Instructor->SetText(FText::FromString(Name));
        Instructor->OnTextChanged.Broadcast(Instructor->GetText());
        const auto Results = GetResults(Panel);
        if (!TestEqual(*FString::Printf(TEXT("%s has one class"), Name), Results.Num(), 1)) return false;
        Results[0]->OnClicked.Broadcast();
        return true;
    };
    if (!PickClass(TEXT("Shepherd")))
    {
        Cleanup();
        return false;
    }
    TestTrue(TEXT("Picking a class shows the save button"), SaveButton->GetVisibility() == ESlateVisibility::Visible);
    TestEqual(TEXT("It offers to add the class"), GetLabel(SaveButton), FString(TEXT("Add to my classes")));
    SaveButton->OnClicked.Broadcast();
    TestEqual(TEXT("Once saved it offers to remove it"), GetLabel(SaveButton), FString(TEXT("Remove from my classes")));
    if (!PickClass(TEXT("Mills")))
    {
        Cleanup();
        return false;
    }
    SaveButton->OnClicked.Broadcast();
    TestEqual(TEXT("Both classes are saved to settings"), GetSaved(), FString(TEXT("CSC_4330,CSC_2730")));

    // 2:00 PM: CSC 2730 is going on and CSC 4330 is after it, even though 4330 was saved first
    auto Today = GetToday(Panel);
    if (TestEqual(TEXT("Both classes meet on Tuesday"), Today.Num(), 2))
    {
        TestTrue(TEXT("The class going on now is first"), Today[0]->ResultText.StartsWith(TEXT("Now | CSC 2730")));
        TestTrue(TEXT("The later class is second"), Today[1]->ResultText.StartsWith(TEXT("CSC 4330")));
        TestEqual(TEXT("My classes buttons know their course"), Today[1]->CourseId, FString(TEXT("CSC_4330")));
    }
    TestTrue(TEXT("Status says what class you're in"), GetLabel(Status).StartsWith(TEXT("In class now: CSC 2730")));

    Panel->SetClockForTests(FDateTime(2026, 11, 17, 15, 30, 0));
    Today = GetToday(Panel);
    if (Today.Num() == 2)
    {
        TestTrue(TEXT("The first class is done by 3:30"), Today[0]->ResultText.StartsWith(TEXT("Done | CSC 2730")));
        TestTrue(TEXT("The second class is going on at 3:30"), Today[1]->ResultText.StartsWith(TEXT("Now | CSC 4330")));
    }
    Panel->SetClockForTests(FDateTime(2026, 11, 17, 18, 0, 0));
    TestEqual(TEXT("After the last class the day is done"), GetLabel(Status), FString(TEXT("No more classes today.")));
    Panel->SetClockForTests(FDateTime(2026, 11, 18, 10, 0, 0));
    TestEqual(TEXT("Neither class meets on Wednesday"), GetToday(Panel).Num(), 0);
    TestEqual(TEXT("Wednesday says so"), GetLabel(Status), FString(TEXT("None of your classes meet today.")));

    // A new panel should load the saved list back, like after restarting the app
    auto* Reopened = MakePanel(Player);
    Reopened->SetClockForTests(FDateTime(2026, 11, 17, 8, 0, 0));
    Today = GetToday(Reopened);
    if (!TestEqual(TEXT("The reopened panel has both classes"), Today.Num(), 2))
    {
        Cleanup();
        return false;
    }
    TestTrue(TEXT("Before class, the first one is next"), Today[0]->ResultText.StartsWith(TEXT("Next | CSC 2730")));

    // Picking a class from My classes lets you take it off the list
    Today[1]->OnClicked.Broadcast();
    auto* ReopenedSave = Cast<UButton>(Reopened->WidgetTree->FindWidget(TEXT("SaveClassButton")));
    if (TestNotNull(TEXT("The reopened panel has a save button"), ReopenedSave))
    {
        TestEqual(TEXT("A saved class offers to be removed"), GetLabel(ReopenedSave), FString(TEXT("Remove from my classes")));
        ReopenedSave->OnClicked.Broadcast();
    }
    TestEqual(TEXT("Removing leaves one class"), GetToday(Reopened).Num(), 1);
    TestEqual(TEXT("Removing updates settings"), GetSaved(), FString(TEXT("CSC_2730")));
    Cleanup();
    return true;
}
#endif
