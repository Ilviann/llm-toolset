#include "UnrealMCPLevelService.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "IO/IoHash.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPLevelActorInspector.h"
#include "UnrealMCPVersion.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealMCPLevelServicePrivate
{
FString HashLevelText(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

bool LevelHasOnlyFields(const FUnrealMCPRecord& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed)
    {
        Names.Add(Name);
    }
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key))
        {
            return false;
        }
    }
    return true;
}

bool ReadLevelPageSize(const FUnrealMCPRecord& Object, int32& OutPageSize, FUnrealMCPError& OutError)
{
    OutPageSize = UnrealMCP::DefaultInspectPageSize;
    if (!Object.HasField(TEXT("page_size")))
    {
        return true;
    }
    double Value = 0.0;
    if (!Object.TryGetNumberField(TEXT("page_size"), Value)
        || !FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("page_size must be an integer")};
        return false;
    }
    OutPageSize = static_cast<int32>(Value);
    if (OutPageSize < 1 || OutPageSize > UnrealMCP::MaxInspectPageSize)
    {
        OutError = {TEXT("invalid_argument"), TEXT("page_size is outside the supported range")};
        return false;
    }
    return true;
}

bool IsLevelOpaqueId(const FString& Value)
{
    if (Value.Len() != 32)
    {
        return false;
    }
    for (const TCHAR Character : Value)
    {
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character))
        {
            return false;
        }
    }
    return true;
}

FString LevelObjectPathForAsset(const FAssetData& Asset)
{
    return Asset.GetObjectPathString();
}

TSharedRef<FUnrealMCPRecord> LevelDiscoveryRecord(const FAssetData& Asset)
{
    const FString PackageName = Asset.PackageName.ToString();
    const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
    Record->SetStringField(TEXT("section"), TEXT("map"));
    Record->SetStringField(TEXT("map_path"), LevelObjectPathForAsset(Asset));
    Record->SetStringField(TEXT("package_name"), PackageName);
    Record->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
    Record->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
    Record->SetStringField(TEXT("mount_point"), FPackageName::GetPackageMountPoint(PackageName).ToString());
    Record->SetBoolField(
        TEXT("world_partition"),
        Asset.GetTagValueRef<FString>(FName(TEXT("LevelIsPartitioned"))) == TEXT("1"));
    Record->SetBoolField(
        TEXT("external_actors"),
        Asset.GetTagValueRef<FString>(FName(TEXT("LevelIsUsingExternalActors"))) == TEXT("1"));
    return Record;
}

UWorld* CurrentEditorWorld()
{
    return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
}

}

using namespace UnrealMCPLevelServicePrivate;

FUnrealMCPLevelService::FUnrealMCPLevelService(FString InProjectHash, TFunction<double()> InNow)
    : ProjectHash(MoveTemp(InProjectHash)), Now(MoveTemp(InNow))
{
    check(IsInGameThread());
    SynchronizeCurrentMap();
    FEditorDelegates::MapChange.AddRaw(this, &FUnrealMCPLevelService::OnMapChanged);
    FEditorDelegates::OnMapOpened.AddRaw(this, &FUnrealMCPLevelService::OnMapOpened);
    FEditorDelegates::PostUndoRedo.AddRaw(this, &FUnrealMCPLevelService::OnUndoRedo);
    FCoreUObjectDelegates::OnObjectModified.AddRaw(this, &FUnrealMCPLevelService::OnObjectModified);
    UPackage::PackageDirtyStateChangedEvent.AddRaw(this, &FUnrealMCPLevelService::OnPackageDirtyStateChanged);
}

FUnrealMCPLevelService::~FUnrealMCPLevelService()
{
    FEditorDelegates::MapChange.RemoveAll(this);
    FEditorDelegates::OnMapOpened.RemoveAll(this);
    FEditorDelegates::PostUndoRedo.RemoveAll(this);
    FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
    UPackage::PackageDirtyStateChangedEvent.RemoveAll(this);
}

void FUnrealMCPLevelService::RemoveExpiredCursors(double CurrentTime)
{
    for (auto It = Cursors.CreateIterator(); It; ++It)
    {
        if (It.Value().ExpiresAt <= CurrentTime)
        {
            It.RemoveCurrent();
        }
    }
}

bool FUnrealMCPLevelService::Inspect(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    RemoveExpiredCursors(Now());
    if (!Arguments->HasField(TEXT("cursor")))
    {
        return InspectInitial(Arguments, 0, FString(), INDEX_NONE, OutResult, OutError);
    }
    if (!LevelHasOnlyFields(*Arguments, {TEXT("cursor"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Cursor continuation accepts only cursor and page_size")};
        return false;
    }
    FString Cursor;
    if (!Arguments->TryGetStringField(TEXT("cursor"), Cursor) || !IsLevelOpaqueId(Cursor))
    {
        OutError = {TEXT("invalid_argument"), TEXT("cursor must be a 32-character lowercase hexadecimal opaque value")};
        return false;
    }
    FCursorState* State = Cursors.Find(Cursor);
    if (State == nullptr)
    {
        OutError = {TEXT("cursor_expired"), TEXT("The level cursor is missing or expired"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadLevelPageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    const FCursorState Saved = *State;
    Cursors.Remove(Cursor);
    return InspectInitial(Saved.Arguments, Saved.Offset, Saved.SnapshotId, PageSize, OutResult, OutError);
}

bool FUnrealMCPLevelService::InspectInitial(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    int32 Offset,
    const FString& ExpectedSnapshot,
    int32 PageSizeOverride,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    FString Mode;
    if (!Arguments->TryGetStringField(TEXT("mode"), Mode)
        || (Mode != TEXT("discover") && Mode != TEXT("current")
            && Mode != TEXT("actors") && Mode != TEXT("actor") && Mode != TEXT("component")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("mode must be discover, current, actors, actor, or component")};
        return false;
    }
    if (Mode == TEXT("current"))
    {
        if (!LevelHasOnlyFields(*Arguments, {TEXT("mode")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Current-map inspection accepts only mode")};
            return false;
        }
        TSharedPtr<FUnrealMCPRecord> Record;
        FString Snapshot;
        if (!BuildCurrent(Record, Snapshot, OutError))
        {
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        Result->SetStringField(TEXT("mode"), Mode);
        Result->SetStringField(TEXT("snapshot_id"), Snapshot);
        Result->SetArrayField(TEXT("records"), {MakeShared<FUnrealMCPValueObject>(Record.ToSharedRef())});
        Result->SetNumberField(TEXT("record_count"), 1);
        Result->SetNumberField(TEXT("page_offset"), 0);
        Result->SetBoolField(TEXT("scan_truncated"), false);
        Result->SetBoolField(TEXT("has_more"), false);
        OutResult = Result;
        return true;
    }

    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadLevelPageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    if (PageSizeOverride != INDEX_NONE)
    {
        PageSize = PageSizeOverride;
    }
    TArray<TSharedPtr<FUnrealMCPValue>> Records;
    FString Snapshot;
    bool bScanTruncated = false;
    if (Mode == TEXT("discover"))
    {
        if (!BuildDiscovery(*Arguments, Records, Snapshot, bScanTruncated, OutError))
        {
            return false;
        }
    }
    else
    {
        TSharedPtr<FUnrealMCPRecord> CurrentRecord;
        if (!BuildCurrent(CurrentRecord, Snapshot, OutError))
        {
            return false;
        }
        if (!FUnrealMCPLevelActorInspector::BuildRecords(
                *Arguments,
                CurrentEditorWorld(),
                CurrentRecord->GetStringField(TEXT("map_id")),
                Snapshot,
                Records,
                bScanTruncated,
                OutError))
        {
            return false;
        }
    }
    if (!ExpectedSnapshot.IsEmpty() && Snapshot != ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The level inspection snapshot changed before continuation")};
        return false;
    }
    if (Offset < 0 || Offset > Records.Num())
    {
        OutError = {TEXT("cursor_expired"), TEXT("The level cursor no longer identifies a valid page")};
        return false;
    }
    const int32 End = FMath::Min(Offset + PageSize, Records.Num());
    TArray<TSharedPtr<FUnrealMCPValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index)
    {
        Page.Add(Records[Index]);
    }
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("mode"), Mode);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetArrayField(TEXT("records"), Page);
    Result->SetNumberField(TEXT("record_count"), Records.Num());
    Result->SetNumberField(TEXT("page_offset"), Offset);
    Result->SetBoolField(TEXT("scan_truncated"), bScanTruncated);
    Result->SetBoolField(TEXT("has_more"), End < Records.Num());
    if (End < Records.Num())
    {
        RemoveExpiredCursors(Now());
        if (Cursors.Num() >= UnrealMCP::MaxRetainedCursors)
        {
            FString OldestKey;
            double OldestExpiry = TNumericLimits<double>::Max();
            for (const TPair<FString, FCursorState>& Pair : Cursors)
            {
                if (Pair.Value.ExpiresAt < OldestExpiry)
                {
                    OldestExpiry = Pair.Value.ExpiresAt;
                    OldestKey = Pair.Key;
                }
            }
            Cursors.Remove(OldestKey);
        }
        const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        Cursors.Add(Cursor, FCursorState{Arguments, Snapshot, End, Now() + UnrealMCP::CursorLifetimeSeconds});
        Result->SetStringField(TEXT("next_cursor"), Cursor);
        Result->SetNumberField(
            TEXT("cursor_expires_in_ms"),
            static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    OutResult = Result;
    return true;
}

bool FUnrealMCPLevelService::BuildDiscovery(
    const FUnrealMCPRecord& Arguments,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError) const
{
    if (!LevelHasOnlyFields(Arguments, {TEXT("mode"), TEXT("package_path"), TEXT("asset_name"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Level discovery contains an unknown field")};
        return false;
    }
    FString PackagePath;
    FString AssetName;
    if (Arguments.HasField(TEXT("package_path"))
        && (!Arguments.TryGetStringField(TEXT("package_path"), PackagePath)
            || PackagePath.IsEmpty() || PackagePath.Len() > 512 || PackagePath.Contains(TEXT(".."))
            || !PackagePath.StartsWith(TEXT("/")) || PackagePath.Contains(TEXT("\\"))))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be a bounded mounted package path")};
        return false;
    }
    if (Arguments.HasField(TEXT("asset_name"))
        && (!Arguments.TryGetStringField(TEXT("asset_name"), AssetName)
            || AssetName.IsEmpty() || AssetName.Len() > 128))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_name must be a bounded exact name")};
        return false;
    }

    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    FARFilter Filter;
    Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
    if (!PackagePath.IsEmpty() && PackagePath != TEXT("/"))
    {
        Filter.PackagePaths.Add(FName(*PackagePath));
        Filter.bRecursivePaths = true;
    }
    TArray<FAssetData> Assets;
    int32 Scanned = 0;
    OutScanTruncated = false;
    Registry.EnumerateAssets(
        Filter,
        [&Assets, &AssetName, &Scanned, &OutScanTruncated](const FAssetData& Asset)
        {
            if (Scanned >= UnrealMCP::MaxLevelDiscoveryScan)
            {
                OutScanTruncated = true;
                return false;
            }
            ++Scanned;
            if (AssetName.IsEmpty() || Asset.AssetName.ToString() == AssetName)
            {
                Assets.Add(Asset);
            }
            return true;
        });
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.GetObjectPathString() < Right.GetObjectPathString();
    });
    TArray<FString> Fingerprint;
    Fingerprint.Add(PackagePath);
    Fingerprint.Add(AssetName);
    Fingerprint.Add(OutScanTruncated ? TEXT("truncated") : TEXT("complete"));
    for (const FAssetData& Asset : Assets)
    {
        const TSharedRef<FUnrealMCPRecord> Record = LevelDiscoveryRecord(Asset);
        Fingerprint.Add(
            Record->GetStringField(TEXT("map_path"))
            + TEXT("|") + (Record->GetBoolField(TEXT("world_partition")) ? TEXT("1") : TEXT("0"))
            + TEXT("|") + (Record->GetBoolField(TEXT("external_actors")) ? TEXT("1") : TEXT("0")));
        OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    OutSnapshot = HashLevelText(FString::Join(Fingerprint, TEXT("\n")));
    return true;
}

FString FUnrealMCPLevelService::CurrentMapPackageName(UWorld* World) const
{
    return World != nullptr && World->GetPackage() != nullptr ? World->GetPackage()->GetName() : FString();
}

FString FUnrealMCPLevelService::CurrentMapObjectPath(UWorld* World) const
{
    const FString PackageName = CurrentMapPackageName(World);
    if (PackageName.IsEmpty())
    {
        return FString();
    }
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

FString FUnrealMCPLevelService::MapIdentity(const FString& MapPath) const
{
    return HashLevelText(ProjectHash + TEXT("|") + MapPath);
}

FUnrealMCPLevelService::FDirtyState FUnrealMCPLevelService::ReadDirtyState(UWorld* World) const
{
    FDirtyState State;
    if (World == nullptr || World->GetPackage() == nullptr)
    {
        State.bComplete = false;
        return State;
    }
    const auto AddPackage = [&State](UPackage* Package)
    {
        if (Package != nullptr && Package->IsDirty())
        {
            State.bDirty = true;
            ++State.DirtyPackageCount;
            if (State.DirtyPackages.Num() < UnrealMCP::MaxDirtyPackageSummary)
            {
                State.DirtyPackages.Add(Package->GetName().Left(256));
            }
        }
    };
    AddPackage(World->GetPackage());
    if (World->PersistentLevel != nullptr)
    {
        const TArray<UPackage*> ExternalPackages = World->PersistentLevel->GetLoadedExternalObjectPackages();
        State.LoadedExternalPackages = ExternalPackages.Num();
        if (ExternalPackages.Num() > UnrealMCP::MaxLevelExternalPackages)
        {
            State.bComplete = false;
        }
        const int32 Count = FMath::Min(ExternalPackages.Num(), UnrealMCP::MaxLevelExternalPackages);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            AddPackage(ExternalPackages[Index]);
        }
    }
    State.DirtyPackages.Sort();
    return State;
}

FString FUnrealMCPLevelService::MapRevision(UWorld* World, const FDirtyState& DirtyState) const
{
    FString PersistentState;
    const FString PackageName = CurrentMapPackageName(World);
    if (!PackageName.IsEmpty())
    {
        const TOptional<FAssetPackageData> PackageData =
            FAssetRegistryModule::GetRegistry().GetAssetPackageDataCopy(FName(*PackageName));
        if (PackageData.IsSet() && !PackageData->GetPackageSavedHash().IsZero())
        {
            PersistentState = LexToString(PackageData->GetPackageSavedHash());
        }
    }
    if (PersistentState.IsEmpty() && World != nullptr && World->GetPackage() != nullptr)
    {
        PersistentState = World->GetPackage()->GetPersistentGuid().ToString(EGuidFormats::Digits).ToLower();
    }
    return HashLevelText(
        PackageName + TEXT("|") + PersistentState
        + TEXT("|") + LexToString(MutationSerial)
        + TEXT("|") + (DirtyState.bDirty ? TEXT("dirty") : TEXT("clean")));
}

bool FUnrealMCPLevelService::BuildCurrent(
    TSharedPtr<FUnrealMCPRecord>& OutRecord,
    FString& OutSnapshot,
    FUnrealMCPError& OutError)
{
    SynchronizeCurrentMap();
    UWorld* World = CurrentEditorWorld();
    if (World == nullptr || World->GetPackage() == nullptr)
    {
        OutError = {TEXT("editor_unavailable"), TEXT("No editor world is available"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    const FString PackageName = CurrentMapPackageName(World);
    const FString MapPath = CurrentMapObjectPath(World);
    const FDirtyState DirtyState = ReadDirtyState(World);
    const FString Revision = MapRevision(World, DirtyState);
    const bool bMounted = FPackageName::IsValidLongPackageName(PackageName)
        && FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(MapPath)).IsValid();
    const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
    Record->SetStringField(TEXT("section"), TEXT("current_map"));
    Record->SetStringField(TEXT("map_id"), MapIdentity(MapPath));
    Record->SetStringField(TEXT("map_path"), MapPath);
    Record->SetStringField(TEXT("package_name"), PackageName);
    Record->SetStringField(TEXT("asset_name"), FPackageName::GetLongPackageAssetName(PackageName));
    Record->SetStringField(TEXT("mount_point"), FPackageName::GetPackageMountPoint(PackageName).ToString());
    Record->SetStringField(TEXT("map_revision"), Revision);
    Record->SetBoolField(TEXT("mounted"), bMounted);
    Record->SetBoolField(TEXT("dirty"), DirtyState.bDirty);
    Record->SetBoolField(TEXT("dirty_state_complete"), DirtyState.bComplete);
    Record->SetNumberField(TEXT("dirty_package_count"), DirtyState.DirtyPackageCount);
    Record->SetNumberField(TEXT("loaded_external_package_count"), DirtyState.LoadedExternalPackages);
    Record->SetBoolField(TEXT("world_partition"), World->GetWorldPartition() != nullptr);
    Record->SetBoolField(
        TEXT("external_actors"),
        World->PersistentLevel != nullptr && World->PersistentLevel->IsUsingExternalActors());
    TArray<TSharedPtr<FUnrealMCPValue>> DirtyPackages;
    for (const FString& DirtyPackage : DirtyState.DirtyPackages)
    {
        DirtyPackages.Add(MakeShared<FUnrealMCPValueString>(DirtyPackage));
    }
    Record->SetArrayField(TEXT("dirty_packages"), DirtyPackages);
    OutSnapshot = HashLevelText(
        TEXT("current|") + Record->GetStringField(TEXT("map_id"))
        + TEXT("|") + Revision
        + TEXT("|") + (DirtyState.bComplete ? TEXT("complete") : TEXT("incomplete")));
    OutRecord = Record;
    return true;
}

bool FUnrealMCPLevelService::Open(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid() || !LevelHasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("map_path")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_open requires only operation_id and map_path")};
        return false;
    }
    FString MapPath;
    if (!Arguments->TryGetStringField(TEXT("map_path"), MapPath)
        || MapPath.Len() < 3 || MapPath.Len() > 512 || !MapPath.StartsWith(TEXT("/"))
        || MapPath.Contains(TEXT("..")) || MapPath.Contains(TEXT("\\")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("map_path must be one exact mounted World object path")};
        return false;
    }
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(MapPath));
    if (!Asset.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested map asset was not found")};
        return false;
    }
    if (Asset.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested asset is not a World")};
        return false;
    }
    if (Asset.GetObjectPathString() != MapPath)
    {
        OutError = {TEXT("invalid_argument"), TEXT("map_path must use the exact mounted object path")};
        return false;
    }

    TSharedPtr<FUnrealMCPRecord> BeforeRecord;
    FString BeforeSnapshot;
    if (!BuildCurrent(BeforeRecord, BeforeSnapshot, OutError))
    {
        return false;
    }
    const bool bAlreadyCurrent = BeforeRecord->GetStringField(TEXT("map_path")) == MapPath;
    if (!bAlreadyCurrent)
    {
        const FDirtyState DirtyState = ReadDirtyState(CurrentEditorWorld());
        const bool bPlaying = GEditor != nullptr && GEditor->IsPlayingSessionInEditor();
        const bool bSimulating = GEditor != nullptr && GEditor->IsSimulatingInEditor();
        const bool bSaving = UE::IsSavingPackage();
        const bool bCollecting = IsGarbageCollecting();
        const bool bTransaction = GEditor != nullptr && GEditor->IsTransactionActive();
        const bool bCompiling = FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
        const bool bAsyncLoading = IsAsyncLoading();
        if (bPlaying || bSimulating || bSaving || bCollecting || bTransaction || bCompiling
            || bAsyncLoading || DirtyState.bDirty || !DirtyState.bComplete)
        {
            const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
            Details->SetBoolField(TEXT("is_playing"), bPlaying);
            Details->SetBoolField(TEXT("is_simulating"), bSimulating);
            Details->SetBoolField(TEXT("is_saving"), bSaving);
            Details->SetBoolField(TEXT("is_garbage_collecting"), bCollecting);
            Details->SetBoolField(TEXT("transaction_active"), bTransaction);
            Details->SetBoolField(TEXT("is_compiling"), bCompiling);
            Details->SetBoolField(TEXT("is_async_loading"), bAsyncLoading);
            Details->SetBoolField(TEXT("dirty"), DirtyState.bDirty);
            Details->SetBoolField(TEXT("dirty_state_complete"), DirtyState.bComplete);
            Details->SetNumberField(TEXT("dirty_package_count"), DirtyState.DirtyPackageCount);
            OutError = {
                TEXT("busy"),
                TEXT("Map opening refused because the current editor state cannot be proven safe"),
                Details,
                true};
            return false;
        }
        FString Filename;
        if (!FPackageName::DoesPackageExist(Asset.PackageName.ToString(), &Filename))
        {
            OutError = {TEXT("not_found"), TEXT("The requested mounted map package is unavailable")};
            return false;
        }
        bOpeningMap = true;
        const bool bLoaded = FEditorFileUtils::LoadMap(Filename, false, false);
        bOpeningMap = false;
        SynchronizeCurrentMap();
        MutationSerial = 0;
        if (!bLoaded)
        {
            OutError = {TEXT("open_failed"), TEXT("Unreal Editor could not open the requested map"), MakeShared<FUnrealMCPRecord>(), true};
            return false;
        }
    }

    TSharedPtr<FUnrealMCPRecord> CurrentRecord;
    FString CurrentSnapshot;
    if (!BuildCurrent(CurrentRecord, CurrentSnapshot, OutError))
    {
        return false;
    }
    if (CurrentRecord->GetStringField(TEXT("map_path")) != MapPath)
    {
        OutError = {TEXT("open_failed"), TEXT("The editor current map does not match the requested map after opening")};
        return false;
    }
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("map_path"), MapPath);
    Result->SetBoolField(TEXT("opened"), !bAlreadyCurrent);
    Result->SetBoolField(TEXT("already_current"), bAlreadyCurrent);
    Result->SetStringField(TEXT("snapshot_id"), CurrentSnapshot);
    Result->SetObjectField(TEXT("current_map"), CurrentRecord.ToSharedRef());
    OutResult = Result;
    return true;
}

bool FUnrealMCPLevelService::IsCurrentWorldObject(const UObject* Object) const
{
    if (Object == nullptr)
    {
        return false;
    }
    UWorld* World = CurrentEditorWorld();
    if (World == nullptr)
    {
        return false;
    }
    if (Object == World)
    {
        return true;
    }
    return Object->GetTypedOuter<UWorld>() == World;
}

bool FUnrealMCPLevelService::IsCurrentWorldPackage(const UPackage* Package) const
{
    UWorld* World = CurrentEditorWorld();
    if (Package == nullptr || World == nullptr)
    {
        return false;
    }
    if (Package == World->GetPackage())
    {
        return true;
    }
    if (World->PersistentLevel != nullptr)
    {
        for (const UPackage* ExternalPackage : World->PersistentLevel->GetLoadedExternalObjectPackages())
        {
            if (ExternalPackage == Package)
            {
                return true;
            }
        }
    }
    return false;
}

void FUnrealMCPLevelService::SynchronizeCurrentMap()
{
    const FString CurrentPackage = CurrentMapPackageName(CurrentEditorWorld());
    if (CurrentPackage != ObservedMapPackage)
    {
        ObservedMapPackage = CurrentPackage;
        MutationSerial = 0;
    }
}

void FUnrealMCPLevelService::BumpRevision()
{
    if (!bOpeningMap)
    {
        ++MutationSerial;
    }
}

void FUnrealMCPLevelService::OnMapChanged(uint32 Flags)
{
    const FString Before = ObservedMapPackage;
    SynchronizeCurrentMap();
    if (Before == ObservedMapPackage && ReadDirtyState(CurrentEditorWorld()).bDirty)
    {
        BumpRevision();
    }
}

void FUnrealMCPLevelService::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
    SynchronizeCurrentMap();
    MutationSerial = 0;
}

void FUnrealMCPLevelService::OnUndoRedo()
{
    BumpRevision();
}

void FUnrealMCPLevelService::OnObjectModified(UObject* Object)
{
    if (IsCurrentWorldObject(Object)
        && Object != nullptr
        && Object->GetOutermost() != nullptr
        && Object->GetOutermost()->IsDirty())
    {
        BumpRevision();
    }
}

void FUnrealMCPLevelService::OnPackageDirtyStateChanged(UPackage* Package)
{
    if (IsCurrentWorldPackage(Package))
    {
        if (ReadDirtyState(CurrentEditorWorld()).bDirty)
        {
            BumpRevision();
        }
        else
        {
            MutationSerial = 0;
        }
    }
}
