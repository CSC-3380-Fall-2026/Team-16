#include "PFTDestinations.h"
#include "PFTRoomMarker.h"
#include "EngineUtils.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

bool UPFTDestinations::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPFTDestinations::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("PFTNavigator"));
    if (Plugin.IsValid()) LoadBindings(Plugin->GetContentDir() / TEXT("Data/room_bindings.json"));
}

bool UPFTDestinations::LoadBindings(const FString& Path)
{
    FString Text;
    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<FPFTRoomBinding> NewBindings;
    Error = TEXT("Invalid room bindings");
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rows)) return false;
    TSet<FString> Keys;
    for (const auto& Row : Rows)
    {
        if (!Row.IsValid() || Row->Type != EJson::Object) return false;
        const auto Object = Row->AsObject();
        FString RoomId, Map;
        const TSharedPtr<FJsonObject>* Location;
        if (!Object->TryGetStringField(TEXT("RoomId"), RoomId) || RoomId.IsEmpty() || !Object->TryGetStringField(TEXT("Map"), Map) || !Map.StartsWith(TEXT("/Game/"))) return false;
        if (!Object->TryGetObjectField(TEXT("Location"), Location)) return false;
        for (const FString Axis : { TEXT("X"), TEXT("Y"), TEXT("Z") })
        {
            double Number;
            if (!(*Location)->HasTypedField<EJson::Number>(Axis) || !(*Location)->TryGetNumberField(Axis, Number) || !FMath::IsFinite(Number)) return false;
        }
        FPFTRoomBinding Binding;
        if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Binding) || Keys.Contains(Map + TEXT(":") + RoomId)) return false;
        Keys.Add(Map + TEXT(":") + RoomId);
        NewBindings.Add(Binding);
    }
    ClearSelection();
    for (const auto& Marker : SpawnedMarkers) if (IsValid(Marker)) Marker->Destroy();
    SpawnedMarkers.Empty();
    Bindings = MoveTemp(NewBindings);
    Error.Empty();
    return true;
}

void UPFTDestinations::ClearSelection()
{
    if (IsValid(ActiveMarker)) ActiveMarker->SetSelected(false);
    ActiveMarker = nullptr;
}

bool UPFTDestinations::SelectRoom(const FString& RoomId)
{
    ClearSelection();
    Error = TEXT("No location is bound for ") + RoomId;
    if (RoomId.IsEmpty()) return false;
    TArray<APFTRoomMarker*> Matches;
    for (TActorIterator<APFTRoomMarker> Marker(GetWorld()); Marker; ++Marker)
    {
        if (Marker->RoomId == RoomId) Matches.Add(*Marker);
    }
    if (Matches.Num() > 1)
    {
        Error = TEXT("Multiple markers are bound to ") + RoomId;
        return false;
    }
    if (Matches.Num() == 1) ActiveMarker = Matches[0];
    const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName());
    if (!ActiveMarker)
    {
        const auto* Binding = Bindings.FindByPredicate([&](const FPFTRoomBinding& Item) { return Item.RoomId == RoomId && Item.Map == Map; });
        if (!Binding) return false;
        ActiveMarker = GetWorld()->SpawnActor<APFTRoomMarker>(Binding->Location, FRotator::ZeroRotator);
        if (!ActiveMarker) return false;
        ActiveMarker->RoomId = RoomId;
        SpawnedMarkers.Add(ActiveMarker);
    }
    ActiveMarker->SetSelected(true);
    Error.Empty();
    return true;
}
