#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector
{
public:
    explicit FUnrealMCPBlueprintInspector(TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });
    FUnrealMCPBlueprintInspector(
        const class FUnrealMCPExtensionRegistry& InExtensionRegistry,
        TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    struct FCursorState
    {
        TSharedPtr<FUnrealMCPRecord> Arguments;
        FString SnapshotId;
        int32 Offset = 0;
        double ExpiresAt = 0.0;
    };

    bool ExecuteInitial(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        int32 Offset,
        const FString& ExpectedSnapshot,
        int32 PageSizeOverride,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    void RemoveExpiredCursors(double CurrentTime);

    TFunction<double()> Now;
    const class FUnrealMCPExtensionRegistry* ExtensionRegistry = nullptr;
    TMap<FString, FCursorState> Cursors;
};
