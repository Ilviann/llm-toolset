#include "UnrealMCPBridge.h"

#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
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
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPGameplayFrameworkEditor.h"
#include "UnrealMCPGameDataService.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPOperationLedger.h"
#include "UnrealMCPVersion.h"
#include "UObject/GarbageCollection.h"
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

bool IsMutationCommand(const FString& Command)
{
    return Command == TEXT("blueprint_create") || Command == TEXT("blueprint_compile") || Command == TEXT("blueprint_save")
        || Command == TEXT("blueprint_component_edit") || Command == TEXT("blueprint_default_edit")
        || Command == TEXT("blueprint_member_edit") || Command == TEXT("blueprint_graph_edit")
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

FUnrealMCPBridge::FUnrealMCPBridge(FString InToken, FString InStateDirectory, FString InProjectHash, uint32 InPort)
    : Token(MoveTemp(InToken)), StateDirectory(MoveTemp(InStateDirectory)), ProjectHash(MoveTemp(InProjectHash)), Port(InPort)
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
    BlueprintMutator.Reset();
    GameplayFrameworkEditor.Reset();
    GameDataService.Reset();
    BlueprintActionCatalog.Reset();
    BlueprintInspector.Reset();
    Token.Reset();
}

bool FUnrealMCPBridge::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& Complete)
{
    if (bStopping || !bReady)
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::ServiceUnavail, TEXT("editor_unavailable"), TEXT("Bridge is shutting down"), true));
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
    if (Command != TEXT("capabilities") && Command != TEXT("editor_state") && Command != TEXT("operation_status")
        && Command != TEXT("blueprint_inspect") && Command != TEXT("blueprint_create") && Command != TEXT("blueprint_compile")
        && Command != TEXT("blueprint_save") && Command != TEXT("blueprint_component_edit") && Command != TEXT("blueprint_default_edit")
        && Command != TEXT("blueprint_member_edit") && Command != TEXT("blueprint_action_catalog")
        && Command != TEXT("blueprint_graph_edit") && Command != TEXT("gameplay_framework_edit")
        && Command != TEXT("game_data_inspect") && Command != TEXT("game_data_edit"))
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, TEXT("invalid_argument"), TEXT("Unknown or unavailable command")));
        return true;
    }
    if (Command == TEXT("operation_status"))
    {
        TSharedPtr<FJsonObject> Status;
        FUnrealMCPError Error;
        if (!OperationLedger || !OperationLedger->Status(Arguments, Status, Error))
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
    if (IsMutationCommand(Command))
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
        if (Pinned->bStopping)
        {
            Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::ServiceUnavail, TEXT("cancelled"), TEXT("Bridge is shutting down")));
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
            Result->SetStringField(TEXT("operation_id"), OperationId);
            Result->SetStringField(TEXT("operation_state"), TEXT("committed"));
            Result->SetStringField(TEXT("bridge_instance_id"), Pinned->BridgeInstanceId);
            Result->SetStringField(TEXT("request_digest"), RequestDigest);
            Pinned->OperationLedger->Commit(OperationId, Result);
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
        BlueprintInspector = MakeUnique<FUnrealMCPBlueprintInspector>();
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
    Result->SetArrayField(TEXT("commands"), Strings({TEXT("capabilities"), TEXT("editor_state"), TEXT("operation_status"),
        TEXT("blueprint_inspect"), TEXT("blueprint_action_catalog"), TEXT("blueprint_graph_edit"), TEXT("blueprint_create"), TEXT("blueprint_compile"),
        TEXT("blueprint_save"), TEXT("blueprint_component_edit"), TEXT("blueprint_default_edit"), TEXT("blueprint_member_edit"),
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
    Features->SetBoolField(TEXT("blueprint_family_policy"), true);
    Features->SetBoolField(TEXT("game_mode_families"), true);
    Features->SetBoolField(TEXT("game_state_families"), true);
    Features->SetBoolField(TEXT("game_instance_family"), true);
    Features->SetBoolField(TEXT("multiplayer_blueprint_authoring"), true);
    Features->SetBoolField(TEXT("custom_event_rpcs"), true);
    Features->SetBoolField(TEXT("typed_replication_settings"), true);
    Features->SetBoolField(TEXT("gameplay_framework_assignment"), true);
    Features->SetBoolField(TEXT("user_defined_struct_authoring"), true);
    Features->SetBoolField(TEXT("typed_data_tables"), true);
    Features->SetBoolField(TEXT("game_data_batch_editing"), true);
    Features->SetBoolField(TEXT("editor_lifecycle"), false);
    Features->SetBoolField(TEXT("project_build"), false);
    Result->SetObjectField(TEXT("features"), Features);
    Result->SetArrayField(TEXT("blueprint_families"), UnrealMCP::BlueprintFamilyPolicy::BuildPublishedMatrix());

    const TSharedRef<FJsonObject> AssetAccess = MakeShared<FJsonObject>();
    AssetAccess->SetStringField(TEXT("read_scope"), TEXT("all_mounted_content"));
    AssetAccess->SetStringField(TEXT("mutation_scope"), TEXT("project_content_and_local_project_plugins"));
    Result->SetObjectField(TEXT("asset_access"), AssetAccess);

    const TSharedRef<FJsonObject> Limits = MakeShared<FJsonObject>();
    Limits->SetNumberField(TEXT("request_bytes"), UnrealMCP::MaxRequestBytes);
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
    Limits->SetNumberField(TEXT("game_data_fields"), UnrealMCP::MaxGameDataFields);
    Limits->SetNumberField(TEXT("game_data_rows"), UnrealMCP::MaxGameDataRows);
    Limits->SetNumberField(TEXT("game_data_batch_rows"), UnrealMCP::MaxGameDataBatchRows);
    Limits->SetNumberField(TEXT("game_data_collection_items"), UnrealMCP::MaxGameDataCollectionItems);
    Limits->SetNumberField(TEXT("game_data_depth"), UnrealMCP::MaxGameDataDepth);
    Limits->SetNumberField(TEXT("game_data_dependencies"), UnrealMCP::MaxGameDataDependencies);
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
