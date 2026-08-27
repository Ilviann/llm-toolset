using UnrealBuildTool;

public class UnrealMCPTestCompanion : ModuleRules
{
    public UnrealMCPTestCompanion(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayAbilities", "GameplayTags", "UnrealMCP" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry", "Json", "Kismet", "KismetCompiler", "UnrealEd"
        });
    }
}
