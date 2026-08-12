#pragma once

#include "CoreMinimal.h"

class FUnrealMCPRecord;
class FUnrealMCPAssetFamilyRegistry;
struct FUnrealMCPError;

namespace UnrealMCP
{
inline constexpr int32 MaxAssetInspectPageSize = 100;
inline constexpr int32 MaxAssetInspectSelectorBytes = 1024;
inline constexpr int32 MaxAssetInspectCompleteGraphBytes = 64 * 1024;
}

class FUnrealMCPAssetInspectionService
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
