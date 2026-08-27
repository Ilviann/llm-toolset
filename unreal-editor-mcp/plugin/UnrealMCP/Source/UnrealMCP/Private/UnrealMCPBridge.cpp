#include "UnrealMCPBridge.h"

#include "Async/Async.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPDiscovery.h"
#include "UnrealMCPExtensionRegistry.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintBlockReplacementService.h"
#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPWidgetTreeService.h"
#include "UnrealMCPGameplayFrameworkEditor.h"
#include "UnrealMCPGameDataService.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPLevelManagementService.h"
#include "UnrealMCPLevelActorEditingService.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPAssetDeletionService.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPOperationLedger.h"
#include "UnrealMCPVersion.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"

namespace
{
const FHttpPath RoutePath(TEXT("/unreal-mcp/v1/command"));

TArray<TSharedPtr<FJsonValue>> Strings(std::initializer_list<const TCHAR*> Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(static_cast<int32>(Values.size()));
    for (const TCHAR* Value : Values)
    {
        Result.Add(MakeShared<FJsonValueString>(Value));
    }
    return Result;
}

FString Header(const FHttpServerRequest& Request, const TCHAR* LowercaseName)
{
    const TArray<FString>* Values = Request.Headers.Find(LowercaseName);
    return Values != nullptr && Values->Num() == 1 ? (*Values)[0] : FString();
}

bool IsRetainedOperationCommand(const FString& Command)
{
    return Command == TEXT("asset_delete") || Command == TEXT("level_open") || Command == TEXT("level_manage")
        || Command == TEXT("level_actor_edit") || Command == TEXT("level_save")
        || Command == TEXT("blueprint_create") || Command == TEXT("blueprint_compile") || Command == TEXT("blueprint_save")
        || Command == TEXT("blueprint_component_edit") || Command == TEXT("blueprint_default_edit")
        || Command == TEXT("blueprint_member_edit") || Command == TEXT("blueprint_graph_edit")
        || Command == TEXT("blueprint_block_replace")
        || Command == TEXT("widget_tree_edit")
        || Command == TEXT("gameplay_framework_edit") || Command == TEXT("game_data_edit");
}

FString AuthenticationBinding(const FString& ProjectHash, const FString& BridgeInstanceId, const FString& Token)
{
    const FString Material = ProjectHash + TEXT("|") + BridgeInstanceId + TEXT("|") + Token;
    FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}
}

FUnrealMCPBridge::FUnrealMCPBridge(
    FString InToken,
    FString InStateDirectory,
    FString InProjectHash,
    uint32 InPort,
    TSharedRef<FUnrealMCPExtensionRegistry> InExtensionRegistry)
    : Token(MoveTemp(InToken)), StateDirectory(MoveTemp(InStateDirectory)),
      ProjectHash(MoveTemp(InProjectHash)), Port(InPort), ExtensionRegistry(MoveTemp(InExtensionRegistry))
{
    BridgeInstanceId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    OperationLedger = MakeUnique<FUnrealMCPOperationLedger>(BridgeInstanceId, AuthenticationBinding(ProjectHash, BridgeInstanceId, Token));
}

FUnrealMCPBridge::~FUnrealMCPBridge()
{
    Stop();
}

bool FUnrealMCPBridge::Start(FString& OutError)
{
    check(IsInGameThread());
    if (bReady)
    {
        OutError = TEXT("bridge is already running");
        return false;
    }
    bStopping = false;
    FHttpServerModule& Server = FHttpServerModule::Get();
    Router = Server.GetHttpRouter(Port, false);
    if (!Router.IsValid())
    {
        OutError = TEXT("could not create HTTP router");
        return false;
    }
    Route = Router->BindRoute(
        RoutePath,
        EHttpServerRequestVerbs::VERB_POST,
        FHttpRequestHandler::CreateSP(AsShared(), &FUnrealMCPBridge::HandleRequest));
    if (!Route.IsValid())
    {
        Router.Reset();
        OutError = TEXT("bridge route is already owned");
        return false;
    }
    Server.StartAllListeners();
    if (!Server.GetHttpRouter(Port, true).IsValid())
    {
        Router->UnbindRoute(Route);
        Route.Reset();
        Router.Reset();
        OutError = TEXT("could not bind loopback listener");
        return false;
    }

    Discovery = MakeUnique<FUnrealMCPDiscovery>(StateDirectory, ProjectHash, Port);
    if (!Discovery->Write(OutError))
    {
        Router->UnbindRoute(Route);
        Route.Reset();
        Router.Reset();
        Discovery.Reset();
        return false;
    }
    bReady = true;
    HeartbeatHandle = FTSTicker::GetCoreTicker().AddTicker(
        TEXT("UnrealMCPHeartbeat"),
        UnrealMCP::HeartbeatIntervalSeconds,
        [Weak = TWeakPtr<FUnrealMCPBridge>(AsShared())](float DeltaTime)
        {
            const TSharedPtr<FUnrealMCPBridge> Pinned = Weak.Pin();
            return Pinned.IsValid() && Pinned->Heartbeat(DeltaTime);
        });
    return true;
}

void FUnrealMCPBridge::Stop()
{
    if (bStopping.Exchange(true))
    {
        return;
    }
    bReady = false;
    if (OperationLedger)
    {
        OperationLedger->CancelQueued();
    }
    if (HeartbeatHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
        HeartbeatHandle.Reset();
    }
    if (Discovery)
    {
        Discovery->Remove();
        Discovery.Reset();
    }
    if (Router.IsValid() && Route.IsValid())
    {
        Router->UnbindRoute(Route);
    }
    Route.Reset();
    Router.Reset();
    BlueprintGraphEditor.Reset();
    BlueprintBlockReplacementService.Reset();
    BlueprintMutator.Reset();
    WidgetTreeService.Reset();
    GameplayFrameworkEditor.Reset();
    GameDataService.Reset();
    LevelActorEditingService.Reset();
    LevelManagementService.Reset();
    LevelService.Reset();
    AssetDeletionService.Reset();
    AssetReferenceService.Reset();
    BlueprintActionCatalog.Reset();
    BlueprintInspector.Reset();
    Token.Reset();
}

bool FUnrealMCPBridge::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& Complete)
{
    if (bStopping || bShutdownAccepted || !bReady)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetBoolField(TEXT("stopping"), bStopping);
        Details->SetBoolField(TEXT("shutdown_accepted"), bShutdownAccepted);
        Details->SetBoolField(TEXT("bridge_ready"), bReady);
        Complete(UnrealMCP::Protocol::Error(
            EHttpServerResponseCodes::ServiceUnavail,
            FUnrealMCPError{
                TEXT("editor_unavailable"),
                bShutdownAccepted || bStopping ? TEXT("Bridge is shutting down") : TEXT("Bridge is not ready"),
                Details,
                true}));
        return true;
    }
    const FString Authorization = Header(Request, TEXT("authorization"));
    const FString Expected = FString(TEXT("Bearer ")) + Token;
    if (!UnrealMCP::Protocol::ConstantTimeEquals(Authorization, Expected))
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::Denied, TEXT("authentication_failed"), TEXT("Authentication failed")));
        return true;
    }
    if (Request.Body.Num() <= 0 || Request.Body.Num() > UnrealMCP::MaxRequestBytes)
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::RequestTooLarge, TEXT("request_too_large"), TEXT("Request body exceeds the configured limit")));
        return true;
    }
    FString Command;
    TSharedPtr<FJsonObject> Arguments;
    FUnrealMCPError ParseError;
    if (!UnrealMCP::Protocol::ParseCommand(Request.Body, Command, Arguments, ParseError))
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, ParseError));
        return true;
    }
    if (Command != TEXT("capabilities") && Command != TEXT("editor_state") && Command != TEXT("editor_shutdown")
        && Command != TEXT("operation_status") && Command != TEXT("operation_cancel") && Command != TEXT("asset_references")
        && Command != TEXT("asset_delete")
        && Command != TEXT("level_inspect") && Command != TEXT("level_open") && Command != TEXT("level_manage")
        && Command != TEXT("level_actor_edit") && Command != TEXT("level_save")
        && Command != TEXT("blueprint_inspect") && Command != TEXT("blueprint_create") && Command != TEXT("blueprint_compile")
        && Command != TEXT("blueprint_save") && Command != TEXT("blueprint_component_edit") && Command != TEXT("blueprint_default_edit")
        && Command != TEXT("blueprint_member_edit") && Command != TEXT("blueprint_action_catalog")
        && Command != TEXT("blueprint_graph_edit") && Command != TEXT("blueprint_block_replace")
        && Command != TEXT("gameplay_framework_edit")
        && Command != TEXT("widget_tree_edit")
        && Command != TEXT("game_data_inspect") && Command != TEXT("game_data_edit"))
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, TEXT("invalid_argument"), TEXT("Unknown or unavailable command")));
        return true;
    }
    if (Command == TEXT("operation_status") || Command == TEXT("operation_cancel"))
    {
        TSharedPtr<FJsonObject> Status;
        FUnrealMCPError Error;
        const bool bResolved = OperationLedger
            && (Command == TEXT("operation_status")
                ? OperationLedger->Status(Arguments, Status, Error)
                : OperationLedger->Cancel(Arguments, Status, Error));
        if (!bResolved)
        {
            Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, Error));
        }
        else
        {
            Complete(UnrealMCP::Protocol::Success(Status));
        }
        return true;
    }
    if (Pending.Load() >= UnrealMCP::MaxQueuedRequests)
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::TooManyRequests, TEXT("busy"), TEXT("Bridge request queue is full"), true));
        return true;
    }
    FString OperationId;
    FString RequestDigest;
    if (IsRetainedOperationCommand(Command))
    {
        const FUnrealMCPOperationAdmission Admission = OperationLedger->Admit(Command, Arguments);
        OperationId = Admission.OperationId;
        RequestDigest = Admission.RequestDigest;
        if (Admission.Kind == EUnrealMCPOperationAdmission::ReplaySuccess)
        {
            Complete(UnrealMCP::Protocol::Success(Admission.Result));
            return true;
        }
        if (Admission.Kind != EUnrealMCPOperationAdmission::Accepted)
        {
            Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest,
                Admission.Error != nullptr ? *Admission.Error : FUnrealMCPError{TEXT("internal_error"), TEXT("Operation admission failed")}));
            return true;
        }
    }
    ++Pending;
    DispatchOnGameThread(MoveTemp(Command), MoveTemp(Arguments), MoveTemp(OperationId), MoveTemp(RequestDigest), Complete, FPlatformTime::Seconds());
    return true;
}

void FUnrealMCPBridge::DispatchOnGameThread(
    FString Command,
    TSharedPtr<FJsonObject> Arguments,
    FString OperationId,
    FString RequestDigest,
    const FHttpResultCallback& Complete,
    double AcceptedAt)
{
    AsyncTask(ENamedThreads::GameThread, [Weak = TWeakPtr<FUnrealMCPBridge>(AsShared()), Command = MoveTemp(Command),
        Arguments = MoveTemp(Arguments), OperationId = MoveTemp(OperationId), RequestDigest = MoveTemp(RequestDigest), Complete, AcceptedAt]() mutable
    {
        const TSharedPtr<FUnrealMCPBridge> Pinned = Weak.Pin();
        if (!Pinned.IsValid())
        {
            Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::ServiceUnavail, TEXT("cancelled"), TEXT("Bridge unloaded before dispatch")));
            return;
        }
        --Pinned->Pending;
        if (Pinned->bStopping || Pinned->bShutdownAccepted)
        {
            Complete(UnrealMCP::Protocol::Error(
                EHttpServerResponseCodes::ServiceUnavail,
                TEXT("cancelled"),
                TEXT("Bridge is shutting down")));
            return;
        }
        if (FPlatformTime::Seconds() - AcceptedAt > UnrealMCP::CommandDeadlineSeconds)
        {
            FUnrealMCPError Timeout{TEXT("timeout"), TEXT("Command expired before Game-thread dispatch"), MakeShared<FJsonObject>(), true};
            if (!OperationId.IsEmpty() && Pinned->OperationLedger) Pinned->OperationLedger->Reject(OperationId, Timeout);
            Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::GatewayTimeout, Timeout));
            return;
        }
        if (!OperationId.IsEmpty() && Pinned->OperationLedger)
        {
            FUnrealMCPError AdmissionError;
            if (!Pinned->OperationLedger->MarkExecuting(OperationId, AdmissionError))
            {
                Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, AdmissionError));
                return;
            }
        }
        TSharedPtr<FJsonObject> Result;
        FUnrealMCPError Error;
        if (!Pinned->Execute(Command, Arguments, Result, Error))
        {
            if (!OperationId.IsEmpty() && Pinned->OperationLedger) Pinned->OperationLedger->Reject(OperationId, Error);
            Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, Error));
            return;
        }
        if (!OperationId.IsEmpty())
        {
            FString OperationState = TEXT("committed");
            Result->TryGetStringField(TEXT("operation_state"), OperationState);
            if (OperationState != TEXT("partial") && OperationState != TEXT("outcome_unknown"))
            {
                OperationState = TEXT("committed");
            }
            Result->SetStringField(TEXT("operation_id"), OperationId);
            Result->SetStringField(TEXT("operation_state"), OperationState);
            Result->SetStringField(TEXT("bridge_instance_id"), Pinned->BridgeInstanceId);
            Result->SetStringField(TEXT("request_digest"), RequestDigest);
            Pinned->OperationLedger->Complete(OperationId, OperationState, Result);
        }
        Complete(UnrealMCP::Protocol::Success(Result));
    });
}

bool FUnrealMCPBridge::Execute(const FString& Command, const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (Command == TEXT("capabilities"))
    {
        OutResult = Capabilities();
        return true;
    }
    if (Command == TEXT("editor_state"))
    {
        OutResult = EditorState();
        return true;
    }
    if (Command == TEXT("editor_shutdown"))
    {
        return EditorShutdown(OutResult, OutError);
    }
    if (ExtensionRegistry->HasExtensionRequest(Arguments))
    {
        return ExtensionRegistry->Execute(Command, Arguments, OutResult, OutError);
    }
    if (Command == TEXT("asset_references"))
    {
        if (!AssetReferenceService)
        {
            AssetReferenceService = MakeUnique<FUnrealMCPAssetReferenceService>();
        }
        return AssetReferenceService->Inspect(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("asset_delete"))
    {
        if (!AssetReferenceService)
        {
            AssetReferenceService = MakeUnique<FUnrealMCPAssetReferenceService>();
        }
        if (!AssetDeletionService)
        {
            AssetDeletionService =
                MakeUnique<FUnrealMCPAssetDeletionService>(*AssetReferenceService);
        }
        if (OperationLedger)
        {
            const TSharedPtr<FJsonObject> State = OperationLedger->CurrentState();
            if (static_cast<int32>(State->GetNumberField(TEXT("queued"))) > 0
                || static_cast<int32>(State->GetNumberField(TEXT("executing"))) > 1)
            {
                OutError = {
                    TEXT("busy"),
                    TEXT("Asset deletion refused while another retained operation is queued or executing"),
                    MakeShared<FJsonObject>(),
                    true};
                return false;
            }
        }
        return AssetDeletionService->Delete(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("level_inspect") || Command == TEXT("level_open") || Command == TEXT("level_manage")
        || Command == TEXT("level_actor_edit") || Command == TEXT("level_save"))
    {
        if (!LevelService)
        {
            LevelService = MakeUnique<FUnrealMCPLevelService>(ProjectHash);
        }
        if (Command != TEXT("level_inspect") && OperationLedger)
        {
            const TSharedPtr<FJsonObject> State = OperationLedger->CurrentState();
            if (static_cast<int32>(State->GetNumberField(TEXT("queued"))) > 0
                || static_cast<int32>(State->GetNumberField(TEXT("executing"))) > 1)
            {
                OutError = {
                    TEXT("busy"),
                    TEXT("Level operation refused while another retained operation is queued or executing"),
                    MakeShared<FJsonObject>(),
                    true};
                return false;
            }
        }
        if (Command == TEXT("level_inspect")) return LevelService->Inspect(Arguments, OutResult, OutError);
        if (Command == TEXT("level_open")) return LevelService->Open(Arguments, OutResult, OutError);
        if (Command == TEXT("level_actor_edit") || Command == TEXT("level_save"))
        {
            if (!LevelActorEditingService)
                LevelActorEditingService = MakeUnique<FUnrealMCPLevelActorEditingService>(*LevelService);
            return Command == TEXT("level_actor_edit")
                ? LevelActorEditingService->Edit(Arguments, OutResult, OutError)
                : LevelActorEditingService->Save(Arguments, OutResult, OutError);
        }
        if (!LevelManagementService)
        {
            LevelManagementService = MakeUnique<FUnrealMCPLevelManagementService>(ProjectHash, *LevelService);
        }
        return LevelManagementService->Manage(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("gameplay_framework_edit"))
    {
        if (!GameplayFrameworkEditor) GameplayFrameworkEditor = MakeUnique<FUnrealMCPGameplayFrameworkEditor>(ProjectHash);
        return GameplayFrameworkEditor->Execute(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("game_data_inspect") || Command == TEXT("game_data_edit"))
    {
        if (!GameDataService) GameDataService = MakeUnique<FUnrealMCPGameDataService>();
        return Command == TEXT("game_data_inspect")
            ? GameDataService->Inspect(Arguments, OutResult, OutError)
            : GameDataService->Edit(Arguments, OutResult, OutError);
    }
    if (!BlueprintInspector)
    {
        BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>(*ExtensionRegistry);
    }
    if (Command == TEXT("blueprint_inspect"))
    {
        return BlueprintInspector->Execute(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("blueprint_action_catalog"))
    {
        if (!BlueprintActionCatalog)
        {
            BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(*BlueprintInspector, BridgeInstanceId);
        }
        return BlueprintActionCatalog->Execute(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("blueprint_graph_edit"))
    {
        if (!BlueprintActionCatalog)
        {
            BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(*BlueprintInspector, BridgeInstanceId);
        }
        if (!BlueprintGraphEditor)
        {
            BlueprintGraphEditor = MakeUnique<FUnrealMCPBlueprintGraphEditor>(*BlueprintInspector, *BlueprintActionCatalog);
        }
        return BlueprintGraphEditor->Execute(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("blueprint_block_replace"))
    {
        if (!BlueprintActionCatalog)
        {
            BlueprintActionCatalog = MakeUnique<FUnrealMCPBlueprintActionCatalog>(
                *BlueprintInspector, BridgeInstanceId);
        }
        if (!BlueprintBlockReplacementService)
        {
            BlueprintBlockReplacementService =
                MakeUnique<FUnrealMCPBlueprintBlockReplacementService>(
                    *BlueprintInspector, *BlueprintActionCatalog);
        }
        return BlueprintBlockReplacementService->Execute(Arguments, OutResult, OutError);
    }
    if (Command == TEXT("widget_tree_edit"))
    {
        if (!WidgetTreeService)
        {
            WidgetTreeService = MakeUnique<FUnrealMCPWidgetTreeService>(*BlueprintInspector);
        }
        return WidgetTreeService->Execute(Arguments, OutResult, OutError);
    }
    if (!BlueprintMutator)
    {
        BlueprintMutator = MakeUnique<FUnrealMCPBlueprintMutator>(*BlueprintInspector);
    }
    return BlueprintMutator->Execute(Command, Arguments, OutResult, OutError);
}

TSharedPtr<FJsonObject> FUnrealMCPBridge::Capabilities() const
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("project_hash"), ProjectHash);
    Result->SetStringField(TEXT("bridge_version"), UnrealMCP::Version);
    Result->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Result->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString(EVersionComponent::Changelist));
    Result->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
    Result->SetStringField(TEXT("mode"), TEXT("blueprint_family_authoring"));
    Result->SetBoolField(TEXT("bridge_ready"), bReady);
    Result->SetArrayField(TEXT("commands"), Strings({TEXT("capabilities"), TEXT("editor_state"), TEXT("editor_shutdown"), TEXT("operation_status"), TEXT("operation_cancel"), TEXT("asset_references"), TEXT("asset_delete"),
        TEXT("level_inspect"), TEXT("level_open"), TEXT("level_manage"), TEXT("level_actor_edit"), TEXT("level_save"),
        TEXT("blueprint_inspect"), TEXT("blueprint_action_catalog"), TEXT("blueprint_graph_edit"),
        TEXT("blueprint_block_replace"), TEXT("blueprint_create"), TEXT("blueprint_compile"),
        TEXT("blueprint_save"), TEXT("blueprint_component_edit"), TEXT("blueprint_default_edit"), TEXT("blueprint_member_edit"),
        TEXT("widget_tree_edit"),
        TEXT("gameplay_framework_edit"), TEXT("game_data_inspect"), TEXT("game_data_edit")}));

    const TSharedRef<FJsonObject> Features = MakeShared<FJsonObject>();
    Features->SetBoolField(TEXT("blueprint_inspection"), true);
    Features->SetBoolField(TEXT("blueprint_mutation"), true);
    Features->SetBoolField(TEXT("blueprint_creation"), true);
    Features->SetBoolField(TEXT("blueprint_compile"), true);
    Features->SetBoolField(TEXT("blueprint_save"), true);
    Features->SetBoolField(TEXT("reliable_mutations"), true);
    Features->SetBoolField(TEXT("blueprint_components"), true);
    Features->SetBoolField(TEXT("blueprint_defaults"), true);
    Features->SetBoolField(TEXT("blueprint_member_variables"), true);
    Features->SetBoolField(TEXT("blueprint_functions"), true);
    Features->SetBoolField(TEXT("blueprint_local_variables"), true);
    Features->SetBoolField(TEXT("blueprint_rep_notify"), true);
    Features->SetBoolField(TEXT("blueprint_macros"), true);
    Features->SetBoolField(TEXT("blueprint_custom_events"), true);
    Features->SetBoolField(TEXT("blueprint_action_catalog"), true);
    Features->SetBoolField(TEXT("blueprint_graph_mutation"), true);
    Features->SetBoolField(TEXT("blueprint_graph_node_lifecycle"), true);
    Features->SetBoolField(TEXT("blueprint_graph_pin_defaults"), true);
    Features->SetBoolField(TEXT("blueprint_graph_direct_connections"), true);
    Features->SetBoolField(TEXT("blueprint_graph_wildcard_specialization"), true);
    Features->SetBoolField(TEXT("blueprint_graph_automatic_conversion"), true);
    Features->SetBoolField(TEXT("blueprint_function_replacement"), true);
    Features->SetBoolField(TEXT("blueprint_function_replacement_scratch_preflight"), true);
    Features->SetBoolField(TEXT("blueprint_family_policy"), true);
    Features->SetBoolField(TEXT("blueprint_library_inspection"), true);
    Features->SetBoolField(TEXT("game_mode_families"), true);
    Features->SetBoolField(TEXT("game_state_families"), true);
    Features->SetBoolField(TEXT("game_instance_family"), true);
    Features->SetBoolField(TEXT("widget_blueprint_family"), true);
    Features->SetBoolField(TEXT("widget_tree_authoring"), true);
    Features->SetBoolField(TEXT("umg_layout_authoring"), true);
    Features->SetBoolField(TEXT("umg_style_authoring"), true);
    Features->SetBoolField(TEXT("umg_property_bindings"), true);
    Features->SetBoolField(TEXT("umg_designer_events"), true);
    Features->SetBoolField(TEXT("multiplayer_blueprint_authoring"), true);
    Features->SetBoolField(TEXT("custom_event_rpcs"), true);
    Features->SetBoolField(TEXT("typed_replication_settings"), true);
    Features->SetBoolField(TEXT("gameplay_framework_assignment"), true);
    Features->SetBoolField(TEXT("user_defined_struct_authoring"), true);
    Features->SetBoolField(TEXT("typed_data_tables"), true);
    Features->SetBoolField(TEXT("game_data_batch_editing"), true);
    Features->SetBoolField(TEXT("data_asset_inspection"), true);
    Features->SetBoolField(TEXT("reflected_inspection_values"), true);
    Features->SetBoolField(TEXT("inherited_component_effective_defaults"), true);
    Features->SetBoolField(TEXT("asset_reference_discovery"), true);
    Features->SetBoolField(TEXT("asset_reference_live_memory"), true);
    Features->SetBoolField(TEXT("asset_delete"), true);
    Features->SetBoolField(TEXT("asset_delete_force"), false);
    Features->SetBoolField(TEXT("asset_delete_undo"), false);
    Features->SetBoolField(TEXT("level_discovery"), true);
    Features->SetBoolField(TEXT("level_open"), true);
    Features->SetBoolField(TEXT("level_snapshots"), true);
    Features->SetBoolField(TEXT("level_management"), true);
    Features->SetBoolField(TEXT("level_blank_creation"), true);
    Features->SetBoolField(TEXT("level_template_creation"), true);
    Features->SetBoolField(TEXT("level_world_settings"), true);
    Features->SetBoolField(TEXT("level_world_partition_conversion"), false);
    Features->SetBoolField(TEXT("level_map_deletion"), true);
    Features->SetBoolField(TEXT("level_actor_inspection"), true);
    Features->SetBoolField(TEXT("level_world_partition_descriptors"), true);
    Features->SetBoolField(TEXT("level_targeted_actor_loading"), true);
    Features->SetBoolField(TEXT("level_instance_properties"), true);
    Features->SetBoolField(TEXT("level_actor_editing"), true);
    Features->SetBoolField(TEXT("level_actor_transactions"), true);
    Features->SetBoolField(TEXT("level_package_save_verification"), true);
    Features->SetBoolField(TEXT("editor_lifecycle"), true);
    Features->SetBoolField(TEXT("graceful_editor_shutdown"), true);
    Features->SetBoolField(TEXT("project_build"), false);
    Features->SetBoolField(TEXT("companion_plugins"), true);
    Features->SetBoolField(TEXT("gas_ability_blueprints_inspection"),
        ExtensionRegistry->HasReadyFamilyCapability(
            TEXT("gameplay_ability"), EUnrealMCPExtensionAccess::Read));
    Features->SetBoolField(TEXT("gas_ability_blueprints_mutation"),
        ExtensionRegistry->HasReadyFamilyCapability(
            TEXT("gameplay_ability"), EUnrealMCPExtensionAccess::Mutation));
    Features->SetBoolField(TEXT("gas_gameplay_effects_inspection"),
        ExtensionRegistry->HasReadyFamilyCapability(
            TEXT("gameplay_effect"), EUnrealMCPExtensionAccess::Read));
    Features->SetBoolField(TEXT("gas_gameplay_effects_mutation"),
        ExtensionRegistry->HasReadyFamilyCapability(
            TEXT("gameplay_effect"), EUnrealMCPExtensionAccess::Mutation));
    Result->SetObjectField(TEXT("features"), Features);
    TArray<TSharedPtr<FJsonValue>> BlueprintFamilies =
        UnrealMCP::BlueprintFamilyPolicy::BuildPublishedMatrix();
    BlueprintFamilies.Append(ExtensionRegistry->BuildBlueprintFamilyCapabilities());
    Result->SetArrayField(TEXT("blueprint_families"), BlueprintFamilies);
    const TSharedPtr<FJsonObject> CompanionCapabilities = ExtensionRegistry->BuildCapabilities();
    Result->SetNumberField(TEXT("companion_api_version"), CompanionCapabilities->GetNumberField(TEXT("companion_api_version")));
    Result->SetNumberField(TEXT("extension_schema_revision"), CompanionCapabilities->GetNumberField(TEXT("extension_schema_revision")));
    Result->SetStringField(TEXT("extension_registry_signature"), CompanionCapabilities->GetStringField(TEXT("registry_signature")));
    Result->SetArrayField(TEXT("companions"), CompanionCapabilities->GetArrayField(TEXT("companions")));
    Result->SetArrayField(TEXT("companion_registration_diagnostics"), CompanionCapabilities->GetArrayField(TEXT("registration_diagnostics")));

    const TSharedRef<FJsonObject> AssetAccess = MakeShared<FJsonObject>();
    AssetAccess->SetStringField(TEXT("read_scope"), TEXT("all_mounted_content"));
    AssetAccess->SetStringField(TEXT("mutation_scope"), TEXT("project_content_and_local_project_plugins"));
    Result->SetObjectField(TEXT("asset_access"), AssetAccess);

    const TSharedRef<FJsonObject> Limits = MakeShared<FJsonObject>();
    Limits->SetNumberField(TEXT("request_bytes"), UnrealMCP::MaxRequestBytes);
    Limits->SetNumberField(TEXT("companion_descriptors"), UnrealMCP::MaxDiscoveredCompanions);
    Limits->SetNumberField(TEXT("companions"), UnrealMCP::MaxAcceptedCompanions);
    Limits->SetNumberField(TEXT("companion_contributions"), UnrealMCP::MaxCompanionContributions);
    Limits->SetNumberField(TEXT("companion_capability_records"), UnrealMCP::MaxCompanionCapabilityRecords);
    Limits->SetNumberField(TEXT("companion_diagnostics"), UnrealMCP::MaxCompanionDiagnostics);
    Limits->SetNumberField(TEXT("extension_id_chars"), UnrealMCP::MaxExtensionIdChars);
    Limits->SetNumberField(TEXT("response_bytes"), UnrealMCP::MaxResponseBytes);
    Limits->SetNumberField(TEXT("queued_requests"), UnrealMCP::MaxQueuedRequests);
    Limits->SetNumberField(TEXT("json_depth"), UnrealMCP::MaxJsonDepth);
    Limits->SetNumberField(TEXT("string_chars"), UnrealMCP::MaxStringLength);
    Limits->SetNumberField(TEXT("command_deadline_ms"), static_cast<int32>(UnrealMCP::CommandDeadlineSeconds * 1000.0));
    Limits->SetNumberField(TEXT("inspect_page_size"), UnrealMCP::MaxInspectPageSize);
    Limits->SetNumberField(TEXT("discovery_scan"), UnrealMCP::MaxDiscoveryScan);
    Limits->SetNumberField(TEXT("inspect_records"), UnrealMCP::MaxInspectRecords);
    Limits->SetNumberField(TEXT("retained_cursors"), UnrealMCP::MaxRetainedCursors);
    Limits->SetNumberField(TEXT("cursor_lifetime_ms"), static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    Limits->SetNumberField(TEXT("compiler_diagnostics"), UnrealMCP::MaxCompilerDiagnostics);
    Limits->SetNumberField(TEXT("diagnostic_chars"), UnrealMCP::MaxDiagnosticChars);
    Limits->SetNumberField(TEXT("retained_operations"), UnrealMCP::MaxRetainedOperations);
    Limits->SetNumberField(TEXT("operation_lifetime_ms"), static_cast<int32>(UnrealMCP::OperationLifetimeSeconds * 1000.0));
    Limits->SetNumberField(TEXT("property_names"), UnrealMCP::MaxPropertyNames);
    Limits->SetNumberField(TEXT("variable_references"), UnrealMCP::MaxVariableReferences);
    Limits->SetNumberField(TEXT("action_results"), UnrealMCP::MaxActionResults);
    Limits->SetNumberField(TEXT("action_scan"), UnrealMCP::MaxActionScan);
    Limits->SetNumberField(TEXT("retained_actions"), UnrealMCP::MaxRetainedActions);
    Limits->SetNumberField(TEXT("retained_catalogs"), UnrealMCP::MaxRetainedCatalogs);
    Limits->SetNumberField(TEXT("action_lifetime_ms"), static_cast<int32>(UnrealMCP::ActionLifetimeSeconds * 1000.0));
    Limits->SetNumberField(TEXT("action_scan_ms"), static_cast<int32>(UnrealMCP::ActionScanSeconds * 1000.0));
    Limits->SetNumberField(TEXT("concurrent_catalogs"), UnrealMCP::MaxConcurrentCatalogs);
    Limits->SetNumberField(TEXT("graph_nodes"), UnrealMCP::MaxGraphNodes);
    Limits->SetNumberField(TEXT("graph_pins_per_node"), UnrealMCP::MaxGraphPinsPerNode);
    Limits->SetNumberField(TEXT("graph_coordinate"), UnrealMCP::MaxGraphCoordinate);
    Limits->SetNumberField(TEXT("graph_links_per_pin"), UnrealMCP::MaxGraphLinksPerPin);
    Limits->SetNumberField(TEXT("graph_automatic_conversion_nodes"), UnrealMCP::MaxAutomaticConversionNodes);
    Limits->SetNumberField(TEXT("pin_default_chars"), UnrealMCP::MaxPinDefaultChars);
    Limits->SetNumberField(TEXT("function_replacement_nodes"), UnrealMCP::MaxFunctionReplacementNodes);
    Limits->SetNumberField(TEXT("function_replacement_owned_nodes"), UnrealMCP::MaxFunctionReplacementOwnedNodes);
    Limits->SetNumberField(TEXT("function_replacement_locals"), UnrealMCP::MaxFunctionReplacementLocals);
    Limits->SetNumberField(TEXT("function_replacement_defaults"), UnrealMCP::MaxFunctionReplacementDefaults);
    Limits->SetNumberField(TEXT("function_replacement_connections"), UnrealMCP::MaxFunctionReplacementConnections);
    Limits->SetNumberField(TEXT("game_data_fields"), UnrealMCP::MaxGameDataFields);
    Limits->SetNumberField(TEXT("game_data_rows"), UnrealMCP::MaxGameDataRows);
    Limits->SetNumberField(TEXT("game_data_batch_rows"), UnrealMCP::MaxGameDataBatchRows);
    Limits->SetNumberField(TEXT("game_data_collection_items"), UnrealMCP::MaxGameDataCollectionItems);
    Limits->SetNumberField(TEXT("game_data_depth"), UnrealMCP::MaxGameDataDepth);
    Limits->SetNumberField(TEXT("game_data_dependencies"), UnrealMCP::MaxGameDataDependencies);
    Limits->SetNumberField(TEXT("asset_reference_registry_candidates"), UnrealMCP::MaxAssetReferenceRegistryCandidates);
    Limits->SetNumberField(TEXT("asset_reference_live_objects"), UnrealMCP::MaxAssetReferenceLiveObjects);
    Limits->SetNumberField(TEXT("asset_reference_records"), UnrealMCP::MaxAssetReferenceRecords);
    Limits->SetNumberField(TEXT("asset_reference_assets_per_package"), UnrealMCP::MaxAssetReferenceAssetsPerPackage);
    Limits->SetNumberField(TEXT("asset_reference_properties"), UnrealMCP::MaxAssetReferenceProperties);
    Limits->SetNumberField(TEXT("asset_reference_retained_cursors"), UnrealMCP::MaxAssetReferenceRetainedCursors);
    Limits->SetNumberField(TEXT("asset_reference_traversal_depth"), 1);
    Limits->SetNumberField(TEXT("level_discovery_scan"), UnrealMCP::MaxLevelDiscoveryScan);
    Limits->SetNumberField(TEXT("level_external_packages"), UnrealMCP::MaxLevelExternalPackages);
    Limits->SetNumberField(TEXT("level_actor_scan"), UnrealMCP::MaxLevelActorScan);
    Limits->SetNumberField(TEXT("level_actor_records"), UnrealMCP::MaxLevelActorRecords);
    Limits->SetNumberField(TEXT("level_components"), UnrealMCP::MaxLevelComponents);
    Limits->SetNumberField(TEXT("level_actor_tags"), UnrealMCP::MaxLevelActorTags);
    Limits->SetNumberField(TEXT("level_data_layers"), UnrealMCP::MaxLevelDataLayers);
    Limits->SetNumberField(TEXT("level_targeted_loads"), UnrealMCP::MaxLevelTargetedLoads);
    Limits->SetNumberField(TEXT("level_setup_properties"), UnrealMCP::MaxLevelSetupProperties);
    Limits->SetNumberField(TEXT("level_owned_packages"), UnrealMCP::MaxLevelOwnedPackages);
    Limits->SetNumberField(TEXT("level_edit_operations"), UnrealMCP::MaxLevelEditOperations);
    Limits->SetNumberField(TEXT("level_edit_actors"), UnrealMCP::MaxLevelEditActors);
    Limits->SetNumberField(TEXT("level_save_packages"), UnrealMCP::MaxLevelSavePackages);
    Limits->SetNumberField(TEXT("dirty_package_summary"), UnrealMCP::MaxDirtyPackageSummary);
    Limits->SetNumberField(TEXT("widget_tree_widgets"), UnrealMCP::MaxWidgetTreeWidgets);
    Limits->SetNumberField(TEXT("widget_tree_depth"), UnrealMCP::MaxWidgetTreeDepth);
    Limits->SetNumberField(TEXT("widget_named_slots"), UnrealMCP::MaxWidgetNamedSlots);
    Limits->SetNumberField(TEXT("widget_defaults_per_widget"), UnrealMCP::MaxWidgetDefaultsPerWidget);
    Limits->SetNumberField(TEXT("widget_changed_defaults"), UnrealMCP::MaxWidgetChangedDefaults);
    Limits->SetNumberField(TEXT("widget_bindings"), UnrealMCP::MaxWidgetBindings);
    Result->SetObjectField(TEXT("limits"), Limits);

    const TSharedRef<FJsonObject> Listener = MakeShared<FJsonObject>();
    Listener->SetStringField(TEXT("host"), TEXT("127.0.0.1"));
    Listener->SetNumberField(TEXT("port"), Port);
    Listener->SetBoolField(TEXT("authenticated"), true);
    Result->SetObjectField(TEXT("listener"), Listener);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBridge::EditorState() const
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("project_hash"), ProjectHash);
    Result->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Result->SetBoolField(TEXT("bridge_ready"), bReady);
    Result->SetStringField(TEXT("state"), IsEngineExitRequested() ? TEXT("shutting_down") : TEXT("ready"));
    Result->SetBoolField(TEXT("is_playing"), GEditor != nullptr && GEditor->IsPlayingSessionInEditor());
    Result->SetBoolField(TEXT("is_simulating"), GEditor != nullptr && GEditor->IsSimulatingInEditor());
    Result->SetBoolField(TEXT("is_saving"), UE::IsSavingPackage());
    Result->SetBoolField(TEXT("is_garbage_collecting"), IsGarbageCollecting());
    Result->SetNumberField(TEXT("queued_requests"), Pending.Load());
    Result->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Result->SetObjectField(TEXT("operation"), OperationLedger ? OperationLedger->CurrentState() : MakeShared<FJsonObject>());
    return Result;
}

bool FUnrealMCPBridge::EditorShutdown(TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (IsEngineExitRequested())
    {
        OutResult = MakeShared<FJsonObject>();
        OutResult->SetBoolField(TEXT("accepted"), true);
        OutResult->SetStringField(TEXT("state"), TEXT("already_shutting_down"));
        OutResult->SetStringField(TEXT("project_hash"), ProjectHash);
        OutResult->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
        return true;
    }

    const bool bPlaying = GEditor != nullptr && GEditor->IsPlayingSessionInEditor();
    const bool bSimulating = GEditor != nullptr && GEditor->IsSimulatingInEditor();
    const bool bSaving = UE::IsSavingPackage();
    const bool bCollecting = IsGarbageCollecting();
    const bool bTransaction = GEditor != nullptr && GEditor->IsTransactionActive();
    const int32 CompilingAssets = FAssetCompilingManager::Get().GetNumRemainingAssets();
    const int32 OtherQueuedRequests = Pending.Load();
    if (bPlaying || bSimulating || bSaving || bCollecting || bTransaction
        || CompilingAssets > 0 || OtherQueuedRequests > 0)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetBoolField(TEXT("is_playing"), bPlaying);
        Details->SetBoolField(TEXT("is_simulating"), bSimulating);
        Details->SetBoolField(TEXT("is_saving"), bSaving);
        Details->SetBoolField(TEXT("is_garbage_collecting"), bCollecting);
        Details->SetBoolField(TEXT("transaction_active"), bTransaction);
        Details->SetNumberField(TEXT("compiling_assets"), CompilingAssets);
        Details->SetNumberField(TEXT("queued_requests"), OtherQueuedRequests);
        OutError = {TEXT("busy"), TEXT("Editor shutdown refused while unsafe editor work is active"), Details, true};
        return false;
    }

    TArray<FString> DirtyPackages;
    int32 DirtyPackageCount = 0;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (Package == nullptr || Package == GetTransientPackage() || !Package->IsDirty()
            || Package->HasAnyPackageFlags(PKG_CompiledIn))
        {
            continue;
        }
        ++DirtyPackageCount;
        if (DirtyPackages.Num() < UnrealMCP::MaxDirtyPackageSummary)
        {
            DirtyPackages.Add(Package->GetName().Left(256));
        }
    }
    DirtyPackages.Sort();
    if (DirtyPackageCount > 0)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("dirty_package_count"), DirtyPackageCount);
        Details->SetBoolField(TEXT("dirty_package_summary_truncated"), DirtyPackageCount > DirtyPackages.Num());
        Details->SetStringField(TEXT("dirty_packages"), FString::Join(DirtyPackages, TEXT(",")).Left(512));
        OutError = {TEXT("busy"), TEXT("Editor shutdown refused because packages have unsaved changes"), Details};
        return false;
    }

    bShutdownAccepted = true;
    OutResult = MakeShared<FJsonObject>();
    OutResult->SetBoolField(TEXT("accepted"), true);
    OutResult->SetStringField(TEXT("state"), TEXT("shutting_down"));
    OutResult->SetNumberField(TEXT("dirty_package_count"), 0);
    OutResult->SetStringField(TEXT("project_hash"), ProjectHash);
    OutResult->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    FTSTicker::GetCoreTicker().AddTicker(
        TEXT("UnrealMCPGracefulShutdown"),
        0.0f,
        [](float)
        {
            FPlatformMisc::RequestExit(false);
            return false;
        });
    return true;
}

bool FUnrealMCPBridge::Heartbeat(float DeltaTime)
{
    if (bStopping || !Discovery)
    {
        return false;
    }
    FString Error;
    if (!Discovery->Write(Error))
    {
        UE_LOG(LogTemp, Error, TEXT("Unreal MCP disabled after heartbeat failure: %s"), *Error);
        bReady = false;
        Discovery->Remove();
        return false;
    }
    return true;
}
