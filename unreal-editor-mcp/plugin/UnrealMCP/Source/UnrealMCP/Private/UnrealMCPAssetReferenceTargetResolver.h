#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

struct FUnrealMCPResolvedAssetReferenceTarget
{
    FString AssetPath;
    FString PackageName;
    FAssetData Asset;
    UObject* LoadedObject = nullptr;
    TSharedPtr<FUnrealMCPRecord> Metadata;
};

class FUnrealMCPAssetReferenceTargetResolver
{
public:
    static bool IsExactMountedAssetPath(const FString& Value);
    bool Resolve(
        const FString& AssetPath,
        FUnrealMCPResolvedAssetReferenceTarget& OutTarget,
        FUnrealMCPError& OutError) const;
    static FString PackageMount(const FString& PackageName);
};
