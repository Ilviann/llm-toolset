#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

struct FUnrealMCPResolvedAssetReferenceTarget;

class FUnrealMCPAssetReferenceRegistryScanner
{
public:
    void Scan(
        const FUnrealMCPResolvedAssetReferenceTarget& Target,
        uint64 SnapshotRegistrySerial,
        TFunctionRef<uint64()> CurrentRegistrySerial,
        TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
        TSharedPtr<FUnrealMCPRecord>& OutScans) const;
};
