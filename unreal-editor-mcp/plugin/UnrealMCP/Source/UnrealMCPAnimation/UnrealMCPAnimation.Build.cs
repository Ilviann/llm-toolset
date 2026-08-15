using UnrealBuildTool;

public class UnrealMCPAnimation : ModuleRules
{
    public UnrealMCPAnimation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "UnrealMCPAssetCore", "UnrealMCPBlueprint" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AnimGraph",
            "BlueprintGraph",
            "Engine",
            "Kismet",
            "UnrealEd"
        });
    }
}
