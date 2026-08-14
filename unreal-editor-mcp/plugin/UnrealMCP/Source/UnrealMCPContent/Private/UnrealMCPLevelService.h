#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

struct FAssetData;
class UPackage;
class UObject;
class UWorld;

class FUnrealMCPLevelService
{
public:
    explicit FUnrealMCPLevelService(
        FString InProjectHash,
        TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });
    ~FUnrealMCPLevelService();

    bool Inspect(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool Open(
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

    struct FDirtyState
    {
        bool bDirty = false;
        bool bComplete = true;
        int32 LoadedExternalPackages = 0;
        int32 DirtyPackageCount = 0;
        TArray<FString> DirtyPackages;
    };

    bool InspectInitial(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        int32 Offset,
        const FString& ExpectedSnapshot,
        int32 PageSizeOverride,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool BuildDiscovery(
        const FUnrealMCPRecord& Arguments,
        TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
        FString& OutSnapshot,
        bool& OutScanTruncated,
        FUnrealMCPError& OutError) const;
    bool BuildCurrent(
        TSharedPtr<FUnrealMCPRecord>& OutRecord,
        FString& OutSnapshot,
        FUnrealMCPError& OutError);
    FDirtyState ReadDirtyState(UWorld* World) const;
    FString CurrentMapPackageName(UWorld* World) const;
    FString CurrentMapObjectPath(UWorld* World) const;
    FString MapIdentity(const FString& MapPath) const;
    FString MapRevision(UWorld* World, const FDirtyState& DirtyState) const;
    bool IsCurrentWorldObject(const UObject* Object) const;
    bool IsCurrentWorldPackage(const UPackage* Package) const;
    void SynchronizeCurrentMap();
    void BumpRevision();
    void RemoveExpiredCursors(double CurrentTime);
    void OnMapChanged(uint32 Flags);
    void OnMapOpened(const FString& Filename, bool bAsTemplate);
    void OnUndoRedo();
    void OnObjectModified(UObject* Object);
    void OnPackageDirtyStateChanged(UPackage* Package);

    FString ProjectHash;
    TFunction<double()> Now;
    TMap<FString, FCursorState> Cursors;
    FString ObservedMapPackage;
    uint64 MutationSerial = 0;
    bool bOpeningMap = false;
};
