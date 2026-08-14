#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAssetInspectDataTestTypes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Misc/AutomationTest.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionService.h"
#include "UnrealMCPDataInspectionAdapters.h"
#include "UnrealMCPNeutralAssetInspectionAdapter.h"
#include "UnrealMCPWireTypes.h"

namespace
{
TSharedRef<FUnrealMCPRecord> Request(
    const FString& AssetPath,
    const FString& Selector = FString(),
    int32 PageSize = 10,
    int32 PageIndex = 0,
    bool bPage = false)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    if (!Selector.IsEmpty()) Result->SetStringField(TEXT("selector"), Selector);
    if (bPage)
    {
        Result->SetNumberField(TEXT("page_size"), PageSize);
        Result->SetNumberField(TEXT("page_index"), PageIndex);
    }
    return Result;
}

FString AssetType(const TSharedPtr<FUnrealMCPRecord>& Result)
{
    return Result->GetObjectField(TEXT("asset"))->GetStringField(TEXT("type"));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPAssetInspectDataFamiliesTest,
    "UnrealMCP.AssetInspect.DataAssetsTablesSelectorsAndSnapshots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPAssetInspectDataFamiliesTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const TSharedRef<FUnrealMCPAssetFamilyRegistry> Registry = MakeShared<FUnrealMCPAssetFamilyRegistry>();
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("neutral inspection family registers"),
            UnrealMCP::AssetCore::RegisterNeutralAssetAdapter(*Registry, Error))
        || !TestTrue(TEXT("data inspection families register"),
            UnrealMCP::DataInspection::RegisterAdapters(*Registry, Error))
        || !TestTrue(TEXT("data inspection registry freezes"), Registry->Freeze(Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    FUnrealMCPAssetInspectionService Service(Registry);
    TSharedPtr<FUnrealMCPRecord> Result;

    const FString DataAssetPackage = Root + TEXT("/DA_Weapon");
    UPackage* DataAssetOuter = CreatePackage(*DataAssetPackage);
    UUnrealMCPAssetInspectDataFixture* DataAsset = NewObject<UUnrealMCPAssetInspectDataFixture>(
        DataAssetOuter, TEXT("DA_Weapon"), RF_Public | RF_Standalone);
    DataAsset->Tags = {TEXT("axe"), TEXT("melee"), TEXT("tool")};
    DataAsset->UnsupportedSubobject = NewObject<UUnrealMCPAssetInspectInlineFixture>(
        DataAsset, TEXT("InlineState"), RF_Transactional);
    FAssetRegistryModule::AssetCreated(DataAsset);
    const bool bDataAssetDirtyBefore = DataAssetOuter->IsDirty();
    if (!TestTrue(TEXT("Primary Data Asset root inspects"),
        Service.Execute(Request(DataAssetPackage), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    TestEqual(TEXT("Primary Data Asset classifies exactly"), AssetType(Result), FString(TEXT("primary_data_asset")));
    TestTrue(TEXT("Primary Data Asset snapshot is stable identity"), Result->GetStringField(TEXT("snapshot_id")).Len() == 40);
    TestTrue(TEXT("Data Asset property index is present"), Result->GetObjectField(TEXT("properties")).IsValid());
    TestEqual(TEXT("inspection preserves Data Asset dirty state"), DataAssetOuter->IsDirty(), bDataAssetDirtyBefore);

    if (!TestTrue(TEXT("Data Asset array property pages"),
        Service.Execute(Request(DataAssetPackage, TEXT("properties/Tags"), 2, 1, true), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    TestEqual(TEXT("array page returns final item"), Result->GetArrayField(TEXT("items")).Num(), 1);
    TestEqual(TEXT("array page is zero based"),
        Result->GetObjectField(TEXT("page"))->GetIntegerField(TEXT("index")), 1);

    const FString TablePackage = Root + TEXT("/DT_Weapons");
    UPackage* TableOuter = CreatePackage(*TablePackage);
    UDataTable* Table = NewObject<UDataTable>(TableOuter, TEXT("DT_Weapons"), RF_Public | RF_Standalone);
    Table->RowStruct = FUnrealMCPAssetInspectDataRow::StaticStruct();
    FUnrealMCPAssetInspectDataRow Axe;
    Axe.Damage = 35; Axe.Tags = {TEXT("axe"), TEXT("melee")}; Axe.Multipliers.Add(TEXT("wood"), 1.5f);
    FUnrealMCPAssetInspectDataRow Bow;
    Bow.Damage = 22; Bow.Tags = {TEXT("bow"), TEXT("ranged")}; Bow.Multipliers.Add(TEXT("wood"), 0.75f);
    Table->AddRow(TEXT("Axe"), Axe); Table->AddRow(TEXT("Bow"), Bow);
    FAssetRegistryModule::AssetCreated(Table);
    const bool bTableDirtyBefore = TableOuter->IsDirty();

    if (!TestTrue(TEXT("Data Table root index pages"),
        Service.Execute(Request(TablePackage, FString(), 1, 1, true), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    TestEqual(TEXT("Data Table classifies exactly"), AssetType(Result), FString(TEXT("data_table")));
    TestEqual(TEXT("root row page returns one name"),
        Result->GetObjectField(TEXT("rows"))->GetArrayField(TEXT("index")).Num(), 1);
    TestEqual(TEXT("inspection preserves Data Table dirty state"), TableOuter->IsDirty(), bTableDirtyBefore);

    if (!TestTrue(TEXT("exact Data Table row inspects"),
        Service.Execute(Request(TablePackage, TEXT("rows/Axe")), Result, Error))) return false;
    TestEqual(TEXT("exact row identity is retained"),
        Result->GetObjectField(TEXT("row"))->GetStringField(TEXT("name")), FString(TEXT("Axe")));
    TestTrue(TEXT("row collection becomes selector descriptor"),
        Result->GetObjectField(TEXT("row"))->GetObjectField(TEXT("values"))->GetObjectField(TEXT("Tags")).IsValid());

    if (!TestTrue(TEXT("nested row array pages"),
        Service.Execute(Request(TablePackage, TEXT("rows/Axe/Tags"), 1, 1, true), Result, Error))) return false;
    TestEqual(TEXT("nested row page returns one tag"), Result->GetArrayField(TEXT("items")).Num(), 1);

    const FGameplayTag ParentTag = FGameplayTag::RequestGameplayTag(FName(TEXT("UnrealMCP.Test")), false);
    const FGameplayTag ChildTag = FGameplayTag::RequestGameplayTag(FName(TEXT("UnrealMCP.Test.Child")), false);
    if (!TestTrue(TEXT("native Gameplay Tag fixtures are registered"), ParentTag.IsValid() && ChildTag.IsValid()))
        return false;
    const FString TagTablePackage = Root + TEXT("/DT_GameplayTags");
    UPackage* TagTableOuter = CreatePackage(*TagTablePackage);
    UDataTable* TagTable = NewObject<UDataTable>(
        TagTableOuter, TEXT("DT_GameplayTags"), RF_Public | RF_Standalone);
    TagTable->RowStruct = FUnrealMCPGameplayTagDataRow::StaticStruct();
    FUnrealMCPGameplayTagDataRow TagRow;
    TagRow.Tag = ChildTag;
    TagRow.Tags.AddTag(ChildTag);
    TagRow.Tags.AddTag(ParentTag);
    TagRow.Nested.Tag = ParentTag;
    TagRow.Nested.Tags.AddTag(ChildTag);
    TagTable->AddRow(TEXT("Exact"), TagRow);
    FAssetRegistryModule::AssetCreated(TagTable);

    if (!TestTrue(TEXT("Gameplay Tag Data Table row inspects semantically"),
        Service.Execute(Request(TagTablePackage, TEXT("rows/Exact")), Result, Error))) return false;
    const TSharedPtr<FUnrealMCPRecord> TagValues =
        Result->GetObjectField(TEXT("row"))->GetObjectField(TEXT("values"));
    TestEqual(TEXT("Gameplay Tag is an exact string"),
        TagValues->GetStringField(TEXT("Tag")), FString(TEXT("UnrealMCP.Test.Child")));
    const TArray<TSharedPtr<FUnrealMCPValue>> ExplicitTags = TagValues->GetArrayField(TEXT("Tags"));
    TestEqual(TEXT("Gameplay Tag Container exposes explicit tags only"), ExplicitTags.Num(), 2);
    TestEqual(TEXT("Gameplay Tag Container is sorted"),
        ExplicitTags[0]->AsString(), FString(TEXT("UnrealMCP.Test")));
    TestEqual(TEXT("nested Gameplay Tag uses the same semantic form"),
        TagValues->GetObjectField(TEXT("Nested"))->GetObjectField(TEXT("fields"))
            ->GetStringField(TEXT("Tag")), FString(TEXT("UnrealMCP.Test")));

    Error = {};
    TestFalse(TEXT("Gameplay Tags are semantic selector leaves"),
        Service.Execute(Request(TagTablePackage, TEXT("rows/Exact/Tag/TagName")), Result, Error));
    TestEqual(TEXT("Gameplay Tag internal selector rejection is stable"),
        Error.Code, FString(TEXT("not_found")));

    Error = {};
    if (!TestTrue(TEXT("Data Table column projection pages"),
        Service.Execute(Request(TablePackage, TEXT("columns/Damage"), 1, 0, true), Result, Error))) return false;
    TestEqual(TEXT("column projection returns one row value"), Result->GetArrayField(TEXT("values")).Num(), 1);

    TSharedRef<FUnrealMCPRecord> InvalidPartial = Request(TablePackage);
    InvalidPartial->SetBoolField(TEXT("allow_partial_graph"), false);
    TestFalse(TEXT("graph fallback flag rejects for data families"), Service.Execute(InvalidPartial, Result, Error));
    TestEqual(TEXT("data-family graph flag error is stable"), Error.Code, FString(TEXT("invalid_argument")));
    return true;
}

#endif
