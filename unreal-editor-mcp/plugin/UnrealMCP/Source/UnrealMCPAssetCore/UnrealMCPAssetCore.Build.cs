using UnrealBuildTool;

public class UnrealMCPAssetCore : ModuleRules
{
    public UnrealMCPAssetCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "Engine",
            "Json",
            "Projects",
            "UnrealEd"
        });
    }
}
