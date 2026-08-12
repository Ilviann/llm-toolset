#include "UnrealMCPCommandCatalog.h"

#include "UnrealMCPAssetDeletionService.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionService.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPBlueprintBlockReplacementService.h"
#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPExtensionRegistry.h"
#include "UnrealMCPGameDataService.h"
#include "UnrealMCPGameplayFrameworkEditor.h"
#include "UnrealMCPLevelActorEditingService.h"
#include "UnrealMCPLevelManagementService.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPOperationLedger.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWidgetTreeService.h"

namespace
{
FUnrealMCPNativeFeature FixedFeature(const TCHAR* Name, bool bValue = true)
{
    return {Name, [bValue]() { return bValue; }};
}

FUnrealMCPNativeLimit FixedLimit(const TCHAR* Name, double Value)
{
    return {Name, Value};
}
}

bool FUnrealMCPCommandCatalogBuilder::Register(
    FUnrealMCPCommandDescriptor Descriptor,
    EUnrealMCPCommandSource Source,
    bool bProvidesModelSchema,
    FString& OutError)
{
    if (bFrozen)
    {
        OutError = TEXT("Native command catalog is frozen");
        return false;
    }
    if (Source != EUnrealMCPCommandSource::FixedNative || bProvidesModelSchema)
    {
        OutError = TEXT("Runtime-provided commands and schemas are forbidden");
        return false;
    }
    if (Descriptor.Identity.IsEmpty() || Descriptor.Handler == nullptr)
    {
        OutError = TEXT("Native command descriptors require an identity and handler");
        return false;
    }
    if (CommandIndexes.Contains(Descriptor.Identity))
    {
        OutError = FString::Printf(TEXT("Duplicate native command: %s"), *Descriptor.Identity);
        return false;
    }
    for (const FUnrealMCPNativeFeature& Feature : Descriptor.Features)
    {
        if (Feature.Name.IsEmpty() || Feature.Resolve == nullptr || FeatureNames.Contains(Feature.Name))
        {
            OutError = FString::Printf(TEXT("Conflicting native capability: %s"), *Feature.Name);
            return false;
        }
    }
    for (const FUnrealMCPNativeLimit& Limit : Descriptor.Limits)
    {
        if (Limit.Name.IsEmpty() || !FMath::IsFinite(Limit.Value) || LimitNames.Contains(Limit.Name))
        {
            OutError = FString::Printf(TEXT("Conflicting native limit: %s"), *Limit.Name);
            return false;
        }
    }
    const int32 Index = Descriptors.Add(MoveTemp(Descriptor));
    CommandIndexes.Add(Descriptors[Index].Identity, Index);
    for (const FUnrealMCPNativeFeature& Feature : Descriptors[Index].Features) FeatureNames.Add(Feature.Name);
    for (const FUnrealMCPNativeLimit& Limit : Descriptors[Index].Limits) LimitNames.Add(Limit.Name);
    return true;
}

bool FUnrealMCPCommandCatalogBuilder::Freeze(FString& OutError)
{
    if (bFrozen)
    {
        OutError = TEXT("Native command catalog is already frozen");
        return false;
    }
    FeatureNames.Reset();
    LimitNames.Reset();
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        for (const FUnrealMCPNativeFeature& Feature : Descriptor.Features)
        {
            if (Feature.Name.IsEmpty() || Feature.Resolve == nullptr || FeatureNames.Contains(Feature.Name))
            {
                OutError = FString::Printf(TEXT("Conflicting native capability: %s"), *Feature.Name);
                return false;
            }
            FeatureNames.Add(Feature.Name);
        }
        for (const FUnrealMCPNativeLimit& Limit : Descriptor.Limits)
        {
            if (Limit.Name.IsEmpty() || !FMath::IsFinite(Limit.Value) || LimitNames.Contains(Limit.Name))
            {
                OutError = FString::Printf(TEXT("Conflicting native limit: %s"), *Limit.Name);
                return false;
            }
            LimitNames.Add(Limit.Name);
        }
    }
    bFrozen = true;
    return true;
}

const FUnrealMCPCommandDescriptor* FUnrealMCPCommandCatalogBuilder::Find(const FString& Command) const
{
    const int32* Index = CommandIndexes.Find(Command);
    return Index != nullptr ? &Descriptors[*Index] : nullptr;
}

FUnrealMCPCommandDescriptor* FUnrealMCPCommandCatalogBuilder::FindMutable(const FString& Command)
{
    if (bFrozen) return nullptr;
    const int32* Index = CommandIndexes.Find(Command);
    return Index != nullptr ? &Descriptors[*Index] : nullptr;
}

TArray<TSharedPtr<FUnrealMCPValue>> FUnrealMCPCommandCatalogBuilder::BuildCommandNames() const
{
    TArray<TSharedPtr<FUnrealMCPValue>> Names;
    Names.Reserve(Descriptors.Num());
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        Names.Add(MakeShared<FUnrealMCPValueString>(Descriptor.Identity));
    }
    return Names;
}

TSharedRef<FUnrealMCPRecord> FUnrealMCPCommandCatalogBuilder::BuildFeatures() const
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        for (const FUnrealMCPNativeFeature& Feature : Descriptor.Features)
        {
            Result->SetBoolField(Feature.Name, Feature.Resolve());
        }
    }
    return Result;
}

TSharedRef<FUnrealMCPRecord> FUnrealMCPCommandCatalogBuilder::BuildLimits() const
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        for (const FUnrealMCPNativeLimit& Limit : Descriptor.Limits)
        {
            Result->SetNumberField(Limit.Name, Limit.Value);
        }
    }
    return Result;
}

FUnrealMCPCommandCatalog::FUnrealMCPCommandCatalog(
    FString InProjectHash,
    FString InBridgeInstanceId,
    FUnrealMCPOperationLedger& InOperationLedger,
    TSharedRef<FUnrealMCPAssetFamilyRegistry> InAssetFamilyRegistry,
    TSharedRef<FUnrealMCPExtensionRegistry> InExtensionRegistry,
    FUnrealMCPCommandHostHandlers InHostHandlers)
    : ProjectHash(MoveTemp(InProjectHash)), BridgeInstanceId(MoveTemp(InBridgeInstanceId)),
      OperationLedger(InOperationLedger), AssetFamilyRegistry(MoveTemp(InAssetFamilyRegistry)),
      ExtensionRegistry(MoveTemp(InExtensionRegistry))
{
    if (!AssetFamilyRegistry->IsFrozen())
    {
        InitializationError = TEXT("Built-in asset-family registry must be frozen before command composition");
        return;
    }
    Build(MoveTemp(InHostHandlers));
}

FUnrealMCPCommandCatalog::~FUnrealMCPCommandCatalog() = default;

bool FUnrealMCPCommandCatalog::Register(FUnrealMCPCommandDescriptor Descriptor)
{
    if (!InitializationError.IsEmpty()) return false;
    return Catalog.Register(MoveTemp(Descriptor), EUnrealMCPCommandSource::FixedNative, false, InitializationError);
}

void FUnrealMCPCommandCatalog::Build(FUnrealMCPCommandHostHandlers HostHandlers)
{
    auto Add = [this](const TCHAR* Identity, EUnrealMCPCommandAccess Access,
        EUnrealMCPRetainedOperationPolicy Retained, EUnrealMCPCommandDispatch Dispatch,
        FUnrealMCPCommandHandler Handler)
    {
        FUnrealMCPCommandDescriptor Descriptor;
        Descriptor.Identity = Identity;
        Descriptor.Access = Access;
        Descriptor.RetainedOperation = Retained;
        Descriptor.Dispatch = Dispatch;
        Descriptor.Handler = MoveTemp(Handler);
        Register(MoveTemp(Descriptor));
    };
    using Access = EUnrealMCPCommandAccess;
    using Retained = EUnrealMCPRetainedOperationPolicy;
    using Dispatch = EUnrealMCPCommandDispatch;

    Add(TEXT("capabilities"), Access::ReadOnly, Retained::None, Dispatch::GameThread, MoveTemp(HostHandlers.Capabilities));
    Add(TEXT("editor_state"), Access::ReadOnly, Retained::None, Dispatch::GameThread, MoveTemp(HostHandlers.EditorState));
    Add(TEXT("editor_shutdown"), Access::Internal, Retained::None, Dispatch::GameThread, MoveTemp(HostHandlers.EditorShutdown));
    Add(TEXT("operation_status"), Access::ReadOnly, Retained::None, Dispatch::RequestThread,
        [this](const auto& Arguments, auto& Result, auto& Error) { return OperationLedger.Status(Arguments, Result, Error); });
    Add(TEXT("operation_cancel"), Access::Writable, Retained::None, Dispatch::RequestThread,
        [this](const auto& Arguments, auto& Result, auto& Error) { return OperationLedger.Cancel(Arguments, Result, Error); });
    Add(TEXT("asset_inspect"), Access::ReadOnly, Retained::None, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteAssetInspect(A, R, E); });
    Add(TEXT("asset_references"), Access::ReadOnly, Retained::None, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteAssetReferences(A, R, E); });
    Add(TEXT("asset_delete"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteAssetDelete(A, R, E); });
    Add(TEXT("level_inspect"), Access::ReadOnly, Retained::None, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteLevelInspect(A, R, E); });
    Add(TEXT("level_open"), Access::ReadOnly, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteLevelOpen(A, R, E); });
    Add(TEXT("level_manage"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteLevelManage(A, R, E); });
    Add(TEXT("level_actor_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteLevelActorEdit(A, R, E); });
    Add(TEXT("level_save"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteLevelSave(A, R, E); });
    Add(TEXT("blueprint_action_catalog"), Access::ReadOnly, Retained::None, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintActionCatalog(A, R, E); });
    Add(TEXT("blueprint_graph_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintGraphEdit(A, R, E); });
    Add(TEXT("blueprint_block_replace"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintBlockReplace(A, R, E); });
    Add(TEXT("blueprint_create"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintMutation(TEXT("blueprint_create"), A, R, E); });
    Add(TEXT("blueprint_compile"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintMutation(TEXT("blueprint_compile"), A, R, E); });
    Add(TEXT("blueprint_save"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintMutation(TEXT("blueprint_save"), A, R, E); });
    Add(TEXT("blueprint_component_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintMutation(TEXT("blueprint_component_edit"), A, R, E); });
    Add(TEXT("blueprint_default_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintMutation(TEXT("blueprint_default_edit"), A, R, E); });
    Add(TEXT("blueprint_member_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteBlueprintMutation(TEXT("blueprint_member_edit"), A, R, E); });
    Add(TEXT("widget_tree_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteWidgetTreeEdit(A, R, E); });
    Add(TEXT("gameplay_framework_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteGameplayFrameworkEdit(A, R, E); });
    Add(TEXT("game_data_inspect"), Access::ReadOnly, Retained::None, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteGameDataInspect(A, R, E); });
    Add(TEXT("game_data_edit"), Access::Writable, Retained::Retained, Dispatch::GameThread,
        [this](const auto& A, auto& R, auto& E) { return ExecuteGameDataEdit(A, R, E); });

    if (FUnrealMCPCommandDescriptor* AssetInspect = Catalog.FindMutable(TEXT("asset_inspect")))
    {
        AssetInspect->bAllowsExtensionRequests = true;
    }

    FUnrealMCPCommandDescriptor* Capabilities = Catalog.FindMutable(TEXT("capabilities"));
    if (Capabilities == nullptr)
    {
        InitializationError = TEXT("Native command catalog is missing capabilities");
        return;
    }
    for (const TCHAR* Feature : {
        TEXT("asset_inspection_core"), TEXT("blueprint_mutation"), TEXT("blueprint_creation"), TEXT("blueprint_compile"), TEXT("blueprint_save"),
        TEXT("reliable_mutations"), TEXT("blueprint_components"), TEXT("blueprint_defaults"), TEXT("blueprint_member_variables"), TEXT("blueprint_functions"),
        TEXT("blueprint_local_variables"), TEXT("blueprint_rep_notify"), TEXT("blueprint_macros"), TEXT("blueprint_custom_events"), TEXT("blueprint_action_catalog"),
        TEXT("blueprint_graph_mutation"), TEXT("blueprint_graph_node_lifecycle"), TEXT("blueprint_graph_pin_defaults"), TEXT("blueprint_graph_direct_connections"),
        TEXT("blueprint_graph_wildcard_specialization"), TEXT("blueprint_graph_automatic_conversion"), TEXT("blueprint_function_replacement"),
        TEXT("blueprint_function_replacement_scratch_preflight"), TEXT("blueprint_macro_replacement"), TEXT("blueprint_custom_event_replacement"),
        TEXT("blueprint_event_replacement"), TEXT("blueprint_logic_unit_external_connections"), TEXT("blueprint_node_layout"), TEXT("blueprint_family_policy"),
        TEXT("game_mode_families"), TEXT("game_state_families"), TEXT("game_instance_family"), TEXT("widget_blueprint_family"), TEXT("widget_tree_authoring"),
        TEXT("umg_layout_authoring"), TEXT("umg_style_authoring"), TEXT("umg_property_bindings"), TEXT("umg_designer_events"),
        TEXT("multiplayer_blueprint_authoring"), TEXT("custom_event_rpcs"), TEXT("typed_replication_settings"), TEXT("gameplay_framework_assignment"),
        TEXT("user_defined_struct_authoring"), TEXT("typed_data_tables"), TEXT("game_data_batch_editing"), TEXT("asset_reference_discovery"),
        TEXT("asset_reference_live_memory"), TEXT("asset_delete"), TEXT("level_discovery"), TEXT("level_open"), TEXT("level_snapshots"),
        TEXT("level_management"), TEXT("level_blank_creation"), TEXT("level_template_creation"), TEXT("level_world_settings"), TEXT("level_map_deletion"),
        TEXT("level_actor_inspection"), TEXT("level_world_partition_descriptors"), TEXT("level_targeted_actor_loading"), TEXT("level_instance_properties"),
        TEXT("level_actor_editing"), TEXT("level_actor_transactions"), TEXT("level_package_save_verification"), TEXT("editor_lifecycle"),
        TEXT("graceful_editor_shutdown"), TEXT("companion_plugins")})
    {
        Capabilities->Features.Add(FixedFeature(Feature));
    }
    for (const TCHAR* Feature : {TEXT("asset_delete_force"), TEXT("asset_delete_undo"), TEXT("level_world_partition_conversion"), TEXT("project_build")})
    {
        Capabilities->Features.Add(FixedFeature(Feature, false));
    }
    auto ExtensionFeature = [this, Capabilities](const TCHAR* Name, const TCHAR* Family, EUnrealMCPExtensionAccess Access)
    {
        Capabilities->Features.Add({Name, [this, Family = FString(Family), Access]()
        {
            return ExtensionRegistry->HasReadyFamilyCapability(Family, Access);
        }});
    };
    ExtensionFeature(TEXT("gas_ability_blueprints_inspection"), TEXT("gameplay_ability"), EUnrealMCPExtensionAccess::Read);
    ExtensionFeature(TEXT("gas_ability_blueprints_mutation"), TEXT("gameplay_ability"), EUnrealMCPExtensionAccess::Mutation);
    ExtensionFeature(TEXT("gas_gameplay_effects_inspection"), TEXT("gameplay_effect"), EUnrealMCPExtensionAccess::Read);
    ExtensionFeature(TEXT("gas_gameplay_effects_mutation"), TEXT("gameplay_effect"), EUnrealMCPExtensionAccess::Mutation);
    ExtensionFeature(TEXT("commonui_widget_blueprints_inspection"), TEXT("commonui_widget"), EUnrealMCPExtensionAccess::Read);
    ExtensionFeature(TEXT("commonui_widget_blueprints_mutation"), TEXT("commonui_widget"), EUnrealMCPExtensionAccess::Mutation);

    Capabilities->Limits = {
        FixedLimit(TEXT("request_bytes"), UnrealMCP::MaxRequestBytes),
        FixedLimit(TEXT("companion_descriptors"), UnrealMCP::MaxDiscoveredCompanions), FixedLimit(TEXT("companions"), UnrealMCP::MaxAcceptedCompanions),
        FixedLimit(TEXT("companion_contributions"), UnrealMCP::MaxCompanionContributions), FixedLimit(TEXT("companion_capability_records"), UnrealMCP::MaxCompanionCapabilityRecords),
        FixedLimit(TEXT("companion_diagnostics"), UnrealMCP::MaxCompanionDiagnostics), FixedLimit(TEXT("extension_id_chars"), UnrealMCP::MaxExtensionIdChars),
        FixedLimit(TEXT("response_bytes"), UnrealMCP::MaxResponseBytes), FixedLimit(TEXT("queued_requests"), UnrealMCP::MaxQueuedRequests),
        FixedLimit(TEXT("json_depth"), UnrealMCP::MaxJsonDepth), FixedLimit(TEXT("string_chars"), UnrealMCP::MaxStringLength),
        FixedLimit(TEXT("command_deadline_ms"), UnrealMCP::CommandDeadlineSeconds * 1000.0), FixedLimit(TEXT("inspect_page_size"), UnrealMCP::MaxInspectPageSize),
        FixedLimit(TEXT("asset_inspect_page_size"), UnrealMCP::MaxAssetInspectPageSize), FixedLimit(TEXT("asset_inspect_selector_bytes"), UnrealMCP::MaxAssetInspectSelectorBytes),
        FixedLimit(TEXT("asset_inspect_complete_graph_bytes"), UnrealMCP::MaxAssetInspectCompleteGraphBytes), FixedLimit(TEXT("discovery_scan"), UnrealMCP::MaxDiscoveryScan),
        FixedLimit(TEXT("inspect_records"), UnrealMCP::MaxInspectRecords), FixedLimit(TEXT("retained_cursors"), UnrealMCP::MaxRetainedCursors),
        FixedLimit(TEXT("cursor_lifetime_ms"), UnrealMCP::CursorLifetimeSeconds * 1000.0), FixedLimit(TEXT("compiler_diagnostics"), UnrealMCP::MaxCompilerDiagnostics),
        FixedLimit(TEXT("diagnostic_chars"), UnrealMCP::MaxDiagnosticChars), FixedLimit(TEXT("retained_operations"), UnrealMCP::MaxRetainedOperations),
        FixedLimit(TEXT("operation_lifetime_ms"), UnrealMCP::OperationLifetimeSeconds * 1000.0), FixedLimit(TEXT("property_names"), UnrealMCP::MaxPropertyNames),
        FixedLimit(TEXT("variable_references"), UnrealMCP::MaxVariableReferences), FixedLimit(TEXT("action_results"), UnrealMCP::MaxActionResults),
        FixedLimit(TEXT("action_scan"), UnrealMCP::MaxActionScan), FixedLimit(TEXT("retained_actions"), UnrealMCP::MaxRetainedActions),
        FixedLimit(TEXT("retained_catalogs"), UnrealMCP::MaxRetainedCatalogs), FixedLimit(TEXT("action_lifetime_ms"), UnrealMCP::ActionLifetimeSeconds * 1000.0),
        FixedLimit(TEXT("action_scan_ms"), UnrealMCP::ActionScanSeconds * 1000.0), FixedLimit(TEXT("concurrent_catalogs"), UnrealMCP::MaxConcurrentCatalogs),
        FixedLimit(TEXT("graph_nodes"), UnrealMCP::MaxGraphNodes), FixedLimit(TEXT("graph_pins_per_node"), UnrealMCP::MaxGraphPinsPerNode),
        FixedLimit(TEXT("graph_coordinate"), UnrealMCP::MaxGraphCoordinate), FixedLimit(TEXT("graph_links_per_pin"), UnrealMCP::MaxGraphLinksPerPin),
        FixedLimit(TEXT("graph_automatic_conversion_nodes"), UnrealMCP::MaxAutomaticConversionNodes), FixedLimit(TEXT("pin_default_chars"), UnrealMCP::MaxPinDefaultChars),
        FixedLimit(TEXT("function_replacement_nodes"), UnrealMCP::MaxFunctionReplacementNodes), FixedLimit(TEXT("function_replacement_owned_nodes"), UnrealMCP::MaxFunctionReplacementOwnedNodes),
        FixedLimit(TEXT("function_replacement_locals"), UnrealMCP::MaxFunctionReplacementLocals), FixedLimit(TEXT("function_replacement_defaults"), UnrealMCP::MaxFunctionReplacementDefaults),
        FixedLimit(TEXT("function_replacement_connections"), UnrealMCP::MaxFunctionReplacementConnections), FixedLimit(TEXT("logic_unit_replacement_nodes"), UnrealMCP::MaxLogicUnitReplacementNodes),
        FixedLimit(TEXT("logic_unit_replacement_owned_nodes"), UnrealMCP::MaxLogicUnitOwnedNodes), FixedLimit(TEXT("logic_unit_replacement_locals"), UnrealMCP::MaxLogicUnitLocals),
        FixedLimit(TEXT("logic_unit_replacement_defaults"), UnrealMCP::MaxLogicUnitDefaults), FixedLimit(TEXT("logic_unit_replacement_connections"), UnrealMCP::MaxLogicUnitConnections),
        FixedLimit(TEXT("logic_unit_external_connections"), UnrealMCP::MaxLogicUnitExternalConnections), FixedLimit(TEXT("logic_unit_layout_nodes"), UnrealMCP::MaxLogicUnitLayoutNodes),
        FixedLimit(TEXT("logic_unit_layout_edges"), UnrealMCP::MaxLogicUnitLayoutEdges), FixedLimit(TEXT("logic_unit_layout_iterations"), UnrealMCP::MaxLogicUnitLayoutIterations),
        FixedLimit(TEXT("logic_unit_layout_collision_probes"), UnrealMCP::MaxLogicUnitLayoutCollisionProbes), FixedLimit(TEXT("logic_unit_layout_work"), UnrealMCP::MaxLogicUnitLayoutWork),
        FixedLimit(TEXT("logic_unit_layout_ms"), UnrealMCP::MaxLogicUnitLayoutSeconds * 1000.0), FixedLimit(TEXT("game_data_fields"), UnrealMCP::MaxGameDataFields),
        FixedLimit(TEXT("game_data_rows"), UnrealMCP::MaxGameDataRows), FixedLimit(TEXT("game_data_batch_rows"), UnrealMCP::MaxGameDataBatchRows),
        FixedLimit(TEXT("game_data_collection_items"), UnrealMCP::MaxGameDataCollectionItems), FixedLimit(TEXT("game_data_depth"), UnrealMCP::MaxGameDataDepth),
        FixedLimit(TEXT("game_data_dependencies"), UnrealMCP::MaxGameDataDependencies), FixedLimit(TEXT("asset_reference_registry_candidates"), UnrealMCP::MaxAssetReferenceRegistryCandidates),
        FixedLimit(TEXT("asset_reference_live_objects"), UnrealMCP::MaxAssetReferenceLiveObjects), FixedLimit(TEXT("asset_reference_records"), UnrealMCP::MaxAssetReferenceRecords),
        FixedLimit(TEXT("asset_reference_assets_per_package"), UnrealMCP::MaxAssetReferenceAssetsPerPackage), FixedLimit(TEXT("asset_reference_properties"), UnrealMCP::MaxAssetReferenceProperties),
        FixedLimit(TEXT("asset_reference_retained_cursors"), UnrealMCP::MaxAssetReferenceRetainedCursors), FixedLimit(TEXT("asset_reference_traversal_depth"), 1),
        FixedLimit(TEXT("level_discovery_scan"), UnrealMCP::MaxLevelDiscoveryScan), FixedLimit(TEXT("level_external_packages"), UnrealMCP::MaxLevelExternalPackages),
        FixedLimit(TEXT("level_actor_scan"), UnrealMCP::MaxLevelActorScan), FixedLimit(TEXT("level_actor_records"), UnrealMCP::MaxLevelActorRecords),
        FixedLimit(TEXT("level_components"), UnrealMCP::MaxLevelComponents), FixedLimit(TEXT("level_actor_tags"), UnrealMCP::MaxLevelActorTags),
        FixedLimit(TEXT("level_data_layers"), UnrealMCP::MaxLevelDataLayers), FixedLimit(TEXT("level_targeted_loads"), UnrealMCP::MaxLevelTargetedLoads),
        FixedLimit(TEXT("level_setup_properties"), UnrealMCP::MaxLevelSetupProperties), FixedLimit(TEXT("level_owned_packages"), UnrealMCP::MaxLevelOwnedPackages),
        FixedLimit(TEXT("level_edit_operations"), UnrealMCP::MaxLevelEditOperations), FixedLimit(TEXT("level_edit_actors"), UnrealMCP::MaxLevelEditActors),
        FixedLimit(TEXT("level_save_packages"), UnrealMCP::MaxLevelSavePackages), FixedLimit(TEXT("dirty_package_summary"), UnrealMCP::MaxDirtyPackageSummary),
        FixedLimit(TEXT("widget_tree_widgets"), UnrealMCP::MaxWidgetTreeWidgets), FixedLimit(TEXT("widget_tree_depth"), UnrealMCP::MaxWidgetTreeDepth),
        FixedLimit(TEXT("widget_named_slots"), UnrealMCP::MaxWidgetNamedSlots), FixedLimit(TEXT("widget_defaults_per_widget"), UnrealMCP::MaxWidgetDefaultsPerWidget),
        FixedLimit(TEXT("widget_changed_defaults"), UnrealMCP::MaxWidgetChangedDefaults), FixedLimit(TEXT("widget_bindings"), UnrealMCP::MaxWidgetBindings)};
    Catalog.Freeze(InitializationError);
}

bool FUnrealMCPCommandCatalog::Execute(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    const FUnrealMCPCommandDescriptor* Descriptor = Catalog.Find(Command);
    if (Descriptor == nullptr)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown or unavailable command")};
        return false;
    }
    if (Descriptor->bAllowsExtensionRequests && ExtensionRegistry->HasExtensionRequest(Arguments))
    {
        return ExtensionRegistry->Execute(Command, Arguments, OutResult, OutError);
    }
    return Descriptor->Handler(Arguments, OutResult, OutError);
}

TArray<TSharedPtr<FUnrealMCPValue>> FUnrealMCPCommandCatalog::BuildBlueprintFamilyCapabilities() const
{
    TArray<TSharedPtr<FUnrealMCPValue>> Result = UnrealMCP::BlueprintFamilyPolicy::BuildPublishedMatrix();
    Result.Append(ExtensionRegistry->BuildBlueprintFamilyCapabilities());
    return Result;
}

bool FUnrealMCPCommandCatalog::RejectConcurrentRetainedOperation(const TCHAR* Message, FUnrealMCPError& OutError) const
{
    const TSharedPtr<FUnrealMCPRecord> State = OperationLedger.CurrentState();
    if (static_cast<int32>(State->GetNumberField(TEXT("queued"))) > 0
        || static_cast<int32>(State->GetNumberField(TEXT("executing"))) > 1)
    {
        OutError = {TEXT("busy"), Message, MakeShared<FUnrealMCPRecord>(), true};
        return true;
    }
    return false;
}

bool FUnrealMCPCommandCatalog::ExecuteAssetInspect(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!AssetInspectionService) AssetInspectionService = MakeUnique<FUnrealMCPAssetInspectionService>(AssetFamilyRegistry);
    return AssetInspectionService->Execute(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteAssetReferences(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!AssetReferenceService) AssetReferenceService = MakeUnique<FUnrealMCPAssetReferenceService>();
    return AssetReferenceService->Inspect(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteAssetDelete(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!AssetReferenceService) AssetReferenceService = MakeUnique<FUnrealMCPAssetReferenceService>();
    if (!AssetDeletionService) AssetDeletionService = MakeUnique<FUnrealMCPAssetDeletionService>(*AssetReferenceService);
    if (RejectConcurrentRetainedOperation(TEXT("Asset deletion refused while another retained operation is queued or executing"), E)) return false;
    return AssetDeletionService->Delete(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteLevelInspect(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!LevelService) LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
    return LevelService->Inspect(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteLevelOpen(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!LevelService) LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
    if (RejectConcurrentRetainedOperation(TEXT("Level operation refused while another retained operation is queued or executing"), E)) return false;
    return LevelService->Open(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteLevelManage(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!LevelService) LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
    if (RejectConcurrentRetainedOperation(TEXT("Level operation refused while another retained operation is queued or executing"), E)) return false;
    if (!LevelManagementService) LevelManagementService = MakeUnique<FUnrealMCPLevelManagementService>(ProjectHash, *LevelService);
    return LevelManagementService->Manage(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteLevelActorEdit(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!LevelService) LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
    if (RejectConcurrentRetainedOperation(TEXT("Level operation refused while another retained operation is queued or executing"), E)) return false;
    if (!LevelActorEditingService) LevelActorEditingService = MakeUnique<FUnrealMCPLevelActorEditingService>(*LevelService);
    return LevelActorEditingService->Edit(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteLevelSave(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!LevelService) LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
    if (RejectConcurrentRetainedOperation(TEXT("Level operation refused while another retained operation is queued or executing"), E)) return false;
    if (!LevelActorEditingService) LevelActorEditingService = MakeUnique<FUnrealMCPLevelActorEditingService>(*LevelService);
    return LevelActorEditingService->Save(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteBlueprintActionCatalog(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!BlueprintInspector) BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(*ExtensionRegistry);
    if (!BlueprintActionCatalog) BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(*BlueprintInspector, BridgeInstanceId);
    return BlueprintActionCatalog->Execute(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteBlueprintGraphEdit(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!BlueprintInspector) BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(*ExtensionRegistry);
    if (!BlueprintActionCatalog) BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(*BlueprintInspector, BridgeInstanceId);
    if (!BlueprintGraphEditor) BlueprintGraphEditor = MakeUnique<FUnrealMCPBlueprintGraphEditor>(*BlueprintInspector, *BlueprintActionCatalog);
    return BlueprintGraphEditor->Execute(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteBlueprintBlockReplace(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!BlueprintInspector) BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(*ExtensionRegistry);
    if (!BlueprintActionCatalog) BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(*BlueprintInspector, BridgeInstanceId);
    if (!BlueprintBlockReplacementService) BlueprintBlockReplacementService = MakeUnique<FUnrealMCPBlueprintBlockReplacementService>(*BlueprintInspector, *BlueprintActionCatalog);
    return BlueprintBlockReplacementService->Execute(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteBlueprintMutation(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!BlueprintInspector) BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(*ExtensionRegistry);
    if (!BlueprintMutator) BlueprintMutator = MakeUnique<FUnrealMCPBlueprintMutator>(*BlueprintInspector);
    return BlueprintMutator->Execute(Command, A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteWidgetTreeEdit(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!BlueprintInspector) BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(*ExtensionRegistry);
    if (!WidgetTreeService) WidgetTreeService = MakeUnique<FUnrealMCPWidgetTreeService>(*BlueprintInspector);
    return WidgetTreeService->Execute(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteGameplayFrameworkEdit(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!GameplayFrameworkEditor) GameplayFrameworkEditor = MakeUnique<FUnrealMCPGameplayFrameworkEditor>(ProjectHash);
    return GameplayFrameworkEditor->Execute(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteGameDataInspect(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!GameDataService) GameDataService = MakeUnique<FUnrealMCPGameDataService>();
    return GameDataService->Inspect(A, R, E);
}
bool FUnrealMCPCommandCatalog::ExecuteGameDataEdit(const TSharedPtr<FUnrealMCPRecord>& A, TSharedPtr<FUnrealMCPRecord>& R, FUnrealMCPError& E)
{
    if (!GameDataService) GameDataService = MakeUnique<FUnrealMCPGameDataService>();
    return GameDataService->Edit(A, R, E);
}
