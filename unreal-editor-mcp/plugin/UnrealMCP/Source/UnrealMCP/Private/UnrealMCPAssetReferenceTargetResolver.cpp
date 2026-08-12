#include "UnrealMCPAssetReferenceTargetResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

bool FUnrealMCPAssetReferenceTargetResolver::IsExactMountedAssetPath(const FString& Value)
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

FString FUnrealMCPAssetReferenceTargetResolver::PackageMount(const FString& PackageName)
{
    return PackageName.IsEmpty()
        ? FString()
        : FPackageName::GetPackageMountPoint(PackageName).ToString().Left(128);
}

bool FUnrealMCPAssetReferenceTargetResolver::Resolve(
    const FString& AssetPath,
    FUnrealMCPResolvedAssetReferenceTarget& OutTarget,
    FUnrealMCPError& OutError) const
{
    check(IsInGameThread());
    if (!IsExactMountedAssetPath(AssetPath))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("asset_path must be one exact mounted asset object path")};
        return false;
    }

    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
    if (!Asset.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested mounted asset was not found")};
        return false;
    }
    if (Asset.GetObjectPathString() != AssetPath)
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("asset_path must use the exact mounted object path")};
        return false;
    }

    const FString PackageName = Asset.PackageName.ToString();
    if (PackageName.IsEmpty() || FPackageName::GetPackageMountPoint(PackageName).IsNone())
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("The requested asset is transient or not in mounted content")};
        return false;
    }

    UObject* LoadedObject = FindObject<UObject>(nullptr, *AssetPath);
    if (LoadedObject != nullptr
        && (LoadedObject->HasAnyFlags(RF_Transient)
            || LoadedObject->GetOutermost() == GetTransientPackage()))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The resolved target is transient")};
        return false;
    }

    OutTarget = FUnrealMCPResolvedAssetReferenceTarget();
    OutTarget.AssetPath = AssetPath;
    OutTarget.PackageName = PackageName;
    OutTarget.Asset = Asset;
    OutTarget.LoadedObject = LoadedObject;
    OutTarget.Metadata = MakeShared<FUnrealMCPRecord>();
    OutTarget.Metadata->SetStringField(TEXT("asset_path"), AssetPath);
    OutTarget.Metadata->SetStringField(TEXT("package_name"), PackageName.Left(512));
    OutTarget.Metadata->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString().Left(128));
    OutTarget.Metadata->SetStringField(
        TEXT("asset_class"),
        Asset.AssetClassPath.ToString().Left(512));
    OutTarget.Metadata->SetStringField(TEXT("mount_point"), PackageMount(PackageName));
    OutTarget.Metadata->SetBoolField(TEXT("loaded"), LoadedObject != nullptr);
    OutTarget.Metadata->SetBoolField(TEXT("redirector"), Asset.IsRedirector());
    const FPrimaryAssetId PrimaryId = Asset.GetPrimaryAssetId();
    OutTarget.Metadata->SetStringField(
        TEXT("primary_asset_id"),
        PrimaryId.IsValid() ? PrimaryId.ToString().Left(256) : FString());
    return true;
}
