using UnrealBuildTool;

public class UnrealMCP : ModuleRules
{
    public UnrealMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "UnrealMCPAssetCore" });
        DynamicallyLoadedModuleNames.AddRange(new[]
        {
            "UnrealMCPUMG",
            "UnrealMCPAnimation",
            "UnrealMCPContent"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "CoreUObject",
            "DataLayerEditor",
            "Engine",
            "EngineSettings",
            "HTTPServer",
            "Json",
            "Kismet",
            "KismetCompiler",
            "Projects",
            "SubobjectDataInterface",
            "UMG",
            "UMGEditor",
            "UnrealMCPBlueprint",
            "UnrealEd"
        });
    }
}
