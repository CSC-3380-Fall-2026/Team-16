#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PFTDirectory.generated.h"

USTRUCT(BlueprintType)
struct PFTNAVIGATOR_API FPFTRoom
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString RoomNumber;
    UPROPERTY(BlueprintReadOnly) FString DisplayName;
    UPROPERTY(BlueprintReadOnly) int32 Floor = 1;
    UPROPERTY(BlueprintReadOnly) FString RoomType;
};

USTRUCT(BlueprintType)
struct PFTNAVIGATOR_API FPFTCourse
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString CourseCode;
    UPROPERTY(BlueprintReadOnly) FString CourseTitle;
    UPROPERTY(BlueprintReadOnly) FString DepartmentId;
    UPROPERTY(BlueprintReadOnly) FString RoomId;
    UPROPERTY(BlueprintReadOnly) FString Instructor;
    UPROPERTY(BlueprintReadOnly) FString MeetingDays;
    UPROPERTY(BlueprintReadOnly) FString StartTime;
    // Optional, "HH:MM" like StartTime. Empty when we don't know when the class ends
    UPROPERTY(BlueprintReadOnly) FString EndTime;

    FString GetStartTimeLabel() const;
    // "11:20 AM", or empty when we don't know the end time
    FString GetEndTimeLabel() const;
    // "10:30 AM - 11:20 AM", or just the start time when there's no end time
    FString GetTimeRangeLabel() const;
    FString GetScheduleLabel() const;
};

// One department from departments.json. OfficeRoomId is the room the main department office is in
USTRUCT(BlueprintType)
struct PFTNAVIGATOR_API FPFTDepartment
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString DisplayName;
    UPROPERTY(BlueprintReadOnly) FString OfficeRoomId;
};

// One professor from faculty.json. OfficeRoomId is the room their office is in
USTRUCT(BlueprintType)
struct PFTNAVIGATOR_API FPFTFaculty
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString FullName;
    UPROPERTY(BlueprintReadOnly) FString DepartmentId;
    UPROPERTY(BlueprintReadOnly) FString OfficeRoomId;
};

USTRUCT(BlueprintType)
struct PFTNAVIGATOR_API FPFTSearchResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString RoomId;
    UPROPERTY(BlueprintReadOnly) FString Label;
    // Only filled in for class search results, so the panel knows which course was picked
    UPROPERTY(BlueprintReadOnly) FString CourseId;
};

UCLASS()
class PFTNAVIGATOR_API UPFTDirectory : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    UFUNCTION(BlueprintCallable) bool LoadDirectory(const FString& Folder);
    UFUNCTION(BlueprintCallable) TArray<FPFTSearchResult> Search(const FString& Query) const;
    UFUNCTION(BlueprintCallable) TArray<FPFTSearchResult> BrowseRooms(const FString& Query, int32 Floor = 0) const;
    TArray<FPFTSearchResult> FindClasses(const FString& Query, const FString& Instructor, TCHAR Day, const FString& StartTime, int32 Floor) const;
    const FPFTRoom* FindRoom(const FString& RoomId) const;
    // Finds a course by its Name, like "CSC_1350". Result: the course, or nullptr if it isn't in the data
    const FPFTCourse* FindCourse(const FString& CourseId) const;
    // Gets the saved classes that meet on Day (one of MTWRFSU), earliest first.
    // Params: CourseIds are the saved course Names, Day is the day letter. Result: that day's classes in order
    TArray<const FPFTCourse*> GetClassesForDay(const TArray<FString>& CourseIds, TCHAR Day) const;
    // Picks the class that's going on right now or is coming up next.
    // Params: DayClasses from GetClassesForDay, Now as "HH:MM". Result: its index, or INDEX_NONE when the day is done
    static int32 FindCurrentOrNextClass(const TArray<const FPFTCourse*>& DayClasses, const FString& Now);
    // Turns a date into the day letter the course data uses. Result: one of M T W R F S U
    static TCHAR GetDayCode(const FDateTime& Time);
    UFUNCTION(BlueprintCallable) FString GetRoomDetails(const FString& RoomId) const;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTRoom> Rooms;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTCourse> Courses;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTDepartment> Departments;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTFaculty> Faculty;
    UPROPERTY(BlueprintReadOnly) FString Error;
private:
    void AddDepartmentFaculty(const FString& DepartmentId, TArray<FPFTSearchResult>& Results) const;
};
