#include "PFTDirectory.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    template<typename Row>
    bool ReadRows(const FString& Path, const TArray<FString>& Fields, TArray<Row>& Rows, FString& Error)
    {
        FString Text;
        TArray<TSharedPtr<FJsonValue>> Values;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Values))
        {
            Error = TEXT("Cannot read JSON array: ") + Path;
            return false;
        }
        for (const auto& Value : Values)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object)
            {
                Error = TEXT("Expected an object in ") + Path;
                return false;
            }
            const auto Object = Value->AsObject();
            for (const FString& Field : Fields)
            {
                FString FieldValue;
                if (!Object->HasTypedField<EJson::String>(Field) || !Object->TryGetStringField(Field, FieldValue) || FieldValue.IsEmpty() || FieldValue != FieldValue.TrimStartAndEnd())
                {
                    Error = TEXT("Invalid ") + Field + TEXT(" in ") + Path;
                    return false;
                }
            }
            if constexpr (std::is_same_v<Row, FPFTRoom>)
            {
                double Floor;
                if (!Object->HasTypedField<EJson::Number>(TEXT("Floor")) || !Object->TryGetNumberField(TEXT("Floor"), Floor) || Floor < 1 || Floor > MAX_int32 || Floor != FMath::FloorToDouble(Floor))
                {
                    Error = TEXT("Invalid Floor in ") + Path;
                    return false;
                }
            }
            Row Entry;
            if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Entry))
            {
                Error = TEXT("Invalid row in ") + Path;
                return false;
            }
            Rows.Add(Entry);
        }
        return true;
    }

    // Checks if a search matches a department. Students might type part of the full name ("Computer Science")
    // or the short code ("CSE"). The code has to match exactly since short searches like "c" would match almost every code
    // Params: Department - the department to check, Text - the trimmed search text
    // Returns: true if the department should show up in the results
    bool DepartmentMatches(const FPFTDepartment& Department, const FString& Text)
    {
        const FString Code = Department.Name.Replace(TEXT("DEPT_"), TEXT(""));
        return Department.DisplayName.Contains(Text) || Code.Equals(Text, ESearchCase::IgnoreCase);
    }
}

void UPFTDirectory::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("PFTNavigator"));
    if (Plugin.IsValid()) LoadDirectory(Plugin->GetContentDir() / TEXT("Data"));
}

bool UPFTDirectory::LoadDirectory(const FString& Folder)
{
    TArray<FPFTRoom> NewRooms;
    TArray<FPFTCourse> NewCourses;
    TArray<FPFTDepartment> NewDepartments;
    TArray<FPFTFaculty> NewFaculty;
    Error.Empty();
    if (!ReadRows(Folder / TEXT("rooms.json"), { TEXT("Name"), TEXT("RoomNumber"), TEXT("DisplayName"), TEXT("RoomType") }, NewRooms, Error)) return false;
    if (!ReadRows(Folder / TEXT("courses.json"), { TEXT("Name"), TEXT("CourseCode"), TEXT("CourseTitle"), TEXT("DepartmentId"), TEXT("RoomId") }, NewCourses, Error)) return false;
    if (!ReadRows(Folder / TEXT("departments.json"), { TEXT("Name"), TEXT("DisplayName"), TEXT("OfficeRoomId") }, NewDepartments, Error)) return false;
    if (!ReadRows(Folder / TEXT("faculty.json"), { TEXT("Name"), TEXT("FullName"), TEXT("DepartmentId"), TEXT("OfficeRoomId") }, NewFaculty, Error)) return false;
    TSet<FString> RoomIds, RoomNumbers, CourseIds, DepartmentIds, FacultyIds;
    for (const auto& Room : NewRooms)
    {
        if (Room.Floor < 1 || RoomIds.Contains(Room.Name) || RoomNumbers.Contains(Room.RoomNumber))
        {
            Error = TEXT("Invalid or duplicate room: ") + Room.Name;
            return false;
        }
        RoomIds.Add(Room.Name);
        RoomNumbers.Add(Room.RoomNumber);
    }
    for (const auto& Course : NewCourses)
    {
        if (!RoomIds.Contains(Course.RoomId) || CourseIds.Contains(Course.Name))
        {
            Error = TEXT("Invalid room reference or duplicate course: ") + Course.Name;
            return false;
        }
        CourseIds.Add(Course.Name);
    }
    // Same idea as the courses check. A department's office has to be a real room
    // or its search result would send you nowhere
    for (const auto& Department : NewDepartments)
    {
        if (!RoomIds.Contains(Department.OfficeRoomId) || DepartmentIds.Contains(Department.Name))
        {
            Error = TEXT("Invalid office room or duplicate department: ") + Department.Name;
            return false;
        }
        DepartmentIds.Add(Department.Name);
    }
    // Each professor needs a real office room and a real department,
    // since department searches list professors and send you to their office
    for (const auto& Professor : NewFaculty)
    {
        if (!RoomIds.Contains(Professor.OfficeRoomId) || !DepartmentIds.Contains(Professor.DepartmentId) || FacultyIds.Contains(Professor.Name))
        {
            Error = TEXT("Invalid office, invalid department, or duplicate faculty: ") + Professor.Name;
            return false;
        }
        FacultyIds.Add(Professor.Name);
    }
    Rooms = MoveTemp(NewRooms);
    Courses = MoveTemp(NewCourses);
    Departments = MoveTemp(NewDepartments);
    Faculty = MoveTemp(NewFaculty);
    return true;
}

const FPFTRoom* UPFTDirectory::FindRoom(const FString& RoomId) const
{
    return Rooms.FindByPredicate([&](const FPFTRoom& Room) { return Room.Name == RoomId; });
}

TArray<FPFTSearchResult> UPFTDirectory::Search(const FString& Query) const
{
    const FString Text = Query.TrimStartAndEnd();
    TArray<FPFTSearchResult> Results;
    if (Text.IsEmpty()) return Results;
    for (const auto& Room : Rooms)
    {
        if (Room.Name.Contains(Text) || Room.RoomNumber.Contains(Text) || Room.DisplayName.Contains(Text))
            Results.Add({ Room.Name, Room.DisplayName });
    }
    for (const auto& Course : Courses)
    {
        if (Course.CourseCode.Contains(Text) || Course.CourseTitle.Contains(Text) || Course.Name.Contains(Text))
            Results.Add({ Course.RoomId, Course.CourseCode + TEXT(" - ") + Course.CourseTitle });
    }
    // Departments match on their full name or their short code. The first result is the department office,
    // then everyone in that department so you can find a professor without knowing their name
    for (const auto& Department : Departments)
    {
        if (!DepartmentMatches(Department, Text)) continue;
        Results.Add({ Department.OfficeRoomId, Department.DisplayName + TEXT(" - Department Office") });
        AddDepartmentFaculty(Department.Name, Results);
    }
    return Results;
}

// Adds every professor in a department to the search results, each one pointing at their office
// Params: DepartmentId - the department to list (like DEPT_CSE), Results - the search results to add to
// Result: one result per professor labeled with their name and office number
void UPFTDirectory::AddDepartmentFaculty(const FString& DepartmentId, TArray<FPFTSearchResult>& Results) const
{
    for (const auto& Professor : Faculty)
    {
        if (Professor.DepartmentId != DepartmentId) continue;
        // LoadDirectory already checked every office is a real room, this is just to be safe
        const FPFTRoom* Office = FindRoom(Professor.OfficeRoomId);
        const FString OfficeNumber = Office ? Office->RoomNumber : Professor.OfficeRoomId;
        Results.Add({ Professor.OfficeRoomId, Professor.FullName + TEXT(" - Office ") + OfficeNumber });
    }
}
