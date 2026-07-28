#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FUnrealMCPResolvedAssetReferenceTarget;

class FUnrealMCPAssetReferenceRegistryScanner
{
public:
    void Scan(
        const FUnrealMCPResolvedAssetReferenceTarget& Target,
        uint64 SnapshotRegistrySerial,
        TFunctionRef<uint64()> CurrentRegistrySerial,
        TArray<TSharedPtr<FJsonValue>>& OutRecords,
        TSharedPtr<FJsonObject>& OutScans) const;
};
