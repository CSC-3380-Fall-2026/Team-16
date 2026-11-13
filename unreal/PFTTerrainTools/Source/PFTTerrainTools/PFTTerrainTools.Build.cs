using UnrealBuildTool;

public class PFTTerrainTools : ModuleRules
{
	public PFTTerrainTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Landscape", "AssetRegistry" });
	}
}
