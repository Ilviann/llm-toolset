#include "Modules/ModuleManager.h"

#include "UnrealMCPAssetDeletionService.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPDomainModule.h"
#include "UnrealMCPGameDataService.h"
#include "UnrealMCPLevelActorEditingService.h"
#include "UnrealMCPLevelManagementService.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::ContentDomain::Private
{
struct FRuntimeState
{
    FRuntimeState(
        FString InProjectHash,
        TFunction<bool(const TCHAR*, FUnrealMCPError&)> InRejectConcurrent)
        : ProjectHash(MoveTemp(InProjectHash))
        , RejectConcurrent(MoveTemp(InRejectConcurrent))
    {
    }

    FUnrealMCPAssetReferenceService& References()
    {
        if (!AssetReferences) AssetReferences = MakeUnique<FUnrealMCPAssetReferenceService>();
        return *AssetReferences;
    }
    FUnrealMCPLevelService& Levels()
    {
        if (!LevelService) LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
        return *LevelService;
    }
    bool RejectAssetDelete(FUnrealMCPError& E) const
    {
        return RejectConcurrent(TEXT("Asset deletion refused while another retained operation is queued or executing"), E);
    }
    bool RejectLevel(FUnrealMCPError& E) const
    {
        return RejectConcurrent(TEXT("Level operation refused while another retained operation is queued or executing"), E);
    }

    bool InspectReferences(const auto& A, auto& R, auto& E) { return References().Inspect(A, R, E); }
    bool DeleteAsset(const auto& A, auto& R, auto& E)
    {
        if (RejectAssetDelete(E)) return false;
        if (!AssetDeletion) AssetDeletion = MakeUnique<FUnrealMCPAssetDeletionService>(References());
        return AssetDeletion->Delete(A, R, E);
    }
    bool InspectLevel(const auto& A, auto& R, auto& E) { return Levels().Inspect(A, R, E); }
    bool OpenLevel(const auto& A, auto& R, auto& E)
    {
        if (RejectLevel(E)) return false;
        return Levels().Open(A, R, E);
    }
    bool ManageLevel(const auto& A, auto& R, auto& E)
    {
        if (RejectLevel(E)) return false;
        if (!LevelManagement) LevelManagement = MakeUnique<FUnrealMCPLevelManagementService>(ProjectHash, Levels());
        return LevelManagement->Manage(A, R, E);
    }
    bool EditLevelActors(const auto& A, auto& R, auto& E)
    {
        if (RejectLevel(E)) return false;
        if (!LevelActorEditing) LevelActorEditing = MakeUnique<FUnrealMCPLevelActorEditingService>(Levels());
        return LevelActorEditing->Edit(A, R, E);
    }
    bool SaveLevel(const auto& A, auto& R, auto& E)
    {
        if (RejectLevel(E)) return false;
        if (!LevelActorEditing) LevelActorEditing = MakeUnique<FUnrealMCPLevelActorEditingService>(Levels());
        return LevelActorEditing->Save(A, R, E);
    }
    bool InspectGameData(const auto& A, auto& R, auto& E)
    {
        if (!GameData) GameData = MakeUnique<FUnrealMCPGameDataService>();
        return GameData->Inspect(A, R, E);
    }
    bool EditGameData(const auto& A, auto& R, auto& E)
    {
        if (!GameData) GameData = MakeUnique<FUnrealMCPGameDataService>();
        return GameData->Edit(A, R, E);
    }

    FString ProjectHash;
    TFunction<bool(const TCHAR*, FUnrealMCPError&)> RejectConcurrent;
    TUniquePtr<FUnrealMCPAssetReferenceService> AssetReferences;
    TUniquePtr<FUnrealMCPAssetDeletionService> AssetDeletion;
    TUniquePtr<FUnrealMCPLevelService> LevelService;
    TUniquePtr<FUnrealMCPLevelManagementService> LevelManagement;
    TUniquePtr<FUnrealMCPLevelActorEditingService> LevelActorEditing;
    TUniquePtr<FUnrealMCPGameDataService> GameData;
};

FUnrealMCPCommandDescriptor Command(
    const TCHAR* Identity,
    EUnrealMCPCommandAccess Access,
    EUnrealMCPRetainedOperationPolicy Retained,
    FUnrealMCPCommandHandler Handler)
{
    FUnrealMCPCommandDescriptor Result;
    Result.Identity = Identity;
    const TMap<FString, int32> Orders = {
        {TEXT("asset_references"), 6}, {TEXT("asset_delete"), 7},
        {TEXT("level_inspect"), 8}, {TEXT("level_open"), 9},
        {TEXT("level_manage"), 10}, {TEXT("level_actor_edit"), 11},
        {TEXT("level_save"), 12},
        {TEXT("game_data_inspect"), 24}, {TEXT("game_data_edit"), 25}};
    Result.Order = Orders.FindChecked(Result.Identity);
    Result.Access = Access;
    Result.RetainedOperation = Retained;
    Result.Dispatch = EUnrealMCPCommandDispatch::GameThread;
    Result.Handler = MoveTemp(Handler);
    return Result;
}
}

class FUnrealMCPContentModule final : public IUnrealMCPBuiltInDomainModule
{
public:
    FName GetDomainName() const override { return TEXT("content"); }
    bool RegisterAssetFamilies(FUnrealMCPAssetFamilyRegistry&, FUnrealMCPError&) override { return true; }

    bool RegisterCommands(
        const FUnrealMCPDomainRegistrar& Registrar,
        const FUnrealMCPDomainContext& Context,
        FString& OutError) override
    {
        using namespace UnrealMCP::ContentDomain::Private;
        using Access = EUnrealMCPCommandAccess;
        using Retained = EUnrealMCPRetainedOperationPolicy;
        const TSharedRef<FRuntimeState> State = MakeShared<FRuntimeState>(
            Context.ProjectHash, Context.RejectConcurrentRetainedOperation);
        const TArray<FUnrealMCPCommandDescriptor> Commands = {
            Command(TEXT("asset_references"), Access::ReadOnly, Retained::None,
                [State](const auto& A, auto& R, auto& E) { return State->InspectReferences(A, R, E); }),
            Command(TEXT("asset_delete"), Access::Writable, Retained::Retained,
                [State](const auto& A, auto& R, auto& E) { return State->DeleteAsset(A, R, E); }),
            Command(TEXT("level_inspect"), Access::ReadOnly, Retained::None,
                [State](const auto& A, auto& R, auto& E) { return State->InspectLevel(A, R, E); }),
            Command(TEXT("level_open"), Access::ReadOnly, Retained::Retained,
                [State](const auto& A, auto& R, auto& E) { return State->OpenLevel(A, R, E); }),
            Command(TEXT("level_manage"), Access::Writable, Retained::Retained,
                [State](const auto& A, auto& R, auto& E) { return State->ManageLevel(A, R, E); }),
            Command(TEXT("level_actor_edit"), Access::Writable, Retained::Retained,
                [State](const auto& A, auto& R, auto& E) { return State->EditLevelActors(A, R, E); }),
            Command(TEXT("level_save"), Access::Writable, Retained::Retained,
                [State](const auto& A, auto& R, auto& E) { return State->SaveLevel(A, R, E); }),
            Command(TEXT("game_data_inspect"), Access::ReadOnly, Retained::None,
                [State](const auto& A, auto& R, auto& E) { return State->InspectGameData(A, R, E); }),
            Command(TEXT("game_data_edit"), Access::Writable, Retained::Retained,
                [State](const auto& A, auto& R, auto& E) { return State->EditGameData(A, R, E); })};
        for (FUnrealMCPCommandDescriptor Descriptor : Commands)
        {
            if (!Registrar.RegisterCommand(MoveTemp(Descriptor), OutError)) return false;
        }
        for (const TCHAR* Feature : {
            TEXT("user_defined_struct_authoring"), TEXT("typed_data_tables"),
            TEXT("game_data_batch_editing"), TEXT("asset_reference_discovery"), TEXT("asset_reference_live_memory"),
            TEXT("asset_delete"), TEXT("level_discovery"), TEXT("level_open"), TEXT("level_snapshots"),
            TEXT("level_management"), TEXT("level_blank_creation"), TEXT("level_template_creation"),
            TEXT("level_world_settings"), TEXT("level_map_deletion"), TEXT("level_actor_inspection"),
            TEXT("level_world_partition_descriptors"), TEXT("level_targeted_actor_loading"),
            TEXT("level_instance_properties"), TEXT("level_actor_editing"), TEXT("level_actor_transactions"),
            TEXT("level_package_save_verification")})
        {
            if (!Registrar.RegisterFeature({Feature, [] { return true; }}, OutError)) return false;
        }
        for (const TCHAR* Feature : {
            TEXT("asset_delete_force"), TEXT("asset_delete_undo"),
            TEXT("level_world_partition_conversion")})
        {
            if (!Registrar.RegisterFeature({Feature, [] { return false; }}, OutError)) return false;
        }
        for (const FUnrealMCPNativeLimit Limit : {
            FUnrealMCPNativeLimit{TEXT("game_data_fields"), UnrealMCP::MaxGameDataFields},
            {TEXT("game_data_rows"), UnrealMCP::MaxGameDataRows},
            {TEXT("game_data_batch_rows"), UnrealMCP::MaxGameDataBatchRows},
            {TEXT("game_data_collection_items"), UnrealMCP::MaxGameDataCollectionItems},
            {TEXT("game_data_depth"), UnrealMCP::MaxGameDataDepth},
            {TEXT("game_data_dependencies"), UnrealMCP::MaxGameDataDependencies},
            {TEXT("asset_reference_registry_candidates"), UnrealMCP::MaxAssetReferenceRegistryCandidates},
            {TEXT("asset_reference_live_objects"), UnrealMCP::MaxAssetReferenceLiveObjects},
            {TEXT("asset_reference_records"), UnrealMCP::MaxAssetReferenceRecords},
            {TEXT("asset_reference_assets_per_package"), UnrealMCP::MaxAssetReferenceAssetsPerPackage},
            {TEXT("asset_reference_properties"), UnrealMCP::MaxAssetReferenceProperties},
            {TEXT("asset_reference_retained_cursors"), UnrealMCP::MaxAssetReferenceRetainedCursors},
            {TEXT("asset_reference_traversal_depth"), 1},
            {TEXT("level_discovery_scan"), UnrealMCP::MaxLevelDiscoveryScan},
            {TEXT("level_external_packages"), UnrealMCP::MaxLevelExternalPackages},
            {TEXT("level_actor_scan"), UnrealMCP::MaxLevelActorScan},
            {TEXT("level_actor_records"), UnrealMCP::MaxLevelActorRecords},
            {TEXT("level_components"), UnrealMCP::MaxLevelComponents},
            {TEXT("level_actor_tags"), UnrealMCP::MaxLevelActorTags},
            {TEXT("level_data_layers"), UnrealMCP::MaxLevelDataLayers},
            {TEXT("level_targeted_loads"), UnrealMCP::MaxLevelTargetedLoads},
            {TEXT("level_setup_properties"), UnrealMCP::MaxLevelSetupProperties},
            {TEXT("level_owned_packages"), UnrealMCP::MaxLevelOwnedPackages},
            {TEXT("level_edit_operations"), UnrealMCP::MaxLevelEditOperations},
            {TEXT("level_edit_actors"), UnrealMCP::MaxLevelEditActors},
            {TEXT("level_save_packages"), UnrealMCP::MaxLevelSavePackages},
            {TEXT("dirty_package_summary"), UnrealMCP::MaxDirtyPackageSummary}})
        {
            if (!Registrar.RegisterLimit(Limit, OutError)) return false;
        }
        return true;
    }
};

IMPLEMENT_MODULE(FUnrealMCPContentModule, UnrealMCPContent)
