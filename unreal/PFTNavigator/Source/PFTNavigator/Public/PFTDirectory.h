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
    UPROPERTY(BlueprintReadOnly) TArray<FPFTRoom> Rooms;
    UPROPERTY(BlueprintReadOnly) TArray<FPFTCourse> Courses;
    UPROPERTY(BlueprintReadOnly) FString Error;
};
