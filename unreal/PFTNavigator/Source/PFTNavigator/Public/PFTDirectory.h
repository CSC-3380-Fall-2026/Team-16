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
};

UCLASS()
class PFTNAVIGATOR_API UPFTDirectory : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    UFUNCTION(BlueprintCallable) bool LoadDirectory(const FString& Folder);
    UFUNCTION(BlueprintCallable) TArray<FPFTSearchResult> Search(const FString& Query) const;
    const FPFTRoom* FindRoom(const FString& RoomId) const;
    UFUNCTION(BlueprintCallable) FString GetRoomDetails(const FString& RoomId) const;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTRoom> Rooms;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTCourse> Courses;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTDepartment> Departments;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTFaculty> Faculty;
    UPROPERTY(BlueprintReadOnly) FString Error;
private:
    void AddDepartmentFaculty(const FString& DepartmentId, TArray<FPFTSearchResult>& Results) const;
};
