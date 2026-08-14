#include "Modules/ModuleManager.h"

#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionService.h"
#include "UnrealMCPDomainModule.h"
#include "UnrealMCPNeutralAssetInspectionAdapter.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::AssetCore::Private
{
struct FRuntimeState
{
    explicit FRuntimeState(TSharedRef<FUnrealMCPAssetFamilyRegistry> InRegistry)
        : Registry(MoveTemp(InRegistry))
    {
    }

    bool Inspect(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError)
    {
        if (!InspectionService)
        {
            InspectionService = MakeUnique<FUnrealMCPAssetInspectionService>(Registry);
        }
        return InspectionService->Execute(Arguments, OutResult, OutError);
    }

    TSharedRef<FUnrealMCPAssetFamilyRegistry> Registry;
    TUniquePtr<FUnrealMCPAssetInspectionService> InspectionService;
};
}

class FUnrealMCPAssetCoreModule final : public IUnrealMCPBuiltInDomainModule
{
public:
    FName GetDomainName() const override { return TEXT("asset_core"); }

    bool RegisterAssetFamilies(
        FUnrealMCPAssetFamilyRegistry& Registry,
        FUnrealMCPError& OutError) override
    {
        return UnrealMCP::AssetCore::RegisterNeutralAssetAdapter(Registry, OutError);
    }

    bool RegisterCommands(
        const FUnrealMCPDomainRegistrar& Registrar,
        const FUnrealMCPDomainContext& Context,
        FString& OutError) override
    {
        using Access = EUnrealMCPCommandAccess;
        using Dispatch = EUnrealMCPCommandDispatch;
        using Retained = EUnrealMCPRetainedOperationPolicy;
        const TSharedRef<UnrealMCP::AssetCore::Private::FRuntimeState> State =
            MakeShared<UnrealMCP::AssetCore::Private::FRuntimeState>(Context.AssetFamilyRegistry);

        FUnrealMCPCommandDescriptor Inspect;
        Inspect.Identity = TEXT("asset_inspect");
        Inspect.Order = 5;
        Inspect.Access = Access::ReadOnly;
        Inspect.RetainedOperation = Retained::None;
        Inspect.Dispatch = Dispatch::GameThread;
        Inspect.bAllowsExtensionRequests = true;
        Inspect.Handler = [State](const auto& A, auto& R, auto& E) { return State->Inspect(A, R, E); };
        return Registrar.RegisterCommand(MoveTemp(Inspect), OutError)
            && Registrar.RegisterFeature({TEXT("asset_inspection_core"), [] { return true; }}, OutError)
            && Registrar.RegisterLimit({TEXT("asset_inspect_page_size"), UnrealMCP::MaxAssetInspectPageSize}, OutError)
            && Registrar.RegisterLimit({TEXT("asset_inspect_selector_bytes"), UnrealMCP::MaxAssetInspectSelectorBytes}, OutError)
            && Registrar.RegisterLimit({TEXT("asset_inspect_complete_graph_bytes"), UnrealMCP::MaxAssetInspectCompleteGraphBytes}, OutError);
    }
};

IMPLEMENT_MODULE(FUnrealMCPAssetCoreModule, UnrealMCPAssetCore)
