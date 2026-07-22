#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPGameDataService
{
public:
    explicit FUnrealMCPGameDataService(TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    bool Inspect(const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError);
    bool Edit(const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError);

private:
    struct FCursorState
    {
        TSharedPtr<FJsonObject> Arguments;
        FString Snapshot;
        int32 Offset = 0;
        double ExpiresAt = 0.0;
    };

    bool InspectInitial(const TSharedPtr<FJsonObject>& Arguments, int32 Offset, const FString& ExpectedSnapshot,
        int32 PageSizeOverride, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError);
    void RemoveExpired(double CurrentTime);

    TFunction<double()> Now;
    TMap<FString, FCursorState> Cursors;
};
