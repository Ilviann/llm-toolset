#pragma once

#include "CoreMinimal.h"

class FUnrealMCPRecord;
class FUnrealMCPAssetFamilyRegistry;
struct FUnrealMCPError;

class UNREALMCPASSETCORE_API FUnrealMCPAssetInspectionService
{
public:
    explicit FUnrealMCPAssetInspectionService(
        TSharedRef<FUnrealMCPAssetFamilyRegistry> InAssetFamilyRegistry)
        : AssetFamilyRegistry(MoveTemp(InAssetFamilyRegistry)) {}

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    TSharedRef<FUnrealMCPAssetFamilyRegistry> AssetFamilyRegistry;
};
