using UnrealBuildTool;

public class UnrealMCPBlueprint : ModuleRules
{
    public UnrealMCPBlueprint(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "UnrealMCPAssetCore" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "Engine",
            "EngineSettings",
            "Kismet",
            "KismetCompiler",
            "Projects",
            "SubobjectDataInterface",
            "UMG",
            "UMGEditor",
            "UnrealEd"
        });
    }
}
