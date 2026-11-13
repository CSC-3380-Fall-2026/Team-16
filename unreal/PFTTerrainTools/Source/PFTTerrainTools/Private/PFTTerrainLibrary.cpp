#include "PFTTerrainLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "LandscapeComponent.h"
#include "LandscapeHLODBuilder.h"
#include "LandscapeProxy.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PFTTerrainTools)

UStaticMesh* UPFTTerrainLibrary::BakeTerrain(ALandscapeProxy* Landscape, const FString& PackageName)
{
	if (!Landscape || Landscape->LandscapeComponents.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
	{
		return nullptr;
	}
	if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
	{
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	const FString MeshName = FPackageName::GetShortName(PackageName);
	FHLODBuildContext Context{};
	Context.TargetWorld = Landscape->GetWorld();
	Context.BuildWorld = Context.TargetWorld;
	Context.AssetsOuter = Package;
	Context.AssetsObjectFlags = RF_Public;
	for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
	{
		Context.SourceComponents.Add(Component);
	}

	TGuardValue MeshPolicy(Landscape->HLODMeshSourceLODPolicy, ELandscapeHLODMeshSourceLODPolicy::SpecificLOD);
	TGuardValue MeshLOD(Landscape->HLODMeshSourceLOD, 1);
	TGuardValue TexturePolicy(Landscape->HLODTextureSizePolicy, ELandscapeHLODTextureSizePolicy::SpecificSize);
	TGuardValue TextureSize(Landscape->HLODTextureSize, 2048);
	const auto Components = GetDefault<ULandscapeHLODBuilder>()->Build(Context, Context.SourceComponents);
	if (Components.Num() != 1)
	{
		return nullptr;
	}

	const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Components[0]);
	UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
	if (!Mesh || !Mesh->Rename(*MeshName, Package, REN_DontCreateRedirectors))
	{
		return nullptr;
	}

	Mesh->SetFlags(RF_Standalone);
	FAssetRegistryModule::AssetCreated(Mesh);
	Mesh->MarkPackageDirty();
	return Mesh;
}
