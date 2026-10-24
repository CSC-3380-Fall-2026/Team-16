using UnrealBuildTool;

public class PFTNavigator : ModuleRules
{
    public PFTNavigator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "JsonUtilities", "Projects", "InputCore", "Slate", "SlateCore" });
        RuntimeDependencies.Add("$(PluginDir)/Content/Data/...", StagedFileType.UFS);
    }
}
