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
            "BlueprintGraph",
            "CoreUObject",
            "Engine",
            "EngineSettings",
            "HTTPServer",
            "Json",
            "Kismet",
            "KismetCompiler",
            "Projects",
            "SubobjectDataInterface",
            "UnrealEd"
        });
    }
}
