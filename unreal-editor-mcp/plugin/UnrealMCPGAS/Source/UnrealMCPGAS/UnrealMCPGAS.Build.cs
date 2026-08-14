using UnrealBuildTool;

public class UnrealMCPGAS : ModuleRules
{
    public UnrealMCPGAS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealMCP", "UnrealMCPAssetCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "GameplayAbilities", "GameplayTags", "GameplayTasks",
            "Kismet", "UnrealEd"
        });
    }
}
