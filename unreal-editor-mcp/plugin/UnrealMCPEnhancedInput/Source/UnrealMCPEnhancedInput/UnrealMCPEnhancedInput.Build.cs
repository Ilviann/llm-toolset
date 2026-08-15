using UnrealBuildTool;

public class UnrealMCPEnhancedInput : ModuleRules
{
    public UnrealMCPEnhancedInput(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealMCP", "UnrealMCPAssetCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "BlueprintGraph", "EnhancedInput", "InputCore", "Kismet", "UnrealEd"
        });
    }
}
