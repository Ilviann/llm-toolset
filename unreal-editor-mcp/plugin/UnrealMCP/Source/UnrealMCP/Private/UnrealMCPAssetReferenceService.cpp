#include "UnrealMCPAssetReferenceService.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/AssetManager.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace UnrealMCPAssetReferencesPrivate
{
using UE::AssetRegistry::EDependencyCategory;
using UE::AssetRegistry::EDependencyProperty;

struct FScanCounts
{
    bool bSupported = true;
    bool bTruncated = false;
    bool bStale = false;
    int32 Candidates = 0;
    int32 Scanned = 0;
    int32 Records = 0;
};

class FDirectTargetReferenceFinder final : public FReferenceFinder
{
public:
    FDirectTargetReferenceFinder(TArray<UObject*>& InScratch, UObject* InTarget)
        : FReferenceFinder(InScratch, nullptr, false, false, false, false)
        , Target(InTarget)
    {
    }

    void Reset()
    {
        ReferenceCount = 0;
        Properties.Reset();
    }

    int32 GetReferenceCount() const
    {
        return ReferenceCount;
    }

    const TArray<FProperty*>& GetProperties() const
    {
        return Properties;
    }

    virtual void HandleObjectReference(
        UObject*& Object,
        const UObject* ReferencingObject,
        const FProperty* ReferencingProperty) override
    {
        if (Object != Target)
        {
            return;
        }
        ++ReferenceCount;
        if (ReferencingProperty != nullptr)
        {
            Properties.AddUnique(const_cast<FProperty*>(ReferencingProperty));
        }
    }

private:
    UObject* Target = nullptr;
    int32 ReferenceCount = 0;
    TArray<FProperty*> Properties;
};

FString HashReferenceText(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

bool AssetReferenceHasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed)
    {
        Names.Add(Name);
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key))
        {
            return false;
        }
    }
    return true;
}

bool ReadAssetReferencePageSize(const FJsonObject& Object, int32& OutPageSize, FUnrealMCPError& OutError)
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

bool IsOpaqueId(const FString& Value)
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

bool IsExactMountedAssetPath(const FString& Value)
{
    if (Value.Len() < 3 || Value.Len() > 512 || !Value.StartsWith(TEXT("/"))
        || Value.Contains(TEXT("..")) || Value.Contains(TEXT("\\")) || Value.Contains(TEXT(":")))
    {
        return false;
    }
    const int32 Slash = Value.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 Dot = Value.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    return Dot > Slash + 1 && Dot < Value.Len() - 1;
}

FString CategoryName(EDependencyCategory Category)
{
    if (Category == EDependencyCategory::Package) return TEXT("package");
    if (Category == EDependencyCategory::Manage) return TEXT("manage");
    if (Category == EDependencyCategory::SearchableName) return TEXT("searchable_name");
    return TEXT("unknown");
}

FString EvidenceName(EDependencyCategory Category)
{
    if (Category == EDependencyCategory::Package) return TEXT("serialized");
    if (Category == EDependencyCategory::Manage) return TEXT("management");
    if (Category == EDependencyCategory::SearchableName) return TEXT("searchable_name");
    return TEXT("unsupported");
}

TArray<TSharedPtr<FJsonValue>> PropertyNames(EDependencyProperty Properties)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    const auto Add = [&Result, Properties](EDependencyProperty Property, const TCHAR* Name)
    {
        if (EnumHasAnyFlags(Properties, Property))
        {
            Result.Add(MakeShared<FJsonValueString>(Name));
        }
    };
    Add(EDependencyProperty::Hard, TEXT("hard"));
    Add(EDependencyProperty::Game, TEXT("game"));
    Add(EDependencyProperty::Build, TEXT("build"));
    Add(EDependencyProperty::Direct, TEXT("direct"));
    Add(EDependencyProperty::CookRule, TEXT("cook_rule"));
    return Result;
}

TSharedRef<FJsonObject> ScanStatus(const FScanCounts& Counts)
{
    const bool bComplete = Counts.bSupported && !Counts.bTruncated && !Counts.bStale;
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(
        TEXT("status"),
        !Counts.bSupported ? TEXT("unsupported")
        : Counts.bTruncated ? TEXT("truncated")
        : Counts.bStale ? TEXT("stale")
        : TEXT("complete"));
    Result->SetBoolField(TEXT("complete"), bComplete);
    Result->SetBoolField(TEXT("truncated"), Counts.bTruncated);
    Result->SetBoolField(TEXT("unsupported"), !Counts.bSupported);
    Result->SetBoolField(TEXT("stale"), Counts.bStale);
    Result->SetNumberField(TEXT("candidate_count"), Counts.Candidates);
    Result->SetNumberField(TEXT("scanned_count"), Counts.Scanned);
    Result->SetNumberField(TEXT("record_count"), Counts.Records);
    return Result;
}

FString SafeObjectPath(const UObject* Object)
{
    return Object != nullptr ? Object->GetPathName().Left(512) : FString();
}

FString SafeClassPath(const UObject* Object)
{
    return Object != nullptr && Object->GetClass() != nullptr
        ? Object->GetClass()->GetClassPathName().ToString().Left(512)
        : FString();
}

FString PackageMount(const FString& PackageName)
{
    return PackageName.IsEmpty()
        ? FString()
        : FPackageName::GetPackageMountPoint(PackageName).ToString().Left(128);
}

TSharedRef<FJsonObject> RegistryRecord(
    const FAssetDependency& Dependency,
    const FString& TargetIdentifier,
    const FAssetData* ReferencerAsset)
{
    const FString PackageName = Dependency.AssetId.PackageName.ToString().Left(512);
    const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("section"), TEXT("registry"));
    Record->SetStringField(TEXT("evidence"), EvidenceName(Dependency.Category));
    Record->SetStringField(TEXT("dependency_category"), CategoryName(Dependency.Category));
    Record->SetStringField(TEXT("target_identifier"), TargetIdentifier.Left(512));
    Record->SetStringField(
        TEXT("target_granularity"),
        Dependency.Category == EDependencyCategory::Package ? TEXT("package") : TEXT("object_or_primary_asset"));
    Record->SetStringField(TEXT("referencer_identifier"), Dependency.AssetId.ToString().Left(512));
    Record->SetStringField(TEXT("referencer_package"), PackageName);
    Record->SetStringField(TEXT("mount_point"), PackageMount(PackageName));
    Record->SetNumberField(TEXT("dependency_property_bits"), static_cast<uint8>(Dependency.Properties));
    Record->SetArrayField(TEXT("dependency_properties"), PropertyNames(Dependency.Properties));
    if (Dependency.Category == EDependencyCategory::Package)
    {
        Record->SetBoolField(TEXT("hard"), EnumHasAnyFlags(Dependency.Properties, EDependencyProperty::Hard));
    }
    if (Dependency.Category == EDependencyCategory::Manage)
    {
        Record->SetBoolField(TEXT("direct"), EnumHasAnyFlags(Dependency.Properties, EDependencyProperty::Direct));
    }
    const FPrimaryAssetId PrimaryId = Dependency.AssetId.GetPrimaryAssetId();
    Record->SetStringField(TEXT("referencer_primary_asset_id"), PrimaryId.IsValid() ? PrimaryId.ToString().Left(256) : FString());
    if (ReferencerAsset != nullptr && ReferencerAsset->IsValid())
    {
        Record->SetStringField(TEXT("referencer_asset_path"), ReferencerAsset->GetObjectPathString().Left(512));
        Record->SetStringField(TEXT("referencer_asset_class"), ReferencerAsset->AssetClassPath.ToString().Left(512));
        Record->SetBoolField(TEXT("asset_class_known"), true);
    }
    else
    {
        Record->SetStringField(TEXT("referencer_asset_path"), FString());
        Record->SetStringField(TEXT("referencer_asset_class"), FString());
        Record->SetBoolField(TEXT("asset_class_known"), false);
    }
    return Record;
}

void ResolveReferencerAssets(
    IAssetRegistry& Registry,
    const FAssetIdentifier& Identifier,
    TArray<FAssetData>& OutAssets,
    bool& OutTruncated)
{
    const FPrimaryAssetId PrimaryId = Identifier.GetPrimaryAssetId();
    if (PrimaryId.IsValid())
    {
        const FSoftObjectPath PrimaryPath = UAssetManager::Get().GetPrimaryAssetPath(PrimaryId);
        const FAssetData Asset = Registry.GetAssetByObjectPath(PrimaryPath);
        if (Asset.IsValid())
        {
            OutAssets.Add(Asset);
        }
        return;
    }
    if (!Identifier.PackageName.IsNone() && !Identifier.ObjectName.IsNone())
    {
        const FSoftObjectPath ObjectPath(
            Identifier.PackageName.ToString() + TEXT(".") + Identifier.ObjectName.ToString());
        const FAssetData Asset = Registry.GetAssetByObjectPath(ObjectPath);
        if (Asset.IsValid())
        {
            OutAssets.Add(Asset);
        }
        return;
    }
    if (!Identifier.PackageName.IsNone())
    {
        Registry.GetAssetsByPackageName(Identifier.PackageName, OutAssets, true);
        OutAssets.Sort([](const FAssetData& Left, const FAssetData& Right)
        {
            return Left.GetObjectPathString() < Right.GetObjectPathString();
        });
        if (OutAssets.Num() > UnrealMCP::MaxAssetReferenceAssetsPerPackage)
        {
            OutAssets.SetNum(UnrealMCP::MaxAssetReferenceAssetsPerPackage, EAllowShrinking::No);
            OutTruncated = true;
        }
    }
}

void AppendRegistryCategory(
    IAssetRegistry& Registry,
    const TArray<FAssetIdentifier>& Targets,
    EDependencyCategory Category,
    bool bRegistryStale,
    TArray<TSharedPtr<FJsonValue>>& Records,
    FScanCounts& OutCounts)
{
    OutCounts.bStale = bRegistryStale;
    TArray<TPair<FString, FAssetDependency>> Dependencies;
    bool bAnySupportedTarget = false;
    TSet<FString> Seen;
    for (const FAssetIdentifier& Target : Targets)
    {
        TArray<FAssetDependency> Found;
        if (Registry.GetReferencers(Target, Found, Category))
        {
            bAnySupportedTarget = true;
        }
        OutCounts.Candidates += Found.Num();
        for (const FAssetDependency& Dependency : Found)
        {
            const FString Key = Target.ToString() + TEXT("|") + Dependency.AssetId.ToString()
                + TEXT("|") + LexToString(static_cast<uint8>(Dependency.Properties));
            if (Seen.Contains(Key))
            {
                continue;
            }
            Seen.Add(Key);
            if (Dependencies.Num() >= UnrealMCP::MaxAssetReferenceRegistryCandidates)
            {
                OutCounts.bTruncated = true;
                continue;
            }
            Dependencies.Emplace(Target.ToString(), Dependency);
        }
    }
    OutCounts.bSupported = bAnySupportedTarget;
    Dependencies.Sort([](const TPair<FString, FAssetDependency>& Left, const TPair<FString, FAssetDependency>& Right)
    {
        const FString LeftKey = Left.Key + TEXT("|") + Left.Value.AssetId.ToString()
            + TEXT("|") + LexToString(static_cast<uint8>(Left.Value.Properties));
        const FString RightKey = Right.Key + TEXT("|") + Right.Value.AssetId.ToString()
            + TEXT("|") + LexToString(static_cast<uint8>(Right.Value.Properties));
        return LeftKey < RightKey;
    });
    for (const TPair<FString, FAssetDependency>& Pair : Dependencies)
    {
        ++OutCounts.Scanned;
        TArray<FAssetData> Assets;
        ResolveReferencerAssets(Registry, Pair.Value.AssetId, Assets, OutCounts.bTruncated);
        if (Assets.IsEmpty())
        {
            if (Records.Num() >= UnrealMCP::MaxAssetReferenceRecords)
            {
                OutCounts.bTruncated = true;
                continue;
            }
            Records.Add(MakeShared<FJsonValueObject>(RegistryRecord(Pair.Value, Pair.Key, nullptr)));
            ++OutCounts.Records;
            continue;
        }
        for (const FAssetData& Asset : Assets)
        {
            if (Records.Num() >= UnrealMCP::MaxAssetReferenceRecords)
            {
                OutCounts.bTruncated = true;
                break;
            }
            Records.Add(MakeShared<FJsonValueObject>(RegistryRecord(Pair.Value, Pair.Key, &Asset)));
            ++OutCounts.Records;
        }
    }
}

void AppendLiveReferences(
    UObject* TargetObject,
    TArray<TSharedPtr<FJsonValue>>& Records,
    FScanCounts& OutCounts,
    int32& OutOpenEditorCount)
{
    if (TargetObject == nullptr)
    {
        OutCounts.bSupported = false;
        return;
    }

    UAssetEditorSubsystem* AssetEditors =
        GEditor != nullptr ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (AssetEditors != nullptr)
    {
        const TArray<IAssetEditorInstance*> Editors = AssetEditors->FindEditorsForAsset(TargetObject);
        OutOpenEditorCount = Editors.Num();
        for (IAssetEditorInstance* Editor : Editors)
        {
            ++OutCounts.Candidates;
            if (Records.Num() >= UnrealMCP::MaxAssetReferenceRecords)
            {
                OutCounts.bTruncated = true;
                break;
            }
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("section"), TEXT("live_memory"));
            Record->SetStringField(TEXT("evidence"), TEXT("live_memory"));
            Record->SetStringField(TEXT("live_kind"), TEXT("asset_editor"));
            Record->SetStringField(TEXT("edited_asset_path"), SafeObjectPath(TargetObject));
            Record->SetStringField(
                TEXT("editor_name"),
                Editor != nullptr ? Editor->GetEditorName().ToString().Left(128) : FString());
            Records.Add(MakeShared<FJsonValueObject>(Record));
            ++OutCounts.Records;
        }
    }

    TArray<UObject*> ScratchReferences;
    FDirectTargetReferenceFinder Finder(ScratchReferences, TargetObject);
    int32 Visited = 0;
    for (FThreadSafeObjectIterator It; It; ++It)
    {
        UObject* Potential = *It;
        if (Potential == nullptr || Potential == TargetObject
            || Potential->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        {
            continue;
        }
        ++OutCounts.Candidates;
        if (Visited >= UnrealMCP::MaxAssetReferenceLiveObjects)
        {
            OutCounts.bTruncated = true;
            break;
        }
        ++Visited;
        ++OutCounts.Scanned;
        Finder.Reset();
        Finder.FindReferences(Potential);
        const int32 ReferenceCount = Finder.GetReferenceCount();
        if (ReferenceCount <= 0)
        {
            continue;
        }
        if (Records.Num() >= UnrealMCP::MaxAssetReferenceRecords)
        {
            OutCounts.bTruncated = true;
            continue;
        }
        const FString PackageName =
            Potential->GetOutermost() != nullptr ? Potential->GetOutermost()->GetName().Left(512) : FString();
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("section"), TEXT("live_memory"));
        Record->SetStringField(TEXT("evidence"), TEXT("live_memory"));
        Record->SetStringField(TEXT("live_kind"), Potential->GetWorld() != nullptr ? TEXT("world_object") : TEXT("loaded_object"));
        Record->SetStringField(TEXT("referencer_object_path"), SafeObjectPath(Potential));
        Record->SetStringField(TEXT("referencer_object_class"), SafeClassPath(Potential));
        Record->SetStringField(TEXT("referencer_package"), PackageName);
        Record->SetStringField(TEXT("mount_point"), PackageMount(PackageName));
        Record->SetNumberField(TEXT("reference_count"), FMath::Min(ReferenceCount, 65535));
        TArray<FProperty*> Properties = Finder.GetProperties();
        Properties.Sort([](const FProperty& Left, const FProperty& Right)
        {
            return Left.GetName() < Right.GetName();
        });
        TArray<TSharedPtr<FJsonValue>> PropertyValues;
        FString LastName;
        for (FProperty* Property : Properties)
        {
            const FString Name = Property != nullptr ? Property->GetName().Left(128) : FString();
            if (Name.IsEmpty() || Name == LastName)
            {
                continue;
            }
            if (PropertyValues.Num() >= UnrealMCP::MaxAssetReferenceProperties)
            {
                Record->SetBoolField(TEXT("properties_truncated"), true);
                break;
            }
            LastName = Name;
            PropertyValues.Add(MakeShared<FJsonValueString>(Name));
        }
        Record->SetArrayField(TEXT("properties"), PropertyValues);
        if (!Record->HasField(TEXT("properties_truncated")))
        {
            Record->SetBoolField(TEXT("properties_truncated"), false);
        }
        Records.Add(MakeShared<FJsonValueObject>(Record));
        ++OutCounts.Records;
    }
}

FString AssetReferenceRecordFingerprint(const TSharedPtr<FJsonValue>& Value)
{
    const TSharedPtr<FJsonObject> Record = Value.IsValid() ? Value->AsObject() : nullptr;
    if (!Record.IsValid())
    {
        return FString();
    }
    TArray<FString> Parts;
    for (const TCHAR* Field : {
        TEXT("section"), TEXT("evidence"), TEXT("dependency_category"), TEXT("target_identifier"),
        TEXT("target_granularity"), TEXT("referencer_identifier"), TEXT("referencer_package"),
        TEXT("mount_point"), TEXT("referencer_primary_asset_id"), TEXT("referencer_asset_path"),
        TEXT("referencer_asset_class"), TEXT("live_kind"), TEXT("edited_asset_path"),
        TEXT("editor_name"), TEXT("referencer_object_path"), TEXT("referencer_object_class")})
    {
        FString Part;
        Record->TryGetStringField(Field, Part);
        Parts.Add(FString(Field) + TEXT("=") + Part);
    }
    for (const TCHAR* Field : {TEXT("dependency_property_bits"), TEXT("reference_count")})
    {
        double Part = 0.0;
        Record->TryGetNumberField(Field, Part);
        Parts.Add(FString(Field) + TEXT("=") + LexToString(static_cast<int64>(Part)));
    }
    for (const TCHAR* Field : {
        TEXT("hard"), TEXT("direct"), TEXT("asset_class_known"), TEXT("properties_truncated")})
    {
        bool Part = false;
        Record->TryGetBoolField(Field, Part);
        Parts.Add(FString(Field) + TEXT("=") + (Part ? TEXT("true") : TEXT("false")));
    }
    for (const TCHAR* Field : {TEXT("dependency_properties"), TEXT("properties")})
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        FString ArrayText;
        if (Record->TryGetArrayField(Field, Values) && Values != nullptr)
        {
            for (const TSharedPtr<FJsonValue>& ArrayValue : *Values)
            {
                ArrayText += ArrayValue.IsValid() ? ArrayValue->AsString() + TEXT(",") : TEXT(",");
            }
        }
        Parts.Add(FString(Field) + TEXT("=") + ArrayText);
    }
    return FString::Join(Parts, TEXT("|"));
}
}

using namespace UnrealMCPAssetReferencesPrivate;

FUnrealMCPAssetReferenceService::FUnrealMCPAssetReferenceService(TFunction<double()> InNow)
    : Now(MoveTemp(InNow))
{
    check(IsInGameThread());
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    Registry.OnAssetAdded().AddRaw(this, &FUnrealMCPAssetReferenceService::BumpRegistrySerial);
    Registry.OnAssetRemoved().AddRaw(this, &FUnrealMCPAssetReferenceService::BumpRegistrySerial);
    Registry.OnAssetRenamed().AddRaw(this, &FUnrealMCPAssetReferenceService::BumpRegistrySerialRenamed);
    Registry.OnAssetUpdated().AddRaw(this, &FUnrealMCPAssetReferenceService::BumpRegistrySerial);
    Registry.OnFilesLoaded().AddRaw(this, &FUnrealMCPAssetReferenceService::BumpRegistrySerialNoArgs);
}

FUnrealMCPAssetReferenceService::~FUnrealMCPAssetReferenceService()
{
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    Registry.OnAssetAdded().RemoveAll(this);
    Registry.OnAssetRemoved().RemoveAll(this);
    Registry.OnAssetRenamed().RemoveAll(this);
    Registry.OnAssetUpdated().RemoveAll(this);
    Registry.OnFilesLoaded().RemoveAll(this);
}

void FUnrealMCPAssetReferenceService::BumpRegistrySerial(const FAssetData&)
{
    ++RegistrySerial;
}

void FUnrealMCPAssetReferenceService::BumpRegistrySerialRenamed(const FAssetData&, const FString&)
{
    ++RegistrySerial;
}

void FUnrealMCPAssetReferenceService::BumpRegistrySerialNoArgs()
{
    ++RegistrySerial;
}

void FUnrealMCPAssetReferenceService::RemoveExpiredCursors(double CurrentTime)
{
    for (auto It = Cursors.CreateIterator(); It; ++It)
    {
        if (It.Value().ExpiresAt <= CurrentTime)
        {
            It.RemoveCurrent();
        }
    }
}

bool FUnrealMCPAssetReferenceService::Inspect(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadAssetReferencePageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    RemoveExpiredCursors(Now());
    return Arguments->HasField(TEXT("cursor"))
        ? Continue(*Arguments, PageSize, OutResult, OutError)
        : InspectInitial(*Arguments, PageSize, OutResult, OutError);
}

bool FUnrealMCPAssetReferenceService::InspectInitial(
    const FJsonObject& Arguments,
    int32 PageSize,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!AssetReferenceHasOnlyFields(Arguments, {TEXT("asset_path"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_references contains an unknown field")};
        return false;
    }
    FString AssetPath;
    if (!Arguments.TryGetStringField(TEXT("asset_path"), AssetPath) || !IsExactMountedAssetPath(AssetPath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must be one exact mounted asset object path")};
        return false;
    }
    const uint64 SnapshotRegistrySerial = RegistrySerial.Load();
    TSharedPtr<FJsonObject> Target;
    TSharedPtr<FJsonObject> Scans;
    TArray<TSharedPtr<FJsonValue>> Records;
    FString Snapshot;
    if (!BuildSnapshot(
        AssetPath, SnapshotRegistrySerial, Target, Scans, Records, Snapshot, OutError))
    {
        return false;
    }
    OutResult = BuildPage(
        AssetPath, Snapshot, Target, Scans, Records, 0, PageSize, SnapshotRegistrySerial);
    return true;
}

bool FUnrealMCPAssetReferenceService::Continue(
    const FJsonObject& Arguments,
    int32 PageSize,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!AssetReferenceHasOnlyFields(Arguments, {TEXT("cursor"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Cursor continuation accepts only cursor and page_size")};
        return false;
    }
    FString Cursor;
    if (!Arguments.TryGetStringField(TEXT("cursor"), Cursor) || !IsOpaqueId(Cursor))
    {
        OutError = {TEXT("invalid_argument"), TEXT("cursor must be a 32-character lowercase hexadecimal opaque value")};
        return false;
    }
    FCursorState* Found = Cursors.Find(Cursor);
    if (Found == nullptr)
    {
        OutError = {TEXT("cursor_expired"), TEXT("The asset-reference cursor is missing or expired"), MakeShared<FJsonObject>(), true};
        return false;
    }
    const FCursorState Saved = *Found;
    Cursors.Remove(Cursor);
    if (Saved.RegistrySerial != RegistrySerial.Load())
    {
        OutError = {
            TEXT("stale_precondition"),
            TEXT("The Asset Registry changed after the reference snapshot was captured")};
        return false;
    }
    if (Saved.Offset < 0 || Saved.Offset > Saved.Records.Num())
    {
        OutError = {TEXT("cursor_expired"), TEXT("The asset-reference cursor no longer identifies a valid page")};
        return false;
    }
    OutResult = BuildPage(
        Saved.AssetPath,
        Saved.SnapshotId,
        Saved.Target,
        Saved.Scans,
        Saved.Records,
        Saved.Offset,
        PageSize,
        Saved.RegistrySerial);
    return true;
}

bool FUnrealMCPAssetReferenceService::BuildSnapshot(
    const FString& AssetPath,
    uint64 SnapshotRegistrySerial,
    TSharedPtr<FJsonObject>& OutTarget,
    TSharedPtr<FJsonObject>& OutScans,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    FUnrealMCPError& OutError)
{
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
    if (!Asset.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested mounted asset was not found")};
        return false;
    }
    if (Asset.GetObjectPathString() != AssetPath)
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must use the exact mounted object path")};
        return false;
    }
    const FString PackageName = Asset.PackageName.ToString();
    if (PackageName.IsEmpty() || FPackageName::GetPackageMountPoint(PackageName).IsNone())
    {
        OutError = {TEXT("invalid_argument"), TEXT("The requested asset is transient or not in mounted content")};
        return false;
    }

    UObject* TargetObject = FindObject<UObject>(nullptr, *AssetPath);
    if (TargetObject != nullptr
        && (TargetObject->HasAnyFlags(RF_Transient) || TargetObject->GetOutermost() == GetTransientPackage()))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The resolved target is transient")};
        return false;
    }
    OutTarget = MakeShared<FJsonObject>();
    OutTarget->SetStringField(TEXT("asset_path"), AssetPath);
    OutTarget->SetStringField(TEXT("package_name"), PackageName.Left(512));
    OutTarget->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString().Left(128));
    OutTarget->SetStringField(TEXT("asset_class"), Asset.AssetClassPath.ToString().Left(512));
    OutTarget->SetStringField(TEXT("mount_point"), PackageMount(PackageName));
    OutTarget->SetBoolField(TEXT("loaded"), TargetObject != nullptr);
    OutTarget->SetBoolField(TEXT("redirector"), Asset.IsRedirector());
    const FPrimaryAssetId TargetPrimaryId = Asset.GetPrimaryAssetId();
    OutTarget->SetStringField(
        TEXT("primary_asset_id"),
        TargetPrimaryId.IsValid() ? TargetPrimaryId.ToString().Left(256) : FString());

    const bool bRegistryStale = Registry.IsGathering();
    FScanCounts Serialized;
    FScanCounts Management;
    FScanCounts Searchable;
    FScanCounts Live;
    const FAssetIdentifier PackageTarget(Asset.PackageName);
    const FAssetIdentifier ObjectTarget(Asset.PackageName, Asset.AssetName);
    AppendRegistryCategory(
        Registry, {PackageTarget}, EDependencyCategory::Package, bRegistryStale, OutRecords, Serialized);
    TArray<FAssetIdentifier> ManagementTargets{PackageTarget, ObjectTarget};
    if (TargetPrimaryId.IsValid())
    {
        ManagementTargets.Add(FAssetIdentifier(TargetPrimaryId));
    }
    AppendRegistryCategory(
        Registry, ManagementTargets, EDependencyCategory::Manage, bRegistryStale, OutRecords, Management);
    AppendRegistryCategory(
        Registry, {ObjectTarget}, EDependencyCategory::SearchableName, bRegistryStale, OutRecords, Searchable);
    if (Registry.IsGathering() || RegistrySerial.Load() != SnapshotRegistrySerial)
    {
        Serialized.bStale = true;
        Management.bStale = true;
        Searchable.bStale = true;
    }
    int32 OpenEditorCount = 0;
    AppendLiveReferences(TargetObject, OutRecords, Live, OpenEditorCount);
    OutTarget->SetNumberField(TEXT("open_editor_count"), OpenEditorCount);

    OutRecords.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
    {
        return AssetReferenceRecordFingerprint(Left) < AssetReferenceRecordFingerprint(Right);
    });
    OutScans = MakeShared<FJsonObject>();
    OutScans->SetObjectField(TEXT("serialized"), ScanStatus(Serialized));
    OutScans->SetObjectField(TEXT("management"), ScanStatus(Management));
    OutScans->SetObjectField(TEXT("searchable_name"), ScanStatus(Searchable));
    OutScans->SetObjectField(TEXT("live_memory"), ScanStatus(Live));

    TArray<FString> Fingerprint{
        AssetPath,
        PackageName,
        Asset.AssetName.ToString(),
        Asset.AssetClassPath.ToString(),
        PackageMount(PackageName),
        TargetObject != nullptr ? TEXT("loaded") : TEXT("unloaded"),
        Asset.IsRedirector() ? TEXT("redirector") : TEXT("asset"),
        TargetPrimaryId.IsValid() ? TargetPrimaryId.ToString() : FString(),
        LexToString(OpenEditorCount)};
    for (const TSharedPtr<FJsonValue>& Record : OutRecords)
    {
        Fingerprint.Add(AssetReferenceRecordFingerprint(Record));
    }
    for (const TCHAR* ScanName : {TEXT("serialized"), TEXT("management"), TEXT("searchable_name"), TEXT("live_memory")})
    {
        const TSharedPtr<FJsonObject> Status = OutScans->GetObjectField(ScanName);
        Fingerprint.Add(
            FString(ScanName) + TEXT("|") + Status->GetStringField(TEXT("status"))
            + TEXT("|") + LexToString(Status->GetIntegerField(TEXT("candidate_count")))
            + TEXT("|") + LexToString(Status->GetIntegerField(TEXT("scanned_count")))
            + TEXT("|") + LexToString(Status->GetIntegerField(TEXT("record_count"))));
    }
    OutSnapshot = HashReferenceText(FString::Join(Fingerprint, TEXT("\n")));
    return true;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetReferenceService::BuildPage(
    const FString& AssetPath,
    const FString& Snapshot,
    const TSharedPtr<FJsonObject>& Target,
    const TSharedPtr<FJsonObject>& Scans,
    const TArray<TSharedPtr<FJsonValue>>& Records,
    int32 Offset,
    int32 PageSize,
    uint64 SnapshotRegistrySerial)
{
    const int32 End = FMath::Min(Offset + PageSize, Records.Num());
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index)
    {
        Page.Add(Records[Index]);
    }
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetObjectField(TEXT("target"), Target.ToSharedRef());
    Result->SetObjectField(TEXT("scans"), Scans.ToSharedRef());
    Result->SetArrayField(TEXT("records"), Page);
    Result->SetNumberField(TEXT("record_count"), Records.Num());
    Result->SetNumberField(TEXT("page_offset"), Offset);
    Result->SetBoolField(TEXT("has_more"), End < Records.Num());
    const TSharedRef<FJsonObject> Limitations = MakeShared<FJsonObject>();
    Limitations->SetBoolField(TEXT("includes_runtime_constructed_paths"), false);
    Limitations->SetBoolField(TEXT("includes_external_code_references"), false);
    Limitations->SetBoolField(TEXT("includes_weak_live_references"), false);
    Limitations->SetStringField(TEXT("serialized_target_granularity"), TEXT("package"));
    Limitations->SetNumberField(TEXT("live_traversal_depth"), 1);
    Result->SetObjectField(TEXT("limitations"), Limitations);
    if (End < Records.Num())
    {
        RemoveExpiredCursors(Now());
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
        const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        Cursors.Add(Cursor, FCursorState{
            AssetPath,
            Snapshot,
            Records,
            Target,
            Scans,
            End,
            SnapshotRegistrySerial,
            Now() + UnrealMCP::CursorLifetimeSeconds});
        Result->SetStringField(TEXT("next_cursor"), Cursor);
        Result->SetNumberField(
            TEXT("cursor_expires_in_ms"),
            static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    return Result;
}
