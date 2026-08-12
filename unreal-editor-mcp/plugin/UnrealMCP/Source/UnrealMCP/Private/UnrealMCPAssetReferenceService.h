#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPAssetReferenceCursorStore.h"
#include "UnrealMCPAssetReferenceSnapshotBuilder.h"
#include "UnrealMCPAssetReferenceTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPAssetReferenceService
{
public:
    explicit FUnrealMCPAssetReferenceService(
        TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    bool Inspect(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool Capture(
        const FString& AssetPath,
        FUnrealMCPAssetReferenceSnapshot& OutSnapshot,
        FUnrealMCPError& OutError);

private:
    bool InspectInitial(
        const FUnrealMCPRecord& Arguments,
        int32 PageSize,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool Continue(
        const FUnrealMCPRecord& Arguments,
        int32 PageSize,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

    FUnrealMCPAssetReferenceCursorStore CursorStore;
    FUnrealMCPAssetReferenceSnapshotBuilder SnapshotBuilder;
};
