#include "Modules/ModuleManager.h"

#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPDomainModule.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWidgetTreeService.h"

namespace UnrealMCP::UMGDomain::Private
{
struct FRuntimeState
{
    explicit FRuntimeState(IUnrealMCPBlueprintExtensionProvider& InExtensions)
        : Extensions(InExtensions)
    {
    }

    bool Execute(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
    {
        if (!Inspector)
        {
            Inspector = MakeUnique<FUnrealMCPBlueprintInspector>(Extensions);
        }
        if (!WidgetTree)
        {
            WidgetTree = MakeUnique<FUnrealMCPWidgetTreeService>(*Inspector);
        }
        return WidgetTree->Execute(A, R, E);
    }

    IUnrealMCPBlueprintExtensionProvider& Extensions;
    TUniquePtr<FUnrealMCPBlueprintInspector> Inspector;
    TUniquePtr<FUnrealMCPWidgetTreeService> WidgetTree;
};
}

class FUnrealMCPUMGModule final : public IUnrealMCPBuiltInDomainModule
{
public:
    FName GetDomainName() const override { return TEXT("umg"); }
    bool RegisterAssetFamilies(FUnrealMCPAssetFamilyRegistry&, FUnrealMCPError&) override { return true; }

    bool RegisterCommands(
        const FUnrealMCPDomainRegistrar& Registrar,
        const FUnrealMCPDomainContext& Context,
        FString& OutError) override
    {
        const TSharedRef<UnrealMCP::UMGDomain::Private::FRuntimeState> State =
            MakeShared<UnrealMCP::UMGDomain::Private::FRuntimeState>(Context.BlueprintExtensions);
        FUnrealMCPCommandDescriptor Descriptor;
        Descriptor.Identity = TEXT("widget_tree_edit");
        Descriptor.Order = 22;
        Descriptor.Access = EUnrealMCPCommandAccess::Writable;
        Descriptor.RetainedOperation = EUnrealMCPRetainedOperationPolicy::Retained;
        Descriptor.Dispatch = EUnrealMCPCommandDispatch::GameThread;
        Descriptor.Handler = [State](const auto& A, auto& R, auto& E) { return State->Execute(A, R, E); };
        if (!Registrar.RegisterCommand(MoveTemp(Descriptor), OutError)) return false;
        for (const TCHAR* Feature : {
            TEXT("widget_tree_authoring"), TEXT("umg_layout_authoring"), TEXT("umg_style_authoring"),
            TEXT("umg_property_bindings"), TEXT("umg_designer_events")})
        {
            if (!Registrar.RegisterFeature({Feature, [] { return true; }}, OutError)) return false;
        }
        for (const FUnrealMCPNativeLimit Limit : {
            FUnrealMCPNativeLimit{TEXT("widget_tree_widgets"), UnrealMCP::MaxWidgetTreeWidgets},
            {TEXT("widget_tree_depth"), UnrealMCP::MaxWidgetTreeDepth},
            {TEXT("widget_named_slots"), UnrealMCP::MaxWidgetNamedSlots},
            {TEXT("widget_defaults_per_widget"), UnrealMCP::MaxWidgetDefaultsPerWidget},
            {TEXT("widget_changed_defaults"), UnrealMCP::MaxWidgetChangedDefaults},
            {TEXT("widget_bindings"), UnrealMCP::MaxWidgetBindings}})
        {
            if (!Registrar.RegisterLimit(Limit, OutError)) return false;
        }
        return true;
    }
};

IMPLEMENT_MODULE(FUnrealMCPUMGModule, UnrealMCPUMG)
