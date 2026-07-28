#include "UnrealMCPAssetReferenceCursorStore.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UnrealMCPVersion.h"

FUnrealMCPAssetReferenceCursorStore::FUnrealMCPAssetReferenceCursorStore(
    TFunction<double()> InNow)
    : Now(MoveTemp(InNow))
{
    check(IsInGameThread());
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    Registry.OnAssetAdded().AddRaw(
        this,
        &FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerial);
    Registry.OnAssetRemoved().AddRaw(
        this,
        &FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerial);
    Registry.OnAssetRenamed().AddRaw(
        this,
        &FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerialRenamed);
    Registry.OnAssetUpdated().AddRaw(
        this,
        &FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerial);
    Registry.OnFilesLoaded().AddRaw(
        this,
        &FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerialNoArgs);
}

FUnrealMCPAssetReferenceCursorStore::~FUnrealMCPAssetReferenceCursorStore()
{
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    Registry.OnAssetAdded().RemoveAll(this);
    Registry.OnAssetRemoved().RemoveAll(this);
    Registry.OnAssetRenamed().RemoveAll(this);
    Registry.OnAssetUpdated().RemoveAll(this);
    Registry.OnFilesLoaded().RemoveAll(this);
}

uint64 FUnrealMCPAssetReferenceCursorStore::GetRegistrySerial() const
{
    return RegistrySerial.Load();
}

bool FUnrealMCPAssetReferenceCursorStore::IsOpaqueId(const FString& Value)
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

void FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerial(const FAssetData&)
{
    ++RegistrySerial;
}

void FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerialRenamed(
    const FAssetData&,
    const FString&)
{
    ++RegistrySerial;
}

void FUnrealMCPAssetReferenceCursorStore::BumpRegistrySerialNoArgs()
{
    ++RegistrySerial;
}

void FUnrealMCPAssetReferenceCursorStore::RemoveExpired(double CurrentTime)
{
    for (auto It = Cursors.CreateIterator(); It; ++It)
    {
        if (It.Value().ExpiresAt <= CurrentTime)
        {
            It.RemoveCurrent();
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAssetReferenceCursorStore::BuildPage(
    const FUnrealMCPAssetReferenceSnapshot& Snapshot,
    int32 Offset,
    int32 PageSize)
{
    const int32 End =
        FMath::Min(Offset + PageSize, Snapshot.Records.Num());
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index)
    {
        Page.Add(Snapshot.Records[Index]);
    }
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Snapshot.AssetPath);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot.SnapshotId);
    Result->SetObjectField(TEXT("target"), Snapshot.Target.ToSharedRef());
    Result->SetObjectField(TEXT("scans"), Snapshot.Scans.ToSharedRef());
    Result->SetArrayField(TEXT("records"), Page);
    Result->SetNumberField(TEXT("record_count"), Snapshot.Records.Num());
    Result->SetNumberField(TEXT("page_offset"), Offset);
    Result->SetBoolField(TEXT("has_more"), End < Snapshot.Records.Num());
    const TSharedRef<FJsonObject> Limitations = MakeShared<FJsonObject>();
    Limitations->SetBoolField(
        TEXT("includes_runtime_constructed_paths"),
        false);
    Limitations->SetBoolField(
        TEXT("includes_external_code_references"),
        false);
    Limitations->SetBoolField(
        TEXT("includes_weak_live_references"),
        false);
    Limitations->SetStringField(
        TEXT("serialized_target_granularity"),
        TEXT("package"));
    Limitations->SetNumberField(TEXT("live_traversal_depth"), 1);
    Result->SetObjectField(TEXT("limitations"), Limitations);
    if (End < Snapshot.Records.Num())
    {
        RemoveExpired(Now());
        if (Cursors.Num() >= UnrealMCP::MaxAssetReferenceRetainedCursors)
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
        const FString Cursor =
            FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        FCursorState State;
        State.Snapshot = Snapshot;
        State.Offset = End;
        State.ExpiresAt = Now() + UnrealMCP::CursorLifetimeSeconds;
        Cursors.Add(Cursor, MoveTemp(State));
        Result->SetStringField(TEXT("next_cursor"), Cursor);
        Result->SetNumberField(
            TEXT("cursor_expires_in_ms"),
            static_cast<int32>(
                UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    return Result;
}

bool FUnrealMCPAssetReferenceCursorStore::Continue(
    const FString& Cursor,
    int32 PageSize,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    RemoveExpired(Now());
    if (!IsOpaqueId(Cursor))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("cursor must be a 32-character lowercase hexadecimal opaque value")};
        return false;
    }
    FCursorState* Found = Cursors.Find(Cursor);
    if (Found == nullptr)
    {
        OutError = {
            TEXT("cursor_expired"),
            TEXT("The asset-reference cursor is missing or expired"),
            MakeShared<FJsonObject>(),
            true};
        return false;
    }
    const FCursorState Saved = *Found;
    Cursors.Remove(Cursor);
    if (Saved.Snapshot.RegistrySerial != RegistrySerial.Load())
    {
        OutError = {
            TEXT("stale_precondition"),
            TEXT("The Asset Registry changed after the reference snapshot was captured")};
        return false;
    }
    if (Saved.Offset < 0
        || Saved.Offset > Saved.Snapshot.Records.Num())
    {
        OutError = {
            TEXT("cursor_expired"),
            TEXT("The asset-reference cursor no longer identifies a valid page")};
        return false;
    }
    OutResult = BuildPage(Saved.Snapshot, Saved.Offset, PageSize);
    return true;
}
