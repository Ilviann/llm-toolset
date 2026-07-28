#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
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
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool Capture(
        const FString& AssetPath,
        FUnrealMCPAssetReferenceSnapshot& OutSnapshot,
        FUnrealMCPError& OutError);

private:
    bool InspectInitial(
        const FJsonObject& Arguments,
        int32 PageSize,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool Continue(
        const FJsonObject& Arguments,
        int32 PageSize,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

    FUnrealMCPAssetReferenceCursorStore CursorStore;
    FUnrealMCPAssetReferenceSnapshotBuilder SnapshotBuilder;
};
