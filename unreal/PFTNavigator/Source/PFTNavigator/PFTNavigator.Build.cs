using UnrealBuildTool;

public class PFTNavigator : ModuleRules
{
    public PFTNavigator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "Projects" });
        RuntimeDependencies.Add("$(PluginDir)/Content/Data/...", StagedFileType.UFS);
    }
}
