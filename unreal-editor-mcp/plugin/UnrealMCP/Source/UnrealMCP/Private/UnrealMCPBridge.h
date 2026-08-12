#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "Templates/Atomic.h"

struct FHttpServerRequest;
struct FUnrealMCPError;
class FUnrealMCPRecord;
class FUnrealMCPDiscovery;
class FUnrealMCPCommandCatalog;
class FUnrealMCPOperationLedger;
class FUnrealMCPExtensionRegistry;
class IHttpRouter;

class FUnrealMCPBridge : public TSharedFromThis<FUnrealMCPBridge>
{
public:
    FUnrealMCPBridge(
        FString InToken,
        FString InStateDirectory,
        FString InProjectHash,
        uint32 InPort,
        TSharedRef<FUnrealMCPExtensionRegistry> InExtensionRegistry);
    ~FUnrealMCPBridge();

    bool Start(FString& OutError);
    void Stop();
    bool IsReady() const { return bReady; }
    int32 PendingRequests() const { return Pending.Load(); }

private:
    bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& Complete);
    void DispatchOnGameThread(FString Command, TSharedPtr<FUnrealMCPRecord> Arguments, FString OperationId, FString RequestDigest,
        const FHttpResultCallback& Complete, double AcceptedAt);
    bool Execute(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    TSharedPtr<FUnrealMCPRecord> Capabilities() const;
    TSharedPtr<FUnrealMCPRecord> EditorState() const;
    bool EditorShutdown(TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool Heartbeat(float DeltaTime);

    FString Token;
    FString StateDirectory;
    FString ProjectHash;
    uint32 Port;
    TSharedPtr<IHttpRouter> Router;
    FHttpRouteHandle Route;
    TUniquePtr<FUnrealMCPDiscovery> Discovery;
    TUniquePtr<FUnrealMCPOperationLedger> OperationLedger;
    TUniquePtr<FUnrealMCPCommandCatalog> CommandCatalog;
    TSharedRef<FUnrealMCPExtensionRegistry> ExtensionRegistry;
    FString BridgeInstanceId;
    FTSTicker::FDelegateHandle HeartbeatHandle;
    TAtomic<int32> Pending{0};
    TAtomic<bool> bStopping{false};
    TAtomic<bool> bShutdownAccepted{false};
    bool bReady = false;
};
