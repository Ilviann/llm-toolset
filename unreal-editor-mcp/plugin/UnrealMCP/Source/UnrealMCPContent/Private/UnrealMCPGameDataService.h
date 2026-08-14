#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPGameDataService
{
public:
    explicit FUnrealMCPGameDataService(TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    bool Inspect(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool Edit(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);

private:
    struct FCursorState
    {
        TSharedPtr<FUnrealMCPRecord> Arguments;
        FString Snapshot;
        int32 Offset = 0;
        double ExpiresAt = 0.0;
    };

    bool InspectInitial(const TSharedPtr<FUnrealMCPRecord>& Arguments, int32 Offset, const FString& ExpectedSnapshot,
        int32 PageSizeOverride, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    void RemoveExpired(double CurrentTime);

    TFunction<double()> Now;
    TMap<FString, FCursorState> Cursors;
};
