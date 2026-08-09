using UnrealBuildTool;

public class UnrealMCPCommonUI : ModuleRules
{
    public UnrealMCPCommonUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealMCP"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CommonInput", "CommonUI", "Json", "Kismet", "UMG", "UMGEditor", "UnrealEd"
        });
    }
}
