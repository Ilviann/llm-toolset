#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

struct FUnrealMCPAssetReferenceSnapshot
{
    FString AssetPath;
    FString SnapshotId;
    TSharedPtr<FUnrealMCPRecord> Target;
    TSharedPtr<FUnrealMCPRecord> Scans;
    TArray<TSharedPtr<FUnrealMCPValue>> Records;
    uint64 RegistrySerial = 0;
};
