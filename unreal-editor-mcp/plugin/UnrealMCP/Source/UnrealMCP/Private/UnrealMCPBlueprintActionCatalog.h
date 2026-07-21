#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;

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
        double ExpiresAt = 0.0;
    };

    struct FCachedCatalog
    {
        TArray<FString> ActionIds;
        FString AssetPath;
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
