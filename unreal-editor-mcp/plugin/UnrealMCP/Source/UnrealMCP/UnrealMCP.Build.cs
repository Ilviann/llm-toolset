using UnrealBuildTool;

public class UnrealMCP : ModuleRules
{
    public UnrealMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "AnimGraph",
            "BlueprintGraph",
            "CoreUObject",
            "DataLayerEditor",
            "Engine",
            "EngineSettings",
            "HTTPServer",
            "Json",
            "Kismet",
            "KismetCompiler",
            "MovieScene",
            "Projects",
            "SubobjectDataInterface",
            "UMG",
            "UMGEditor",
            "UnrealEd"
        });
    }
}
