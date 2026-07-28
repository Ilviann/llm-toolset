#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPAssetReferenceLiveScanner.h"
#include "UnrealMCPAssetReferenceRegistryScanner.h"
#include "UnrealMCPAssetReferenceTargetResolver.h"
#include "UnrealMCPAssetReferenceTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPAssetReferenceSnapshotBuilder
{
public:
    bool Capture(
        const FString& AssetPath,
        uint64 SnapshotRegistrySerial,
        TFunctionRef<uint64()> CurrentRegistrySerial,
        FUnrealMCPAssetReferenceSnapshot& OutSnapshot,
        FUnrealMCPError& OutError) const;

private:
    FUnrealMCPAssetReferenceTargetResolver TargetResolver;
    FUnrealMCPAssetReferenceRegistryScanner RegistryScanner;
    FUnrealMCPAssetReferenceLiveScanner LiveScanner;
};
