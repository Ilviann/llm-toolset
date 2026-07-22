#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPBlueprintInspectionBuilder.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{
bool BuildDiscovery(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError)
{
    if (!HasOnlyFields(Arguments, {TEXT("mode"), TEXT("package_path"), TEXT("asset_name"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Discovery arguments contain an unknown field")};
        return false;
    }
    FString PackagePath;
    FString RawPackagePath;
    if (Arguments.HasField(TEXT("package_path")) && !Arguments.TryGetStringField(TEXT("package_path"), RawPackagePath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be a string")};
        return false;
    }
    if (!NormalizePackagePath(RawPackagePath, PackagePath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be a valid mounted-content package path")};
        return false;
    }
    FString AssetName;
    if (Arguments.HasField(TEXT("asset_name"))
        && (!Arguments.TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty() || AssetName.Len() > 128))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_name must be a non-empty bounded string")};
        return false;
    }

    FARFilter Filter;
    if (!PackagePath.IsEmpty())
    {
        Filter.PackagePaths.Add(FName(*PackagePath));
    }
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    TArray<FAssetData> Assets;
    int32 Scanned = 0;
    OutScanTruncated = false;
    FAssetRegistryModule::GetRegistry().EnumerateAssets(Filter, [&Assets, &Scanned, &OutScanTruncated](const FAssetData& Asset)
    {
        if (Scanned++ >= UnrealMCP::MaxDiscoveryScan)
        {
            OutScanTruncated = true;
            return false;
        }
        Assets.Add(Asset);
        return true;
    });
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.GetObjectPathString() < Right.GetObjectPathString();
    });
    TArray<FString> Fingerprint;
    for (int32 Index = 0; Index < Assets.Num(); ++Index)
    {
        const FAssetData& Asset = Assets[Index];
        const UnrealMCP::BlueprintFamilyPolicy::FFamilyInfo Family = AssetBlueprintFamily(Asset);
        if ((!AssetName.IsEmpty() && Asset.AssetName.ToString() != AssetName) || !Family.bSupported)
        {
            continue;
        }
        const TSharedRef<FJsonObject> Value = Record(TEXT("asset"));
        Value->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
        Value->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
        Value->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
        Value->SetStringField(TEXT("blueprint_family"), Family.Name);
        Value->SetStringField(TEXT("native_family_class"), Family.NativeBaseClass);
        FString Parent;
        if (Asset.GetTagValue(FBlueprintTags::ParentClassPath, Parent))
        {
            Parent = FPackageName::ExportTextPathToObjectPath(Parent);
        }
        Value->SetStringField(TEXT("parent_class"), Parent);
        AddRecord(OutRecords, Value);
        Fingerprint.Add(Asset.GetObjectPathString() + TEXT("|") + Parent + TEXT("|") + Family.Name);
    }
    OutSnapshot = HashLines(MoveTemp(Fingerprint));
    return true;
}
}
