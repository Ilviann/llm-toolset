using UnrealBuildTool;

public class UnrealMCPContent : ModuleRules
{
    public UnrealMCPContent(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "UnrealMCPAssetCore", "UnrealMCPBlueprint" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "DataLayerEditor",
            "Engine",
            "EngineSettings",
            "Kismet",
            "Projects",
            "UnrealEd"
        });
    }
}
