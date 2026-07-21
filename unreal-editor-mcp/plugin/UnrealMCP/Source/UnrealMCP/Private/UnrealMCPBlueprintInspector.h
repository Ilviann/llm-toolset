#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector
{
public:
    explicit FUnrealMCPBlueprintInspector(TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    struct FCursorState
    {
        TSharedPtr<FJsonObject> Arguments;
        FString SnapshotId;
        int32 Offset = 0;
        double ExpiresAt = 0.0;
    };

    bool ExecuteInitial(
        const TSharedPtr<FJsonObject>& Arguments,
        int32 Offset,
        const FString& ExpectedSnapshot,
        int32 PageSizeOverride,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    void RemoveExpiredCursors(double CurrentTime);

    TFunction<double()> Now;
    TMap<FString, FCursorState> Cursors;
};
