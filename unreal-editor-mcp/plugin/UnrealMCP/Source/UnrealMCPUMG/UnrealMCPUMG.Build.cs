using UnrealBuildTool;

public class UnrealMCPUMG : ModuleRules
{
    public UnrealMCPUMG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "UnrealMCPAssetCore", "UnrealMCPBlueprint" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "BlueprintGraph",
            "Engine",
            "Kismet",
            "UMG",
            "UMGEditor",
            "UnrealEd"
        });
    }
}
