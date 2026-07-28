#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Atomic.h"
#include "UnrealMCPProtocol.h"

struct FAssetData;

class FUnrealMCPAssetReferenceService
{
public:
    explicit FUnrealMCPAssetReferenceService(
        TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });
    ~FUnrealMCPAssetReferenceService();

    bool Inspect(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    struct FCursorState
    {
        FString AssetPath;
        FString SnapshotId;
        TArray<TSharedPtr<FJsonValue>> Records;
        TSharedPtr<FJsonObject> Target;
        TSharedPtr<FJsonObject> Scans;
        int32 Offset = 0;
        uint64 RegistrySerial = 0;
        double ExpiresAt = 0.0;
    };

    bool InspectInitial(
        const FJsonObject& Arguments,
        int32 PageSize,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool Continue(
        const FJsonObject& Arguments,
        int32 PageSize,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool BuildSnapshot(
        const FString& AssetPath,
        uint64 SnapshotRegistrySerial,
        TSharedPtr<FJsonObject>& OutTarget,
        TSharedPtr<FJsonObject>& OutScans,
        TArray<TSharedPtr<FJsonValue>>& OutRecords,
        FString& OutSnapshot,
        FUnrealMCPError& OutError);
    TSharedPtr<FJsonObject> BuildPage(
        const FString& AssetPath,
        const FString& Snapshot,
        const TSharedPtr<FJsonObject>& Target,
        const TSharedPtr<FJsonObject>& Scans,
        const TArray<TSharedPtr<FJsonValue>>& Records,
        int32 Offset,
        int32 PageSize,
        uint64 SnapshotRegistrySerial);
    void RemoveExpiredCursors(double CurrentTime);
    void BumpRegistrySerial(const FAssetData&);
    void BumpRegistrySerialRenamed(const FAssetData&, const FString&);
    void BumpRegistrySerialNoArgs();

    TFunction<double()> Now;
    TMap<FString, FCursorState> Cursors;
    TAtomic<uint64> RegistrySerial{0};
};
