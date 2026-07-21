#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "Templates/Atomic.h"

struct FHttpServerRequest;
struct FUnrealMCPError;
class FJsonObject;
class FUnrealMCPDiscovery;
class FUnrealMCPBlueprintInspector;
class FUnrealMCPBlueprintActionCatalog;
class FUnrealMCPBlueprintMutator;
class FUnrealMCPOperationLedger;
class IHttpRouter;

class FUnrealMCPBridge : public TSharedFromThis<FUnrealMCPBridge>
{
public:
    FUnrealMCPBridge(FString InToken, FString InStateDirectory, FString InProjectHash, uint32 InPort);
    ~FUnrealMCPBridge();

    bool Start(FString& OutError);
    void Stop();
    bool IsReady() const { return bReady; }
    int32 PendingRequests() const { return Pending.Load(); }

private:
    bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& Complete);
    void DispatchOnGameThread(FString Command, TSharedPtr<FJsonObject> Arguments, FString OperationId, FString RequestDigest,
        const FHttpResultCallback& Complete, double AcceptedAt);
    bool Execute(const FString& Command, const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError);
    TSharedPtr<FJsonObject> Capabilities() const;
    TSharedPtr<FJsonObject> EditorState() const;
    bool Heartbeat(float DeltaTime);

    FString Token;
    FString StateDirectory;
    FString ProjectHash;
    uint32 Port;
    TSharedPtr<IHttpRouter> Router;
    FHttpRouteHandle Route;
    TUniquePtr<FUnrealMCPDiscovery> Discovery;
    TUniquePtr<FUnrealMCPBlueprintInspector> BlueprintInspector;
    TUniquePtr<FUnrealMCPBlueprintActionCatalog> BlueprintActionCatalog;
    TUniquePtr<FUnrealMCPBlueprintMutator> BlueprintMutator;
    TUniquePtr<FUnrealMCPOperationLedger> OperationLedger;
    FString BridgeInstanceId;
    FTSTicker::FDelegateHandle HeartbeatHandle;
    TAtomic<int32> Pending{0};
    TAtomic<bool> bStopping{false};
    bool bReady = false;
};
