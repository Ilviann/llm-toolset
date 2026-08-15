#include "Modules/ModuleManager.h"

#include "UnrealMCPAnimationInspectionAdapter.h"
#include "UnrealMCPDomainModule.h"

class FUnrealMCPAnimationModule final : public IUnrealMCPBuiltInDomainModule
{
public:
    FName GetDomainName() const override { return TEXT("animation"); }

    bool RegisterAssetFamilies(
        FUnrealMCPAssetFamilyRegistry& Registry,
        FUnrealMCPError& OutError) override
    {
        return UnrealMCP::AnimationInspection::RegisterAdapter(Registry, OutError);
    }

    bool RegisterCommands(
        const FUnrealMCPDomainRegistrar& Registrar,
        const FUnrealMCPDomainContext&,
        FString& OutError) override
    {
        if (!Registrar.RegisterFeature({TEXT("asset_inspect_animation"), [] { return true; }}, OutError))
            return false;
        for (const FUnrealMCPNativeLimit Limit : {
            FUnrealMCPNativeLimit{TEXT("animation_graphs"), 128},
            {TEXT("animation_state_machines"), 128},
            {TEXT("animation_states"), 512},
            {TEXT("animation_transitions"), 1024},
            {TEXT("animation_parent_overrides"), 256}})
        {
            if (!Registrar.RegisterLimit(Limit, OutError)) return false;
        }
        return true;
    }
};

IMPLEMENT_MODULE(FUnrealMCPAnimationModule, UnrealMCPAnimation)
