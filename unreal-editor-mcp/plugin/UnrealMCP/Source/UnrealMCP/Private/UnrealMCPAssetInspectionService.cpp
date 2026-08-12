#include "UnrealMCPAssetInspectionService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionAdapters.h"
#include "UnrealMCPProtocol.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCP::AssetInspectionServicePrivate
{
constexpr int32 DefaultPageSize = 10;

struct FDecodedInspectionRequest
{
    FString AssetPath;
    TArray<FString> SelectorSegments;
    int32 PageSize = DefaultPageSize;
    int32 PageIndex = 0;
    bool bVerbose = false;
    bool bAllowPartialGraph = false;
    bool bHasPaging = false;
    bool bHasPartialGraphFlag = false;
};

bool HasOnlyFields(const FUnrealMCPRecord& Object, std::initializer_list<const TCHAR*> Allowed)
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

bool IsUnreserved(uint8 Byte)
{
    return (Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z')
        || (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '.' || Byte == '_' || Byte == '~';
}

int32 HexValue(TCHAR Character)
{
    if (Character >= '0' && Character <= '9') return Character - '0';
    if (Character >= 'A' && Character <= 'F') return Character - 'A' + 10;
    return INDEX_NONE;
}

FString EncodeSegment(const FString& Input)
{
    const FTCHARToUTF8 Encoded(*Input);
    FString Result;
    static const TCHAR Digits[] = TEXT("0123456789ABCDEF");
    for (int32 Index = 0; Index < Encoded.Length(); ++Index)
    {
        const uint8 Byte = static_cast<uint8>(Encoded.Get()[Index]);
        if (IsUnreserved(Byte))
        {
            Result.AppendChar(static_cast<TCHAR>(Byte));
        }
        else
        {
            Result.AppendChar('%');
            Result.AppendChar(Digits[(Byte >> 4) & 0xF]);
            Result.AppendChar(Digits[Byte & 0xF]);
        }
    }
    return Result;
}

bool DecodeSegment(const FString& Input, FString& Out)
{
    TArray<uint8> Bytes;
    for (int32 Index = 0; Index < Input.Len(); ++Index)
    {
        const TCHAR Character = Input[Index];
        if (Character == '%')
        {
            if (Index + 2 >= Input.Len()) return false;
            const int32 High = HexValue(Input[Index + 1]);
            const int32 Low = HexValue(Input[Index + 2]);
            if (High == INDEX_NONE || Low == INDEX_NONE) return false;
            Bytes.Add(static_cast<uint8>((High << 4) | Low));
            Index += 2;
        }
        else
        {
            if (Character > 0x7F || !IsUnreserved(static_cast<uint8>(Character))) return false;
            Bytes.Add(static_cast<uint8>(Character));
        }
    }
    if (Bytes.Contains(0)) return false;
    const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    Out = FString(Converted.Length(), Converted.Get());
    if (Out.IsEmpty() || Out.Contains(TEXT("/")) || Out.Contains(TEXT("\\"))) return false;
    const FTCHARToUTF8 RoundTrip(*Out);
    return RoundTrip.Length() == Bytes.Num()
        && FMemory::Memcmp(RoundTrip.Get(), Bytes.GetData(), Bytes.Num()) == 0;
}

bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath)
{
    if (!Input.StartsWith(TEXT("/Game/")) || Input.StartsWith(TEXT("//"))
        || Input.Contains(TEXT("..")) || Input.Contains(TEXT("\\")) || Input.Contains(TEXT(":")))
    {
        return false;
    }
    const FString PackageName = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(PackageName, true)) return false;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString Canonical = PackageName + TEXT(".") + AssetName;
    if (Input.Contains(TEXT(".")) && Input != Canonical) return false;
    if (!FPackageName::IsValidObjectPath(Canonical)) return false;
    OutObjectPath = Canonical;
    return true;
}

bool ReadInteger(const FUnrealMCPRecord& Object, const TCHAR* Name, int32 Default, int32 Minimum, int32 Maximum,
    int32& Out, FUnrealMCPError& OutError)
{
    Out = Default;
    if (!Object.HasField(Name)) return true;
    double Number = 0.0;
    if (!Object.TryGetNumberField(Name, Number) || !FMath::IsFinite(Number)
        || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
        || Number < Minimum || Number > Maximum)
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s is outside the supported integer range"), Name)};
        return false;
    }
    Out = static_cast<int32>(Number);
    return true;
}

bool ReadBoolean(const FUnrealMCPRecord& Object, const TCHAR* Name, bool Default, bool& Out, FUnrealMCPError& OutError)
{
    Out = Default;
    if (!Object.HasField(Name)) return true;
    if (!Object.TryGetBoolField(Name, Out))
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must be boolean"), Name)};
        return false;
    }
    return true;
}

bool DecodeRequest(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    FDecodedInspectionRequest& Out,
    FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid() || !HasOnlyFields(*Arguments,
        {TEXT("asset_path"), TEXT("selector"), TEXT("verbose"), TEXT("page_size"), TEXT("page_index"), TEXT("allow_partial_graph")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_inspect accepts only its six documented fields")};
        return false;
    }
    FString RawPath;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawPath) || RawPath.Len() > 512
        || !NormalizeAssetPath(RawPath, Out.AssetPath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must be one exact /Game package or canonical object path")};
        return false;
    }
    if (Arguments->HasField(TEXT("selector")))
    {
        FString Selector;
        if (!Arguments->TryGetStringField(TEXT("selector"), Selector)
            || Selector.IsEmpty() || Selector.Len() > UnrealMCP::MaxAssetInspectSelectorBytes)
        {
            OutError = {TEXT("invalid_argument"), TEXT("selector must be one bounded canonical selector")};
            return false;
        }
        TArray<FString> EncodedSegments;
        Selector.ParseIntoArray(EncodedSegments, TEXT("/"), false);
        if (EncodedSegments.IsEmpty())
        {
            OutError = {TEXT("invalid_argument"), TEXT("selector must contain non-empty path segments")};
            return false;
        }
        for (const FString& Encoded : EncodedSegments)
        {
            FString Decoded;
            if (!DecodeSegment(Encoded, Decoded) || EncodeSegment(Decoded) != Encoded)
            {
                OutError = {TEXT("invalid_argument"), TEXT("selector segments must use canonical uppercase UTF-8 percent encoding")};
                return false;
            }
            Out.SelectorSegments.Add(MoveTemp(Decoded));
        }
    }
    Out.bHasPaging = Arguments->HasField(TEXT("page_size")) || Arguments->HasField(TEXT("page_index"));
    Out.bHasPartialGraphFlag = Arguments->HasField(TEXT("allow_partial_graph"));
    return ReadInteger(*Arguments, TEXT("page_size"), DefaultPageSize, 1, UnrealMCP::MaxAssetInspectPageSize, Out.PageSize, OutError)
        && ReadInteger(*Arguments, TEXT("page_index"), 0, 0, 1000000, Out.PageIndex, OutError)
        && ReadBoolean(*Arguments, TEXT("verbose"), false, Out.bVerbose, OutError)
        && ReadBoolean(*Arguments, TEXT("allow_partial_graph"), false, Out.bAllowPartialGraph, OutError);
}
}

bool FUnrealMCPAssetInspectionService::Execute(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::AssetInspectionServicePrivate;
    check(IsInGameThread());

    FDecodedInspectionRequest Request;
    if (!DecodeRequest(Arguments, Request, OutError)) return false;

    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(Request.AssetPath));
    if (!AssetData.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested /Game asset was not found")};
        return false;
    }
    UObject* AssetObject = AssetData.GetAsset();
    if (AssetObject == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The requested asset could not be loaded"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }

    FUnrealMCPAssetFamilySelection Family;
    if (!AssetFamilyRegistry->Select(
        AssetObject->GetClass(), EUnrealMCPAssetFamilyCapability::Inspection, Family, OutError))
    {
        return false;
    }
    check(Family.Descriptor != nullptr && Family.Descriptor->InspectionAdapter.IsValid());

    UBlueprint* Blueprint = Cast<UBlueprint>(AssetObject);
    const UPackage* Package = AssetObject->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();
    const TEnumAsByte<EBlueprintStatus> StatusBefore = Blueprint != nullptr
        ? Blueprint->Status : TEnumAsByte<EBlueprintStatus>(BS_Unknown);
    const FString Snapshot = UnrealMCP::AssetInspection::BuildStableSnapshot(AssetObject);

    FUnrealMCPAssetFamilyInspectionContext Context;
    Context.Asset = AssetObject;
    Context.Identity = {Request.AssetPath, Snapshot};
    Context.Selector.Segments = Request.SelectorSegments;
    Context.PageIndex = Request.PageIndex;
    Context.PageSize = Request.PageSize;
    Context.bVerbose = Request.bVerbose;
    Context.bAllowPartialGraph = Request.bAllowPartialGraph;
    Context.bHasPaging = Request.bHasPaging;
    Context.bHasPartialGraphFlag = Request.bHasPartialGraphFlag;

    FUnrealMCPAssetFamilyDocumentBuilder Document(Family.Descriptor->Bounds);
    FUnrealMCPAssetFamilySelectorRouter Selectors(Family.Descriptor->Bounds);
    FUnrealMCPAssetFamilySnapshotBuilder AdapterSnapshot(Family.Descriptor->Bounds);
    if (!Family.Descriptor->InspectionAdapter->Inspect(
        Context, Document, Selectors, AdapterSnapshot, OutError)
        || !UnrealMCP::AssetInspection::EncodeDocument(
            Document, Request.AssetPath, Snapshot, OutResult, OutError))
    {
        return false;
    }

    if ((Package != nullptr && Package->IsDirty() != bDirtyBefore)
        || (Blueprint != nullptr && Blueprint->Status != StatusBefore))
    {
        OutError = {TEXT("internal_error"), TEXT("Asset inspection unexpectedly changed editor state")};
        OutResult.Reset();
        return false;
    }
    if (UnrealMCP::AssetInspection::BuildStableSnapshot(AssetObject) != Snapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The asset changed during inspection"), MakeShared<FUnrealMCPRecord>(), true};
        OutResult.Reset();
        return false;
    }
    return true;
}
