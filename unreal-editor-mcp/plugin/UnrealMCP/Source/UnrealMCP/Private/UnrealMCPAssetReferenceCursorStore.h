#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Atomic.h"
#include "UnrealMCPAssetReferenceTypes.h"
#include "UnrealMCPProtocol.h"

struct FAssetData;

class FUnrealMCPAssetReferenceCursorStore
{
public:
    explicit FUnrealMCPAssetReferenceCursorStore(
        TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });
    ~FUnrealMCPAssetReferenceCursorStore();

    uint64 GetRegistrySerial() const;
    TSharedPtr<FJsonObject> BuildPage(
        const FUnrealMCPAssetReferenceSnapshot& Snapshot,
        int32 Offset,
        int32 PageSize);
    bool Continue(
        const FString& Cursor,
        int32 PageSize,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    struct FCursorState
    {
        FUnrealMCPAssetReferenceSnapshot Snapshot;
        int32 Offset = 0;
        double ExpiresAt = 0.0;
    };

    static bool IsOpaqueId(const FString& Value);
    void RemoveExpired(double CurrentTime);
    void BumpRegistrySerial(const FAssetData&);
    void BumpRegistrySerialRenamed(const FAssetData&, const FString&);
    void BumpRegistrySerialNoArgs();

    TFunction<double()> Now;
    TMap<FString, FCursorState> Cursors;
    TAtomic<uint64> RegistrySerial{0};
};
