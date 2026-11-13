#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "PFTTerrainLibrary.generated.h"

class ALandscapeProxy;
class UStaticMesh;

UCLASS()
class UPFTTerrainLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Bake the source Landscape into a new PackageName and return the unsaved textured mesh. */
	UFUNCTION(BlueprintCallable, Category = "PFT Terrain")
	static UStaticMesh* BakeTerrain(ALandscapeProxy* Landscape, const FString& PackageName);
};
