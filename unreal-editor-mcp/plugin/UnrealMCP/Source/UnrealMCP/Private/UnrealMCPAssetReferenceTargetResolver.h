#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

struct FUnrealMCPResolvedAssetReferenceTarget
{
    FString AssetPath;
    FString PackageName;
    FAssetData Asset;
    UObject* LoadedObject = nullptr;
    TSharedPtr<FJsonObject> Metadata;
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
