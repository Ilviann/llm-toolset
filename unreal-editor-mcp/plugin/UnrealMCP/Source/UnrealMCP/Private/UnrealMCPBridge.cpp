#include "UnrealMCPBridge.h"

#include "Async/Async.h"
#include "AssetCompilingManager.h"
#include "UnrealMCPWireTypes.h"
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
#include "UnrealMCPCommandCatalog.h"
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

FString Header(const FHttpServerRequest& Request, const TCHAR* LowercaseName)
{
    const TArray<FString>* Values = Request.Headers.Find(LowercaseName);
    return Values != nullptr && Values->Num() == 1 ? (*Values)[0] : FString();
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
    FUnrealMCPCommandHostHandlers HostHandlers;
    HostHandlers.Capabilities = [this](const auto&, auto& Result, auto&) { Result = Capabilities(); return true; };
    HostHandlers.EditorState = [this](const auto&, auto& Result, auto&) { Result = EditorState(); return true; };
    HostHandlers.EditorShutdown = [this](const auto&, auto& Result, auto& Error) { return EditorShutdown(Result, Error); };
    CommandCatalog = MakeUnique<FUnrealMCPCommandCatalog>(
        ProjectHash, BridgeInstanceId, *OperationLedger, ExtensionRegistry, MoveTemp(HostHandlers));
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
    if (!CommandCatalog || !CommandCatalog->IsValid())
    {
        OutError = CommandCatalog ? CommandCatalog->GetInitializationError() : TEXT("Native command catalog is unavailable");
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
    CommandCatalog.Reset();
    Token.Reset();
}

bool FUnrealMCPBridge::HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& Complete)
{
    if (bStopping || bShutdownAccepted || !bReady)
    {
        const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
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
    FUnrealMCPCommandRequest WireRequest;
    FUnrealMCPError ParseError;
    if (!UnrealMCP::Protocol::ParseCommand(Request.Body, WireRequest, ParseError))
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, ParseError));
        return true;
    }
    FString Command = MoveTemp(WireRequest.Command);
    TSharedPtr<FUnrealMCPRecord> Arguments = MoveTemp(WireRequest.Arguments);
    const FUnrealMCPCommandDescriptor* Descriptor = CommandCatalog->Find(Command);
    if (Descriptor == nullptr)
    {
        Complete(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, TEXT("invalid_argument"), TEXT("Unknown or unavailable command")));
        return true;
    }
    if (Descriptor->Dispatch == EUnrealMCPCommandDispatch::RequestThread)
    {
        TSharedPtr<FUnrealMCPRecord> Status;
        FUnrealMCPError Error;
        const bool bResolved = Descriptor->Handler(Arguments, Status, Error);
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
    if (Descriptor->RetainedOperation == EUnrealMCPRetainedOperationPolicy::Retained)
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
    TSharedPtr<FUnrealMCPRecord> Arguments,
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
            FUnrealMCPError Timeout{TEXT("timeout"), TEXT("Command expired before Game-thread dispatch"), MakeShared<FUnrealMCPRecord>(), true};
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
        TSharedPtr<FUnrealMCPRecord> Result;
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

bool FUnrealMCPBridge::Execute(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    return CommandCatalog->Execute(Command, Arguments, OutResult, OutError);
}

TSharedPtr<FUnrealMCPRecord> FUnrealMCPBridge::Capabilities() const
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("project_hash"), ProjectHash);
    Result->SetStringField(TEXT("bridge_version"), UnrealMCP::Version);
    Result->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Result->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString(EVersionComponent::Changelist));
    Result->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
    Result->SetStringField(TEXT("mode"), TEXT("blueprint_family_authoring"));
    Result->SetBoolField(TEXT("bridge_ready"), bReady);
    Result->SetArrayField(TEXT("commands"), CommandCatalog->BuildCommandNames());

    Result->SetObjectField(TEXT("features"), CommandCatalog->BuildFeatures());
    Result->SetArrayField(TEXT("blueprint_families"), CommandCatalog->BuildBlueprintFamilyCapabilities());
    const TSharedPtr<FUnrealMCPRecord> CompanionCapabilities = ExtensionRegistry->BuildCapabilities();
    Result->SetNumberField(TEXT("companion_api_version"), CompanionCapabilities->GetNumberField(TEXT("companion_api_version")));
    Result->SetNumberField(TEXT("extension_schema_revision"), CompanionCapabilities->GetNumberField(TEXT("extension_schema_revision")));
    Result->SetStringField(TEXT("extension_registry_signature"), CompanionCapabilities->GetStringField(TEXT("registry_signature")));
    Result->SetArrayField(TEXT("companions"), CompanionCapabilities->GetArrayField(TEXT("companions")));
    Result->SetArrayField(TEXT("companion_registration_diagnostics"), CompanionCapabilities->GetArrayField(TEXT("registration_diagnostics")));

    const TSharedRef<FUnrealMCPRecord> AssetAccess = MakeShared<FUnrealMCPRecord>();
    AssetAccess->SetStringField(TEXT("read_scope"), TEXT("all_mounted_content"));
    AssetAccess->SetStringField(TEXT("mutation_scope"), TEXT("project_content_and_local_project_plugins"));
    Result->SetObjectField(TEXT("asset_access"), AssetAccess);

    Result->SetObjectField(TEXT("limits"), CommandCatalog->BuildLimits());

    const TSharedRef<FUnrealMCPRecord> Listener = MakeShared<FUnrealMCPRecord>();
    Listener->SetStringField(TEXT("host"), TEXT("127.0.0.1"));
    Listener->SetNumberField(TEXT("port"), Port);
    Listener->SetBoolField(TEXT("authenticated"), true);
    Result->SetObjectField(TEXT("listener"), Listener);
    return Result;
}

TSharedPtr<FUnrealMCPRecord> FUnrealMCPBridge::EditorState() const
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
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
    Result->SetObjectField(TEXT("operation"), OperationLedger ? OperationLedger->CurrentState() : MakeShared<FUnrealMCPRecord>());
    return Result;
}

bool FUnrealMCPBridge::EditorShutdown(TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (IsEngineExitRequested())
    {
        OutResult = MakeShared<FUnrealMCPRecord>();
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
        const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
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
        const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
        Details->SetNumberField(TEXT("dirty_package_count"), DirtyPackageCount);
        Details->SetBoolField(TEXT("dirty_package_summary_truncated"), DirtyPackageCount > DirtyPackages.Num());
        Details->SetStringField(TEXT("dirty_packages"), FString::Join(DirtyPackages, TEXT(",")).Left(512));
        OutError = {TEXT("busy"), TEXT("Editor shutdown refused because packages have unsaved changes"), Details};
        return false;
    }

    bShutdownAccepted = true;
    OutResult = MakeShared<FUnrealMCPRecord>();
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
