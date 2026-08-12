#include "UnrealMCPAssetReferenceService.h"

#include "UnrealMCPAssetReferenceTargetResolver.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCPAssetReferenceServicePrivate
{
bool HasOnlyFields(
    const FUnrealMCPRecord& Object,
    std::initializer_list<const TCHAR*> Allowed)
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

bool ReadPageSize(
    const FUnrealMCPRecord& Object,
    int32& OutPageSize,
    FUnrealMCPError& OutError)
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
        OutError = {
            TEXT("invalid_argument"),
            TEXT("page_size must be an integer")};
        return false;
    }
    OutPageSize = static_cast<int32>(Value);
    if (OutPageSize < 1
        || OutPageSize > UnrealMCP::MaxInspectPageSize)
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("page_size is outside the supported range")};
        return false;
    }
    return true;
}
}

FUnrealMCPAssetReferenceService::FUnrealMCPAssetReferenceService(
    TFunction<double()> InNow)
    : CursorStore(MoveTemp(InNow))
{
}

bool FUnrealMCPAssetReferenceService::Inspect(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("arguments must be an object")};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!UnrealMCPAssetReferenceServicePrivate::ReadPageSize(
        *Arguments,
        PageSize,
        OutError))
    {
        return false;
    }
    return Arguments->HasField(TEXT("cursor"))
        ? Continue(*Arguments, PageSize, OutResult, OutError)
        : InspectInitial(*Arguments, PageSize, OutResult, OutError);
}

bool FUnrealMCPAssetReferenceService::Capture(
    const FString& AssetPath,
    FUnrealMCPAssetReferenceSnapshot& OutSnapshot,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    const uint64 RegistrySerial = CursorStore.GetRegistrySerial();
    return SnapshotBuilder.Capture(
        AssetPath,
        RegistrySerial,
        [this] { return CursorStore.GetRegistrySerial(); },
        OutSnapshot,
        OutError);
}

bool FUnrealMCPAssetReferenceService::InspectInitial(
    const FUnrealMCPRecord& Arguments,
    int32 PageSize,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!UnrealMCPAssetReferenceServicePrivate::HasOnlyFields(
        Arguments,
        {TEXT("asset_path"), TEXT("page_size")}))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("asset_references contains an unknown field")};
        return false;
    }
    FString AssetPath;
    if (!Arguments.TryGetStringField(TEXT("asset_path"), AssetPath)
        || !FUnrealMCPAssetReferenceTargetResolver::IsExactMountedAssetPath(
            AssetPath))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("asset_path must be one exact mounted asset object path")};
        return false;
    }
    FUnrealMCPAssetReferenceSnapshot Snapshot;
    if (!Capture(AssetPath, Snapshot, OutError))
    {
        return false;
    }
    OutResult = CursorStore.BuildPage(Snapshot, 0, PageSize);
    return true;
}

bool FUnrealMCPAssetReferenceService::Continue(
    const FUnrealMCPRecord& Arguments,
    int32 PageSize,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!UnrealMCPAssetReferenceServicePrivate::HasOnlyFields(
        Arguments,
        {TEXT("cursor"), TEXT("page_size")}))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("Cursor continuation accepts only cursor and page_size")};
        return false;
    }
    FString Cursor;
    if (!Arguments.TryGetStringField(TEXT("cursor"), Cursor))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("cursor must be a 32-character lowercase hexadecimal opaque value")};
        return false;
    }
    return CursorStore.Continue(
        Cursor,
        PageSize,
        OutResult,
        OutError);
}
