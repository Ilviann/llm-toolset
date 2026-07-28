#include "UnrealMCPGameDataService.h"

#include "UnrealMCPGameDataInspectionBuilder.h"
#include "UnrealMCPGameDataRequestValidation.h"
#include "UnrealMCPVersion.h"

FUnrealMCPGameDataService::FUnrealMCPGameDataService(TFunction<double()> InNow)
    : Now(MoveTemp(InNow))
{
}

void FUnrealMCPGameDataService::RemoveExpired(double CurrentTime)
{
    for (auto It = Cursors.CreateIterator(); It; ++It)
        if (It.Value().ExpiresAt <= CurrentTime) It.RemoveCurrent();
}

bool FUnrealMCPGameDataService::Inspect(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::GameDataRequestValidation;
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    RemoveExpired(Now());
    if (!Arguments->HasField(TEXT("cursor")))
        return InspectInitial(Arguments, 0, FString(), INDEX_NONE, OutResult, OutError);
    if (!HasOnlyFields(*Arguments, {TEXT("cursor"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Cursor continuation accepts only cursor and page_size")};
        return false;
    }
    FString Cursor;
    if (!Arguments->TryGetStringField(TEXT("cursor"), Cursor) || Cursor.Len() != 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("cursor must be one opaque identity")};
        return false;
    }
    FCursorState* State = Cursors.Find(Cursor);
    if (State == nullptr)
    {
        OutError = {TEXT("cursor_expired"), TEXT("The game-data cursor is missing or expired"), MakeShared<FJsonObject>(), true};
        return false;
    }
    int32 PageSize = 0;
    if (!ReadPageSize(*Arguments, PageSize, OutError)) return false;
    const FCursorState Saved = *State;
    Cursors.Remove(Cursor);
    return InspectInitial(Saved.Arguments, Saved.Offset, Saved.Snapshot, PageSize, OutResult, OutError);
}

bool FUnrealMCPGameDataService::InspectInitial(
    const TSharedPtr<FJsonObject>& Arguments,
    int32 Offset,
    const FString& ExpectedSnapshot,
    int32 PageSizeOverride,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::GameDataRequestValidation;
    int32 PageSize = 0;
    if (!ReadPageSize(*Arguments, PageSize, OutError)) return false;
    if (PageSizeOverride != INDEX_NONE) PageSize = PageSizeOverride;
    FString Target;
    FString ObjectPath;
    FString Package;
    FString Snapshot;
    TArray<TSharedPtr<FJsonValue>> Records;
    TArray<TSharedPtr<FJsonValue>> Schema;
    TSharedPtr<FJsonObject> Metadata;
    if (!UnrealMCP::GameDataInspectionBuilder::Build(
        *Arguments, Target, ObjectPath, Package, Records, Schema, Snapshot, Metadata, OutError))
    {
        return false;
    }
    if (!ExpectedSnapshot.IsEmpty() && ExpectedSnapshot != Snapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The game-data snapshot changed before cursor continuation")};
        return false;
    }
    if (Offset < 0 || Offset > Records.Num())
    {
        OutError = {TEXT("cursor_expired"), TEXT("The cursor no longer identifies a valid page")};
        return false;
    }
    const int32 End = FMath::Min(Offset + PageSize, Records.Num());
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index) Page.Add(Records[Index]);
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("target"), Target);
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetObjectField(TEXT("metadata"), Metadata);
    Result->SetArrayField(TEXT("schema"), Schema);
    Result->SetArrayField(TEXT("records"), Page);
    Result->SetNumberField(TEXT("record_count"), Records.Num());
    Result->SetNumberField(TEXT("page_offset"), Offset);
    Result->SetBoolField(TEXT("has_more"), End < Records.Num());
    if (End < Records.Num())
    {
        RemoveExpired(Now());
        if (Cursors.Num() >= UnrealMCP::MaxRetainedCursors)
        {
            FString Oldest;
            double Expiry = TNumericLimits<double>::Max();
            for (const TPair<FString, FCursorState>& Pair : Cursors)
            {
                if (Pair.Value.ExpiresAt < Expiry)
                {
                    Expiry = Pair.Value.ExpiresAt;
                    Oldest = Pair.Key;
                }
            }
            Cursors.Remove(Oldest);
        }
        const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        Cursors.Add(Cursor, FCursorState{Arguments, Snapshot, End, Now() + UnrealMCP::CursorLifetimeSeconds});
        Result->SetStringField(TEXT("next_cursor"), Cursor);
        Result->SetNumberField(TEXT("cursor_expires_in_ms"), static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    OutResult = Result;
    return true;
}
