#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Curves/CurveFloat.h"
#include "UnrealMCPAssetDeletionService.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPGameDataService.h"

namespace
{
TSharedRef<FUnrealMCPRecord> DataCreateArguments(
    const FString& Target,
    const FString& AssetPath)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("target"), Target);
    Arguments->SetStringField(TEXT("operation"), TEXT("create"));
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    return Arguments;
}

bool SaveCurve(UCurveFloat* Curve, FString& OutFilename)
{
    if (Curve == nullptr || Curve->GetOutermost() == nullptr)
    {
        return false;
    }
    OutFilename = FPackageName::LongPackageNameToFilename(
        Curve->GetOutermost()->GetName(),
        FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    return UPackage::SavePackage(
        Curve->GetOutermost(), Curve, *OutFilename, SaveArgs);
}

TSharedRef<FUnrealMCPRecord> DeleteArguments(
    const FString& AssetPath,
    const FString& Snapshot)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    return Arguments;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetDeleteTest,
    "UnrealMCP.AssetDelete.PreflightPersistenceAndReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetDeleteTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName =
        TEXT("/Game/UnrealMCPAssetDeleteTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits)
        + TEXT("/Curve");
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString AssetPath = PackageName + TEXT(".") + AssetName;
    UPackage* Package = CreatePackage(*PackageName);
    UCurveFloat* Curve = NewObject<UCurveFloat>(
        Package,
        *AssetName,
        RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Curve);
    FString Filename;
    if (!TestTrue(TEXT("disposable asset saves"), SaveCurve(Curve, Filename)))
    {
        return false;
    }
    Package->SetDirtyFlag(false);

    FUnrealMCPAssetReferenceService References;
    FUnrealMCPAssetDeletionService Deletion(References);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Inspect = MakeShared<FUnrealMCPRecord>();
    Inspect->SetStringField(TEXT("asset_path"), AssetPath);
    Inspect->SetNumberField(TEXT("page_size"), 100);
    if (!TestTrue(
        TEXT("disposable reference snapshot succeeds"),
        References.Inspect(Inspect, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestFalse(
        TEXT("stale deletion rejects"),
        Deletion.Delete(
            DeleteArguments(AssetPath, FString::ChrN(40, TEXT('0'))),
            Result,
            Error));
    TestEqual(
        TEXT("stale deletion is explicit"),
        Error.Code,
        FString(TEXT("stale_precondition")));
    TestTrue(TEXT("stale rejection preserves storage"), IFileManager::Get().FileExists(*Filename));

    const FString ReferencePrefix =
        TEXT("/Game/UnrealMCPAssetDeleteReferenceTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FUnrealMCPGameDataService GameData;
    TSharedRef<FUnrealMCPRecord> CreateStruct =
        DataCreateArguments(TEXT("user_defined_struct"), ReferencePrefix + TEXT("/ST_Target"));
    const TSharedRef<FUnrealMCPRecord> Member = MakeShared<FUnrealMCPRecord>();
    Member->SetStringField(TEXT("name"), TEXT("Value"));
    Member->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    Member->SetObjectField(
        TEXT("default"),
        LiteralDefault(MakeShared<FUnrealMCPValueNumber>(1)));
    CreateStruct->SetArrayField(
        TEXT("members"),
        {MakeShared<FUnrealMCPValueObject>(Member)});
    if (!TestTrue(
        TEXT("referenced deletion target creates"),
        GameData.Edit(CreateStruct, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString ReferencedPath = Result->GetStringField(TEXT("asset_path"));
    TSharedRef<FUnrealMCPRecord> CreateTable =
        DataCreateArguments(TEXT("data_table"), ReferencePrefix + TEXT("/DT_Referencer"));
    CreateTable->SetStringField(TEXT("row_struct"), ReferencedPath);
    if (!TestTrue(
        TEXT("serialized referencer creates"),
        GameData.Edit(CreateTable, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString ReferencerPath = Result->GetStringField(TEXT("asset_path"));
    const TSharedRef<FUnrealMCPRecord> InspectReferenced = MakeShared<FUnrealMCPRecord>();
    InspectReferenced->SetStringField(TEXT("asset_path"), ReferencedPath);
    InspectReferenced->SetNumberField(TEXT("page_size"), 100);
    if (!TestTrue(
        TEXT("referenced target snapshot succeeds"),
        References.Inspect(InspectReferenced, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    FString ReferencedFilename;
    TestTrue(
        TEXT("referenced target has persisted storage"),
        FPackageName::DoesPackageExist(
            FPackageName::ObjectPathToPackageName(ReferencedPath),
            &ReferencedFilename));
    TestFalse(
        TEXT("serialized asset reference rejects"),
        Deletion.Delete(
            DeleteArguments(
                ReferencedPath,
                Result->GetStringField(TEXT("snapshot_id"))),
            Result,
            Error));
    TestEqual(
        TEXT("reference rejection is explicit"),
        Error.Code,
        FString(TEXT("asset_referenced")));
    TestTrue(
        TEXT("reference rejection preserves storage"),
        IFileManager::Get().FileExists(*ReferencedFilename));

    const TSharedRef<FUnrealMCPRecord> InspectReferencer = MakeShared<FUnrealMCPRecord>();
    InspectReferencer->SetStringField(TEXT("asset_path"), ReferencerPath);
    InspectReferencer->SetNumberField(TEXT("page_size"), 100);
    if (!TestTrue(
        TEXT("Data Table deletion snapshot succeeds"),
        References.Inspect(InspectReferencer, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    if (!TestTrue(
        TEXT("clean Data Table deletes"),
        Deletion.Delete(
            DeleteArguments(
                ReferencerPath,
                Result->GetStringField(TEXT("snapshot_id"))),
            Result,
            Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(TEXT("Data Table delete result is verified"), Result->GetBoolField(TEXT("deleted")));
    TestFalse(
        TEXT("deleted Data Table no longer resolves before restart"),
        References.Inspect(InspectReferencer, Result, Error));
    TestEqual(
        TEXT("deleted Data Table reports not found"),
        Error.Code,
        FString(TEXT("not_found")));

    if (!TestTrue(
        TEXT("fresh reference snapshot succeeds"),
        References.Inspect(Inspect, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    if (!TestTrue(
        TEXT("clean exact asset deletes"),
        Deletion.Delete(
            DeleteArguments(AssetPath, Result->GetStringField(TEXT("snapshot_id"))),
            Result,
            Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(TEXT("delete result is verified"), Result->GetBoolField(TEXT("deleted")));
    TestFalse(TEXT("asset registry no longer resolves target"),
        FAssetRegistryModule::GetRegistry()
            .GetAssetByObjectPath(FSoftObjectPath(AssetPath))
            .IsValid());
    TestFalse(TEXT("package file is absent"), IFileManager::Get().FileExists(*Filename));
    TestFalse(TEXT("asset deletion does not claim Undo"), Result->GetBoolField(TEXT("undo_supported")));
    return true;
}

#endif
