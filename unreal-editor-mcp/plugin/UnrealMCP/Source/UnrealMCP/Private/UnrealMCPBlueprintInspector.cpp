#include "UnrealMCPBlueprintInspector.h"

#include "UnrealMCPBlueprintInspectionBuilder.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPVersion.h"


FUnrealMCPBlueprintInspector::FUnrealMCPBlueprintInspector(TFunction<double()> InNow)
    : Now(MoveTemp(InNow))
{
}

void FUnrealMCPBlueprintInspector::RemoveExpiredCursors(double CurrentTime)
{
    using namespace UnrealMCP::BlueprintInspectionPrivate;
    for (auto It = Cursors.CreateIterator(); It; ++It)
    {
        if (It.Value().ExpiresAt <= CurrentTime)
        {
            It.RemoveCurrent();
        }
    }
}

bool FUnrealMCPBlueprintInspector::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintInspectionPrivate;
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    const double CurrentTime = Now();
    RemoveExpiredCursors(CurrentTime);
    if (!Arguments->HasField(TEXT("cursor")))
    {
        return ExecuteInitial(Arguments, 0, FString(), INDEX_NONE, OutResult, OutError);
    }
    if (!HasOnlyFields(*Arguments, {TEXT("cursor"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Cursor continuation accepts only cursor and page_size")};
        return false;
    }
    FString Cursor;
    if (!Arguments->TryGetStringField(TEXT("cursor"), Cursor) || Cursor.Len() != 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("cursor must be a 32-character opaque value")};
        return false;
    }
    FCursorState* State = Cursors.Find(Cursor);
    if (State == nullptr)
    {
        OutError = {TEXT("cursor_expired"), TEXT("The inspection cursor is missing or expired"), MakeShared<FJsonObject>(), true};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadPageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    const FCursorState Saved = *State;
    Cursors.Remove(Cursor);
    return ExecuteInitial(Saved.Arguments, Saved.Offset, Saved.SnapshotId, PageSize, OutResult, OutError);
}

bool FUnrealMCPBlueprintInspector::ExecuteInitial(
    const TSharedPtr<FJsonObject>& Arguments,
    int32 Offset,
    const FString& ExpectedSnapshot,
    int32 PageSizeOverride,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintInspectionPrivate;
    FString Mode;
    if (!Arguments->TryGetStringField(TEXT("mode"), Mode) || (Mode != TEXT("discover") && Mode != TEXT("inspect")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("mode must be discover or inspect")};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadPageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    if (PageSizeOverride != INDEX_NONE)
    {
        PageSize = PageSizeOverride;
    }
    TArray<TSharedPtr<FJsonValue>> Records;
    FString Snapshot;
    bool bScanTruncated = false;
    const bool bBuilt = Mode == TEXT("discover")
        ? BuildDiscovery(*Arguments, Records, Snapshot, bScanTruncated, OutError)
        : BuildInspection(*Arguments, Records, Snapshot, bScanTruncated, OutError);
    if (!bBuilt)
    {
        return false;
    }
    if (!ExpectedSnapshot.IsEmpty() && Snapshot != ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The structural snapshot changed before the cursor was continued")};
        return false;
    }
    if (Offset < 0 || Offset > Records.Num())
    {
        OutError = {TEXT("cursor_expired"), TEXT("The inspection cursor no longer identifies a valid page")};
        return false;
    }
    const int32 End = FMath::Min(Offset + PageSize, Records.Num());
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index) Page.Add(Records[Index]);
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
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
            double Oldest = TNumericLimits<double>::Max();
            for (const TPair<FString, FCursorState>& Pair : Cursors)
            {
                if (Pair.Value.ExpiresAt < Oldest)
                {
                    Oldest = Pair.Value.ExpiresAt;
                    OldestKey = Pair.Key;
                }
            }
            Cursors.Remove(OldestKey);
        }
        const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        Cursors.Add(Cursor, FCursorState{Arguments, Snapshot, End, Now() + UnrealMCP::CursorLifetimeSeconds});
        Result->SetStringField(TEXT("next_cursor"), Cursor);
        Result->SetNumberField(TEXT("cursor_expires_in_ms"), static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    OutResult = Result;
    return true;
}
