using UnrealBuildTool;

public class UnrealMCPAI : ModuleRules
{
    public UnrealMCPAI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "UnrealMCP", "UnrealMCPAssetCore"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AIGraph", "AIModule", "BehaviorTreeEditor", "BlueprintGraph", "Kismet", "UnrealEd"
        });
    }
}
