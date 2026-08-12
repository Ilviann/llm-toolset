#pragma once

#include "CoreMinimal.h"

class FUnrealMCPRecord;
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
    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
};
