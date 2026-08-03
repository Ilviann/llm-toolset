using UnrealBuildTool;

public class UnrealMCPGAS : ModuleRules
{
    public UnrealMCPGAS(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealMCP"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "GameplayAbilities", "GameplayTags", "GameplayTasks", "Json",
            "Kismet", "UnrealEd"
        });
    }
}
