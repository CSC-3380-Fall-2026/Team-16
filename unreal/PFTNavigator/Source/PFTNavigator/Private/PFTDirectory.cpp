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
    Error.Empty();
    if (!ReadRows(Folder / TEXT("rooms.json"), { TEXT("Name"), TEXT("RoomNumber"), TEXT("DisplayName"), TEXT("RoomType") }, NewRooms, Error)) return false;
    if (!ReadRows(Folder / TEXT("courses.json"), { TEXT("Name"), TEXT("CourseCode"), TEXT("CourseTitle"), TEXT("DepartmentId"), TEXT("RoomId") }, NewCourses, Error)) return false;
    TSet<FString> RoomIds, RoomNumbers, CourseIds;
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
    Rooms = MoveTemp(NewRooms);
    Courses = MoveTemp(NewCourses);
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
    return Results;
}
