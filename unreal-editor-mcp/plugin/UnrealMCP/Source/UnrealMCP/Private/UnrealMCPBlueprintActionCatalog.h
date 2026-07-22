#pragma once

#include "CoreMinimal.h"
#include "BlueprintNodeSpawner.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UBlueprint;
class UEdGraph;

class FUnrealMCPBlueprintActionCatalog
{
public:
    FUnrealMCPBlueprintActionCatalog(
        FUnrealMCPBlueprintInspector& InInspector,
        FString InBridgeInstanceId,
        TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); },
        TFunction<double()> InScanNow = [] { return FPlatformTime::Seconds(); });

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

    struct FResolvedAction
    {
        const UBlueprintNodeSpawner* Spawner = nullptr;
        IBlueprintNodeBinder::FBindingSet Bindings;
    };

    bool ResolveForInvocation(
        const FString& ActionId,
        UBlueprint* Blueprint,
        UEdGraph* Graph,
        const FString& AssetPath,
        const FString& GraphId,
        const FString& SnapshotId,
        FResolvedAction& OutAction,
        FUnrealMCPError& OutError);

private:
    struct FRetainedAction
    {
        TSharedPtr<FJsonObject> PublicRecord;
        FString QueryKey;
        FString RebuildSignature;
        FString TargetClass;
        FString GraphSchema;
        FString AssetPath;
        FString GraphId;
        FString SnapshotId;
        FString PinNodeId;
        FString PinId;
        double ExpiresAt = 0.0;
    };

    struct FCachedCatalog
    {
        TArray<FString> ActionIds;
        FString AssetPath;
        FString BlueprintFamily;
        FString GraphId;
        FString SnapshotId;
        int32 ScannedCount = 0;
        bool bTruncated = false;
        bool bTimedOut = false;
        double ExpiresAt = 0.0;
    };

    void RemoveExpired(double CurrentTime);
    void EvictFor(int32 IncomingCount);
    bool BuildCachedResult(const FCachedCatalog& Cache, TSharedPtr<FJsonObject>& OutResult) const;

    FUnrealMCPBlueprintInspector& Inspector;
    FString BridgeInstanceId;
    TFunction<double()> Now;
    TFunction<double()> ScanNow;
    TMap<FString, FRetainedAction> RetainedActions;
    TMap<FString, FCachedCatalog> Catalogs;
};
