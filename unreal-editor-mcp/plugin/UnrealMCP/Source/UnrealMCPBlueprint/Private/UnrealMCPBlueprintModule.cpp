#include "Modules/ModuleManager.h"

#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionAdapters.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPBlueprintBlockReplacementService.h"
#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPDomainModule.h"
#include "UnrealMCPGameplayFrameworkEditor.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::BlueprintDomain::Private
{
struct FRuntimeState
{
    FRuntimeState(
        IUnrealMCPBlueprintExtensionProvider& InExtensions,
        FString InBridgeInstanceId)
        : Extensions(InExtensions)
        , BridgeInstanceId(MoveTemp(InBridgeInstanceId))
    {
    }

    FUnrealMCPBlueprintInspector& Inspector()
    {
        if (!BlueprintInspector)
        {
            BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(Extensions);
        }
        return *BlueprintInspector;
    }

    FUnrealMCPBlueprintActionCatalog& ActionCatalog()
    {
        if (!BlueprintActionCatalog)
        {
            BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(Inspector(), BridgeInstanceId);
        }
        return *BlueprintActionCatalog;
    }

    bool ExecuteActionCatalog(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
    {
        return ActionCatalog().Execute(A, R, E);
    }

    bool ExecuteGraphEdit(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
    {
        if (!GraphEditor)
        {
            GraphEditor = MakeUnique<FUnrealMCPBlueprintGraphEditor>(Inspector(), ActionCatalog());
        }
        return GraphEditor->Execute(A, R, E);
    }

    bool ExecuteBlockReplace(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
    {
        if (!BlockReplacement)
        {
            BlockReplacement = MakeUnique<FUnrealMCPBlueprintBlockReplacementService>(Inspector(), ActionCatalog());
        }
        return BlockReplacement->Execute(A, R, E);
    }

    bool ExecuteMutation(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
    {
        if (!Mutator)
        {
            Mutator = MakeUnique<FUnrealMCPBlueprintMutator>(Inspector());
        }
        return Mutator->Execute(Command, A, R, E);
    }

    bool ExecuteGameplayFramework(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
    {
        if (!GameplayFramework)
        {
            GameplayFramework = MakeUnique<FUnrealMCPGameplayFrameworkEditor>(ProjectHash);
        }
        return GameplayFramework->Execute(A, R, E);
    }

    IUnrealMCPBlueprintExtensionProvider& Extensions;
    FString BridgeInstanceId;
    FString ProjectHash;
    TUniquePtr<FUnrealMCPBlueprintInspector> BlueprintInspector;
    TUniquePtr<FUnrealMCPBlueprintActionCatalog> BlueprintActionCatalog;
    TUniquePtr<FUnrealMCPBlueprintGraphEditor> GraphEditor;
    TUniquePtr<FUnrealMCPBlueprintBlockReplacementService> BlockReplacement;
    TUniquePtr<FUnrealMCPBlueprintMutator> Mutator;
    TUniquePtr<FUnrealMCPGameplayFrameworkEditor> GameplayFramework;
};

FUnrealMCPCommandDescriptor Command(
    const TCHAR* Identity,
    EUnrealMCPCommandAccess Access,
    FUnrealMCPCommandHandler Handler)
{
    FUnrealMCPCommandDescriptor Result;
    Result.Identity = Identity;
    const TMap<FString, int32> Orders = {
        {TEXT("blueprint_action_catalog"), 13}, {TEXT("blueprint_graph_edit"), 14},
        {TEXT("blueprint_block_replace"), 15}, {TEXT("blueprint_create"), 16},
        {TEXT("blueprint_compile"), 17}, {TEXT("blueprint_save"), 18},
        {TEXT("blueprint_component_edit"), 19}, {TEXT("blueprint_default_edit"), 20},
        {TEXT("blueprint_member_edit"), 21}, {TEXT("gameplay_framework_edit"), 23}};
    Result.Order = Orders.FindChecked(Result.Identity);
    Result.Access = Access;
    Result.RetainedOperation = Access == EUnrealMCPCommandAccess::ReadOnly
        ? EUnrealMCPRetainedOperationPolicy::None
        : EUnrealMCPRetainedOperationPolicy::Retained;
    Result.Dispatch = EUnrealMCPCommandDispatch::GameThread;
    Result.Handler = MoveTemp(Handler);
    return Result;
}
}

class FUnrealMCPBlueprintModule final : public IUnrealMCPBuiltInDomainModule
{
public:
    FName GetDomainName() const override { return TEXT("blueprint"); }

    bool RegisterAssetFamilies(FUnrealMCPAssetFamilyRegistry& Registry, FUnrealMCPError& OutError) override
    {
        return UnrealMCP::AssetInspection::RegisterBlueprintAdapter(Registry, OutError);
    }

    bool RegisterCommands(
        const FUnrealMCPDomainRegistrar& Registrar,
        const FUnrealMCPDomainContext& Context,
        FString& OutError) override
    {
        using namespace UnrealMCP::BlueprintDomain::Private;
        using Access = EUnrealMCPCommandAccess;
        const TSharedRef<FRuntimeState> State = MakeShared<FRuntimeState>(
            Context.BlueprintExtensions, Context.BridgeInstanceId);
        State->ProjectHash = Context.ProjectHash;
        if (!Registrar.RegisterCommand(Command(TEXT("blueprint_action_catalog"), Access::ReadOnly,
                [State](const auto& A, auto& R, auto& E) { return State->ExecuteActionCatalog(A, R, E); }), OutError)
            || !Registrar.RegisterCommand(Command(TEXT("blueprint_graph_edit"), Access::Writable,
                [State](const auto& A, auto& R, auto& E) { return State->ExecuteGraphEdit(A, R, E); }), OutError)
            || !Registrar.RegisterCommand(Command(TEXT("blueprint_block_replace"), Access::Writable,
                [State](const auto& A, auto& R, auto& E) { return State->ExecuteBlockReplace(A, R, E); }), OutError)
            || !Registrar.RegisterCommand(Command(TEXT("gameplay_framework_edit"), Access::Writable,
                [State](const auto& A, auto& R, auto& E) { return State->ExecuteGameplayFramework(A, R, E); }), OutError))
        {
            return false;
        }
        for (const TCHAR* Identity : {
            TEXT("blueprint_create"), TEXT("blueprint_compile"), TEXT("blueprint_save"),
            TEXT("blueprint_component_edit"), TEXT("blueprint_default_edit"), TEXT("blueprint_member_edit")})
        {
            const FString CommandIdentity(Identity);
            if (!Registrar.RegisterCommand(Command(Identity, Access::Writable,
                [State, CommandIdentity](const auto& A, auto& R, auto& E)
                {
                    return State->ExecuteMutation(CommandIdentity, A, R, E);
                }), OutError))
            {
                return false;
            }
        }
        for (const TCHAR* Feature : {
            TEXT("blueprint_mutation"), TEXT("blueprint_creation"), TEXT("blueprint_compile"), TEXT("blueprint_save"),
            TEXT("reliable_mutations"), TEXT("blueprint_components"), TEXT("blueprint_defaults"), TEXT("blueprint_member_variables"),
            TEXT("blueprint_functions"), TEXT("blueprint_local_variables"), TEXT("blueprint_rep_notify"), TEXT("blueprint_macros"),
            TEXT("blueprint_custom_events"), TEXT("blueprint_action_catalog"), TEXT("blueprint_graph_mutation"),
            TEXT("blueprint_graph_node_lifecycle"), TEXT("blueprint_graph_pin_defaults"), TEXT("blueprint_graph_direct_connections"),
            TEXT("blueprint_graph_wildcard_specialization"), TEXT("blueprint_graph_automatic_conversion"),
            TEXT("blueprint_function_replacement"), TEXT("blueprint_function_replacement_scratch_preflight"),
            TEXT("blueprint_macro_replacement"), TEXT("blueprint_custom_event_replacement"), TEXT("blueprint_event_replacement"),
            TEXT("blueprint_logic_unit_external_connections"), TEXT("blueprint_node_layout"), TEXT("blueprint_family_policy"),
            TEXT("game_mode_families"), TEXT("game_state_families"), TEXT("game_instance_family"),
            TEXT("widget_blueprint_family"), TEXT("multiplayer_blueprint_authoring"), TEXT("custom_event_rpcs"),
            TEXT("typed_replication_settings"), TEXT("gameplay_framework_assignment")})
        {
            if (!Registrar.RegisterFeature({Feature, [] { return true; }}, OutError)) return false;
        }
        for (const FUnrealMCPNativeLimit Limit : {
            FUnrealMCPNativeLimit{TEXT("compiler_diagnostics"), UnrealMCP::MaxCompilerDiagnostics},
            {TEXT("diagnostic_chars"), UnrealMCP::MaxDiagnosticChars},
            {TEXT("property_names"), UnrealMCP::MaxPropertyNames},
            {TEXT("variable_references"), UnrealMCP::MaxVariableReferences},
            {TEXT("action_results"), UnrealMCP::MaxActionResults},
            {TEXT("action_scan"), UnrealMCP::MaxActionScan},
            {TEXT("retained_actions"), UnrealMCP::MaxRetainedActions},
            {TEXT("retained_catalogs"), UnrealMCP::MaxRetainedCatalogs},
            {TEXT("action_lifetime_ms"), UnrealMCP::ActionLifetimeSeconds * 1000.0},
            {TEXT("action_scan_ms"), UnrealMCP::ActionScanSeconds * 1000.0},
            {TEXT("concurrent_catalogs"), UnrealMCP::MaxConcurrentCatalogs},
            {TEXT("graph_nodes"), UnrealMCP::MaxGraphNodes},
            {TEXT("graph_pins_per_node"), UnrealMCP::MaxGraphPinsPerNode},
            {TEXT("graph_coordinate"), UnrealMCP::MaxGraphCoordinate},
            {TEXT("graph_links_per_pin"), UnrealMCP::MaxGraphLinksPerPin},
            {TEXT("graph_automatic_conversion_nodes"), UnrealMCP::MaxAutomaticConversionNodes},
            {TEXT("pin_default_chars"), UnrealMCP::MaxPinDefaultChars},
            {TEXT("function_replacement_nodes"), UnrealMCP::MaxFunctionReplacementNodes},
            {TEXT("function_replacement_owned_nodes"), UnrealMCP::MaxFunctionReplacementOwnedNodes},
            {TEXT("function_replacement_locals"), UnrealMCP::MaxFunctionReplacementLocals},
            {TEXT("function_replacement_defaults"), UnrealMCP::MaxFunctionReplacementDefaults},
            {TEXT("function_replacement_connections"), UnrealMCP::MaxFunctionReplacementConnections},
            {TEXT("logic_unit_replacement_nodes"), UnrealMCP::MaxLogicUnitReplacementNodes},
            {TEXT("logic_unit_replacement_owned_nodes"), UnrealMCP::MaxLogicUnitOwnedNodes},
            {TEXT("logic_unit_replacement_locals"), UnrealMCP::MaxLogicUnitLocals},
            {TEXT("logic_unit_replacement_defaults"), UnrealMCP::MaxLogicUnitDefaults},
            {TEXT("logic_unit_replacement_connections"), UnrealMCP::MaxLogicUnitConnections},
            {TEXT("logic_unit_external_connections"), UnrealMCP::MaxLogicUnitExternalConnections},
            {TEXT("logic_unit_layout_nodes"), UnrealMCP::MaxLogicUnitLayoutNodes},
            {TEXT("logic_unit_layout_edges"), UnrealMCP::MaxLogicUnitLayoutEdges},
            {TEXT("logic_unit_layout_iterations"), UnrealMCP::MaxLogicUnitLayoutIterations},
            {TEXT("logic_unit_layout_collision_probes"), UnrealMCP::MaxLogicUnitLayoutCollisionProbes},
            {TEXT("logic_unit_layout_work"), UnrealMCP::MaxLogicUnitLayoutWork},
            {TEXT("logic_unit_layout_ms"), UnrealMCP::MaxLogicUnitLayoutSeconds * 1000.0}})
        {
            if (!Registrar.RegisterLimit(Limit, OutError)) return false;
        }
        return Registrar.RegisterBlueprintFamilies(
            [] { return UnrealMCP::BlueprintFamilyPolicy::BuildPublishedMatrix(); }, OutError);
    }
};

IMPLEMENT_MODULE(FUnrealMCPBlueprintModule, UnrealMCPBlueprint)
