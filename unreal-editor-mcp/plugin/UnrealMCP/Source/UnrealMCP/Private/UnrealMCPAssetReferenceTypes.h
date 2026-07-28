#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FUnrealMCPAssetReferenceSnapshot
{
    FString AssetPath;
    FString SnapshotId;
    TSharedPtr<FJsonObject> Target;
    TSharedPtr<FJsonObject> Scans;
    TArray<TSharedPtr<FJsonValue>> Records;
    uint64 RegistrySerial = 0;
};
