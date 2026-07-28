#include "UnrealMCPAssetReferenceRegistryScanner.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "UnrealMCPAssetReferenceTargetResolver.h"
#include "UnrealMCPVersion.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCPAssetReferenceRegistryScannerPrivate
{
using UE::AssetRegistry::EDependencyCategory;
using UE::AssetRegistry::EDependencyProperty;

struct FScanCounts
{
    bool bTruncated = false;
    bool bStale = false;
    int32 Candidates = 0;
    int32 Scanned = 0;
    int32 Records = 0;
};

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
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(
        TEXT("status"),
        Counts.bTruncated ? TEXT("truncated")
        : Counts.bStale ? TEXT("stale")
        : TEXT("complete"));
    Result->SetBoolField(TEXT("complete"), !Counts.bTruncated && !Counts.bStale);
    Result->SetBoolField(TEXT("truncated"), Counts.bTruncated);
    Result->SetBoolField(TEXT("unsupported"), false);
    Result->SetBoolField(TEXT("stale"), Counts.bStale);
    Result->SetNumberField(TEXT("candidate_count"), Counts.Candidates);
    Result->SetNumberField(TEXT("scanned_count"), Counts.Scanned);
    Result->SetNumberField(TEXT("record_count"), Counts.Records);
    return Result;
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
        Dependency.Category == EDependencyCategory::Package
            ? TEXT("package")
            : TEXT("object_or_primary_asset"));
    Record->SetStringField(
        TEXT("referencer_identifier"),
        Dependency.AssetId.ToString().Left(512));
    Record->SetStringField(TEXT("referencer_package"), PackageName);
    Record->SetStringField(
        TEXT("mount_point"),
        FUnrealMCPAssetReferenceTargetResolver::PackageMount(PackageName));
    Record->SetNumberField(
        TEXT("dependency_property_bits"),
        static_cast<uint8>(Dependency.Properties));
    Record->SetArrayField(
        TEXT("dependency_properties"),
        PropertyNames(Dependency.Properties));
    if (Dependency.Category == EDependencyCategory::Package)
    {
        Record->SetBoolField(
            TEXT("hard"),
            EnumHasAnyFlags(Dependency.Properties, EDependencyProperty::Hard));
    }
    if (Dependency.Category == EDependencyCategory::Manage)
    {
        Record->SetBoolField(
            TEXT("direct"),
            EnumHasAnyFlags(Dependency.Properties, EDependencyProperty::Direct));
    }
    const FPrimaryAssetId PrimaryId = Dependency.AssetId.GetPrimaryAssetId();
    Record->SetStringField(
        TEXT("referencer_primary_asset_id"),
        PrimaryId.IsValid() ? PrimaryId.ToString().Left(256) : FString());
    if (ReferencerAsset != nullptr && ReferencerAsset->IsValid())
    {
        Record->SetStringField(
            TEXT("referencer_asset_path"),
            ReferencerAsset->GetObjectPathString().Left(512));
        Record->SetStringField(
            TEXT("referencer_asset_class"),
            ReferencerAsset->AssetClassPath.ToString().Left(512));
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
            Identifier.PackageName.ToString() + TEXT(".")
            + Identifier.ObjectName.ToString());
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
            OutAssets.SetNum(
                UnrealMCP::MaxAssetReferenceAssetsPerPackage,
                EAllowShrinking::No);
            OutTruncated = true;
        }
    }
}

void AppendCategory(
    IAssetRegistry& Registry,
    const TArray<FAssetIdentifier>& Targets,
    EDependencyCategory Category,
    bool bRegistryStale,
    TArray<TSharedPtr<FJsonValue>>& Records,
    FScanCounts& OutCounts)
{
    OutCounts.bStale = bRegistryStale;
    TArray<TPair<FString, FAssetDependency>> Dependencies;
    TSet<FString> Seen;
    for (const FAssetIdentifier& Target : Targets)
    {
        TArray<FAssetDependency> Found;
        Registry.GetReferencers(Target, Found, Category);
        OutCounts.Candidates += Found.Num();
        for (const FAssetDependency& Dependency : Found)
        {
            const FString Key =
                Target.ToString() + TEXT("|") + Dependency.AssetId.ToString()
                + TEXT("|")
                + LexToString(static_cast<uint8>(Dependency.Properties));
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
    Dependencies.Sort(
        [](const TPair<FString, FAssetDependency>& Left,
           const TPair<FString, FAssetDependency>& Right)
        {
            const FString LeftKey =
                Left.Key + TEXT("|") + Left.Value.AssetId.ToString()
                + TEXT("|")
                + LexToString(static_cast<uint8>(Left.Value.Properties));
            const FString RightKey =
                Right.Key + TEXT("|") + Right.Value.AssetId.ToString()
                + TEXT("|")
                + LexToString(static_cast<uint8>(Right.Value.Properties));
            return LeftKey < RightKey;
        });
    for (const TPair<FString, FAssetDependency>& Pair : Dependencies)
    {
        ++OutCounts.Scanned;
        TArray<FAssetData> Assets;
        ResolveReferencerAssets(
            Registry,
            Pair.Value.AssetId,
            Assets,
            OutCounts.bTruncated);
        if (Assets.IsEmpty())
        {
            if (Records.Num() >= UnrealMCP::MaxAssetReferenceRecords)
            {
                OutCounts.bTruncated = true;
                continue;
            }
            Records.Add(
                MakeShared<FJsonValueObject>(
                    RegistryRecord(Pair.Value, Pair.Key, nullptr)));
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
            Records.Add(
                MakeShared<FJsonValueObject>(
                    RegistryRecord(Pair.Value, Pair.Key, &Asset)));
            ++OutCounts.Records;
        }
    }
}
}

void FUnrealMCPAssetReferenceRegistryScanner::Scan(
    const FUnrealMCPResolvedAssetReferenceTarget& Target,
    uint64 SnapshotRegistrySerial,
    TFunctionRef<uint64()> CurrentRegistrySerial,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    TSharedPtr<FJsonObject>& OutScans) const
{
    using namespace UnrealMCPAssetReferenceRegistryScannerPrivate;
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const bool bRegistryStale = Registry.IsGathering();
    FScanCounts Serialized;
    FScanCounts Management;
    FScanCounts Searchable;
    const FAssetIdentifier PackageTarget(Target.Asset.PackageName);
    const FAssetIdentifier ObjectTarget(
        Target.Asset.PackageName,
        Target.Asset.AssetName);
    AppendCategory(
        Registry,
        {PackageTarget},
        EDependencyCategory::Package,
        bRegistryStale,
        OutRecords,
        Serialized);
    TArray<FAssetIdentifier> ManagementTargets{PackageTarget, ObjectTarget};
    const FPrimaryAssetId PrimaryId = Target.Asset.GetPrimaryAssetId();
    if (PrimaryId.IsValid())
    {
        ManagementTargets.Add(FAssetIdentifier(PrimaryId));
    }
    AppendCategory(
        Registry,
        ManagementTargets,
        EDependencyCategory::Manage,
        bRegistryStale,
        OutRecords,
        Management);
    AppendCategory(
        Registry,
        {ObjectTarget},
        EDependencyCategory::SearchableName,
        bRegistryStale,
        OutRecords,
        Searchable);
    if (Registry.IsGathering()
        || CurrentRegistrySerial() != SnapshotRegistrySerial)
    {
        Serialized.bStale = true;
        Management.bStale = true;
        Searchable.bStale = true;
    }
    OutScans = MakeShared<FJsonObject>();
    OutScans->SetObjectField(TEXT("serialized"), ScanStatus(Serialized));
    OutScans->SetObjectField(TEXT("management"), ScanStatus(Management));
    OutScans->SetObjectField(TEXT("searchable_name"), ScanStatus(Searchable));
}
