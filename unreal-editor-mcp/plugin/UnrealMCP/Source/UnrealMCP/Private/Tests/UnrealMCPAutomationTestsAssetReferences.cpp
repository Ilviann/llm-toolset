#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPGameDataService.h"

namespace
{
TSharedRef<FJsonObject> DataCreateArguments(
    const FString& Target,
    const FString& AssetPath)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("target"), Target);
    Arguments->SetStringField(TEXT("operation"), TEXT("create"));
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    return Arguments;
}

bool HasEvidence(const TArray<TSharedPtr<FJsonValue>>& Records, const FString& Evidence)
{
    for (const TSharedPtr<FJsonValue>& Value : Records)
    {
        const TSharedPtr<FJsonObject> Record = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Actual;
        if (Record.IsValid() && Record->TryGetStringField(TEXT("evidence"), Actual) && Actual == Evidence)
        {
            return true;
        }
    }
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetReferencesTest,
    "UnrealMCP.AssetReferences.RegistryLiveMemoryAndCursors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetReferencesTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Prefix =
        TEXT("/Game/UnrealMCPAssetReferenceTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString StructPackage = Prefix + TEXT("/ST_Target");
    const FString TablePackageA = Prefix + TEXT("/DT_ReferencerA");
    const FString TablePackageB = Prefix + TEXT("/DT_ReferencerB");

    FUnrealMCPGameDataService GameData;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    TSharedRef<FJsonObject> CreateStruct =
        DataCreateArguments(TEXT("user_defined_struct"), StructPackage);
    const TSharedRef<FJsonObject> Member = MakeShared<FJsonObject>();
    Member->SetStringField(TEXT("name"), TEXT("Value"));
    Member->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    Member->SetObjectField(
        TEXT("default"),
        LiteralDefault(MakeShared<FJsonValueNumber>(1)));
    CreateStruct->SetArrayField(
        TEXT("members"),
        {MakeShared<FJsonValueObject>(Member)});
    if (!TestTrue(
        TEXT("reference target struct creates"),
        GameData.Edit(CreateStruct, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString StructPath = Result->GetStringField(TEXT("asset_path"));

    for (const FString& TablePackage : {TablePackageA, TablePackageB})
    {
        TSharedRef<FJsonObject> CreateTable =
            DataCreateArguments(TEXT("data_table"), TablePackage);
        CreateTable->SetStringField(TEXT("row_struct"), StructPath);
        if (!TestTrue(
            TEXT("serialized referencer Data Table creates"),
            GameData.Edit(CreateTable, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
    }

    UObject* Target = FindObject<UObject>(nullptr, *StructPath);
    TestNotNull(TEXT("reference target remains loaded"), Target);
    const bool bDirtyBefore =
        Target != nullptr && Target->GetOutermost() != nullptr
        ? Target->GetOutermost()->IsDirty()
        : false;

    double CurrentTime = 100.0;
    FUnrealMCPAssetReferenceService Service(
        [&CurrentTime] { return CurrentTime; });
    const TSharedRef<FJsonObject> Inspect = MakeShared<FJsonObject>();
    Inspect->SetStringField(TEXT("asset_path"), StructPath);
    Inspect->SetNumberField(TEXT("page_size"), 1);
    if (!TestTrue(
        TEXT("bounded asset-reference scan succeeds"),
        Service.Inspect(Inspect, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(
        TEXT("exact target path is retained"),
        Result->GetStringField(TEXT("asset_path")),
        StructPath);
    TestEqual(
        TEXT("reference snapshot is exact"),
        Result->GetStringField(TEXT("snapshot_id")).Len(),
        40);
    TestTrue(
        TEXT("serialized and live referencers produce multiple bounded records"),
        Result->GetIntegerField(TEXT("record_count")) >= 2);
    TestTrue(TEXT("first page has a continuation"), Result->GetBoolField(TEXT("has_more")));
    const FString Cursor = Result->GetStringField(TEXT("next_cursor"));
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestFalse(
        TEXT("loaded target keeps live scan supported"),
        Result->GetObjectField(TEXT("scans"))
            ->GetObjectField(TEXT("live_memory"))
            ->GetBoolField(TEXT("unsupported")));
    TestTrue(
        TEXT("serialized registry evidence is reported"),
        Result->GetObjectField(TEXT("scans"))
            ->GetObjectField(TEXT("serialized"))
            ->GetIntegerField(TEXT("record_count")) >= 2);
    TestEqual(
        TEXT("inspection preserves package dirtiness"),
        Target->GetOutermost()->IsDirty(),
        bDirtyBefore);

    const TSharedRef<FJsonObject> Continue = MakeShared<FJsonObject>();
    Continue->SetStringField(TEXT("cursor"), Cursor);
    Continue->SetNumberField(TEXT("page_size"), 100);
    TestTrue(
        TEXT("single-use cursor continues exact snapshot"),
        Service.Inspect(Continue, Result, Error));
    TestEqual(
        TEXT("continuation retains snapshot"),
        Result->GetStringField(TEXT("snapshot_id")),
        Snapshot);
    TestTrue(
        TEXT("continued page includes actionable evidence"),
        HasEvidence(Result->GetArrayField(TEXT("records")), TEXT("serialized"))
        || HasEvidence(Result->GetArrayField(TEXT("records")), TEXT("live_memory")));
    TestFalse(
        TEXT("consumed cursor cannot replay"),
        Service.Inspect(Continue, Result, Error));
    TestEqual(
        TEXT("consumed cursor reports expiry"),
        Error.Code,
        FString(TEXT("cursor_expired")));

    TestTrue(
        TEXT("fresh scan creates a cursor for stale-state coverage"),
        Service.Inspect(Inspect, Result, Error));
    const FString StaleCursor = Result->GetStringField(TEXT("next_cursor"));
    UPackage* EventPackage = CreatePackage(*(Prefix + TEXT("/RegistryEvent")));
    UCurveFloat* EventAsset = NewObject<UCurveFloat>(
        EventPackage,
        TEXT("RegistryEvent"),
        RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(EventAsset);
    const TSharedRef<FJsonObject> Stale = MakeShared<FJsonObject>();
    Stale->SetStringField(TEXT("cursor"), StaleCursor);
    TestFalse(
        TEXT("registry change invalidates continuation"),
        Service.Inspect(Stale, Result, Error));
    TestEqual(
        TEXT("registry-change rejection is stable"),
        Error.Code,
        FString(TEXT("stale_precondition")));
    FAssetRegistryModule::AssetDeleted(EventAsset);
    EventAsset->ClearFlags(RF_Public | RF_Standalone);
    EventPackage->SetDirtyFlag(false);

    const TSharedRef<FJsonObject> PackageOnly = MakeShared<FJsonObject>();
    PackageOnly->SetStringField(TEXT("asset_path"), StructPackage);
    TestFalse(
        TEXT("ambiguous package-only input rejects"),
        Service.Inspect(PackageOnly, Result, Error));
    TestEqual(
        TEXT("package-only rejection is explicit"),
        Error.Code,
        FString(TEXT("invalid_argument")));

    const TSharedRef<FJsonObject> Missing = MakeShared<FJsonObject>();
    Missing->SetStringField(TEXT("asset_path"), Prefix + TEXT("/Missing.Missing"));
    TestFalse(
        TEXT("missing target rejects"),
        Service.Inspect(Missing, Result, Error));
    TestEqual(
        TEXT("missing target is explicit"),
        Error.Code,
        FString(TEXT("not_found")));
    return true;
}

#endif
