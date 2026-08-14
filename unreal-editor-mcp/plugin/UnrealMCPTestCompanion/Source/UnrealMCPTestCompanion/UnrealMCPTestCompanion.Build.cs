using UnrealBuildTool;

public class UnrealMCPTestCompanion : ModuleRules
{
    public UnrealMCPTestCompanion(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealMCP", "UnrealMCPAssetCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry", "Kismet", "KismetCompiler", "UnrealEd"
        });
    }
}
