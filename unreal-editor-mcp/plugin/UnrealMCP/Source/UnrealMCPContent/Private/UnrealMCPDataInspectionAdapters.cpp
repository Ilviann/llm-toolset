#include "UnrealMCPDataInspectionAdapters.h"

#include "AssetRegistry/AssetBundleData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPGameDataInspectionBuilder.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPStructuredDataInspection.h"
#include "UnrealMCPVersion.h"
#include "UObject/PrimaryAssetId.h"

namespace UnrealMCP::DataInspection::Private
{
TSharedRef<FUnrealMCPRecord> BaseResult(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    const FString& Type,
    const FString& Class,
    const FString& ParentType = FString())
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("format"), TEXT("yaml"));
    Result->SetNumberField(TEXT("schema_version"), 1);
    Result->SetStringField(TEXT("snapshot_id"), Context.Identity.SnapshotId);
    const TSharedRef<FUnrealMCPRecord> Asset = MakeShared<FUnrealMCPRecord>();
    Asset->SetStringField(TEXT("path"), Context.Identity.ObjectPath);
    Asset->SetStringField(TEXT("type"), Type);
    if (!Class.IsEmpty()) Asset->SetStringField(TEXT("class"), Class);
    if (!ParentType.IsEmpty()) Asset->SetStringField(TEXT("parent_type"), ParentType);
    Result->SetObjectField(TEXT("asset"), Asset);
    return Result;
}

FString ValueTypeName(const TSharedPtr<FUnrealMCPValue>& Value)
{
    if (!Value.IsValid()) return TEXT("null");
    switch (Value->Type)
    {
    case EUnrealMCPValueType::Null: return TEXT("null");
    case EUnrealMCPValueType::Boolean: return TEXT("boolean");
    case EUnrealMCPValueType::Number: return TEXT("number");
    case EUnrealMCPValueType::String: return TEXT("string");
    case EUnrealMCPValueType::Array: return TEXT("array");
    case EUnrealMCPValueType::Record: return TEXT("record");
    }
    return TEXT("unknown");
}

bool AddResult(
    const TSharedRef<FUnrealMCPRecord>& Result,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPError& OutError)
{
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Result->Values)
    {
        if (!Document.Add({Field.Key, ValueTypeName(Field.Value), Field.Value}, OutError)) return false;
    }
    return true;
}

bool RegisterRoutes(
    const TArray<FUnrealMCPAssetFamilySelectorRoute>& Routes,
    FUnrealMCPAssetFamilySelectorRouter& Selectors,
    FUnrealMCPError& OutError)
{
    for (const FUnrealMCPAssetFamilySelectorRoute& Route : Routes)
        if (!Selectors.Register(Route, OutError)) return false;
    return Selectors.Freeze(OutError);
}

void AddSelection(const FUnrealMCPAssetFamilyInspectionContext& Context, const TSharedRef<FUnrealMCPRecord>& Result)
{
    const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
    TArray<FString> Encoded;
    for (const FString& Segment : Context.Selector.Segments)
        Encoded.Add(StructuredDataInspection::EncodeSelectorSegment(Segment));
    Selection->SetStringField(TEXT("selector"), FString::Join(Encoded, TEXT("/")));
    Result->SetObjectField(TEXT("selection"), Selection);
}

TSharedRef<FUnrealMCPRecord> PageRecord(
    int32 PageIndex, int32 PageSize, int32 Total, int32 Returned, const FString& Snapshot)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("size"), PageSize);
    Result->SetNumberField(TEXT("index"), PageIndex);
    Result->SetNumberField(TEXT("count"), Total == 0 ? 0 : (Total + PageSize - 1) / PageSize);
    Result->SetNumberField(TEXT("returned"), Returned);
    Result->SetNumberField(TEXT("total_items"), Total);
    Result->SetBoolField(TEXT("has_previous"), PageIndex > 0 && Total > 0);
    Result->SetBoolField(TEXT("has_next"), static_cast<int64>(PageIndex + 1) * PageSize < Total);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    return Result;
}

bool RejectPartialGraph(const FUnrealMCPAssetFamilyInspectionContext& Context, FUnrealMCPError& OutError)
{
    if (!Context.bHasPartialGraphFlag) return false;
    OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
    return true;
}

FAssetData AssetData(UObject* Asset)
{
    return Asset != nullptr
        ? FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(Asset->GetPathName()))
        : FAssetData();
}

TArray<FAssetBundleEntry> SortedBundles(UObject* Asset)
{
    TArray<FAssetBundleEntry> Result;
    const FAssetData Data = AssetData(Asset);
    if (Data.TaggedAssetBundles.IsValid()) Result = Data.TaggedAssetBundles->Bundles;
    Result.Sort([](const FAssetBundleEntry& Left, const FAssetBundleEntry& Right)
        { return Left.BundleName.LexicalLess(Right.BundleName); });
    return Result;
}

FString DataAssetSnapshot(UObject* Asset)
{
    UDataAsset* DataAsset = Cast<UDataAsset>(Asset);
    if (DataAsset == nullptr) return FString();
    FUnrealMCPStructuredDataSource Source{DataAsset->GetClass(), DataAsset, DataAsset, true};
    TArray<FString> Lines;
    Lines.Add(StructuredDataInspection::BuildSnapshot(Source, DataAsset->GetPathName()));
    const FPrimaryAssetId PrimaryId = DataAsset->GetPrimaryAssetId();
    Lines.Add(TEXT("primary|") + PrimaryId.ToString());
    for (const FAssetBundleEntry& Bundle : SortedBundles(DataAsset))
    {
        TArray<FString> Paths;
        for (const FTopLevelAssetPath& Path : Bundle.AssetPaths) Paths.Add(Path.ToString());
        Paths.Sort();
        Lines.Add(TEXT("bundle|") + Bundle.BundleName.ToString() + TEXT("|") + FString::Join(Paths, TEXT(",")));
    }
    return StructuredDataInspection::BuildSnapshot(Source, FString::Join(Lines, TEXT("\n")));
}

class FDataAssetInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    static TArray<FUnrealMCPAssetFamilySelectorRoute> Routes()
    {
        return {
            {TEXT("data_asset_properties"), {TEXT("properties")}, true, false},
            {TEXT("primary_data_asset_bundles"), {TEXT("asset_bundles")}, true, false}};
    }

    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        UDataAsset* Asset = Cast<UDataAsset>(Context.Asset);
        if (Asset == nullptr)
        {
            OutError = {TEXT("unsupported_type"), TEXT("The Data Asset adapter requires a UDataAsset instance")};
            return false;
        }
        if (RejectPartialGraph(Context, OutError)) return false;
        const bool bPrimary = Asset->IsA<UPrimaryDataAsset>();
        const FString Type = bPrimary ? TEXT("primary_data_asset") : TEXT("data_asset");
        UClass* ParentClass = Asset->GetClass()->GetSuperClass();
        const TSharedRef<FUnrealMCPRecord> Result = BaseResult(
            Context, Type, Asset->GetClass()->GetPathName(), ParentClass != nullptr ? ParentClass->GetPathName() : FString());
        FUnrealMCPStructuredDataSource Source{Asset->GetClass(), Asset, Asset, true};

        if (Context.Selector.IsRoot())
        {
            if (Context.bHasPaging)
            {
                OutError = {TEXT("invalid_argument"), TEXT("Paging parameters require a pageable Data Asset selector")};
                return false;
            }
            const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
            Block->SetStringField(TEXT("value_source"), TEXT("asset_instance"));
            Block->SetStringField(TEXT("load_behavior"), TEXT("lazy_on_demand"));
            Result->SetObjectField(TEXT("data_asset"), Block);
            TSharedPtr<FUnrealMCPRecord> Properties;
            if (!StructuredDataInspection::BuildPropertyPage(
                Source, TEXT("properties"), 0, 10, Context.Identity.SnapshotId, Properties, OutError)) return false;
            Result->SetObjectField(TEXT("properties"), Properties.ToSharedRef());
            TArray<TSharedPtr<FUnrealMCPValue>> SelectorValues;
            SelectorValues.Add(MakeShared<FUnrealMCPValueString>(TEXT("properties")));
            if (bPrimary)
            {
                const FPrimaryAssetId Id = Asset->GetPrimaryAssetId();
                const TSharedRef<FUnrealMCPRecord> Primary = MakeShared<FUnrealMCPRecord>();
                const TSharedRef<FUnrealMCPRecord> IdRecord = MakeShared<FUnrealMCPRecord>();
                IdRecord->SetBoolField(TEXT("valid"), Id.IsValid());
                if (Id.IsValid())
                {
                    IdRecord->SetStringField(TEXT("type"), Id.PrimaryAssetType.ToString());
                    IdRecord->SetStringField(TEXT("name"), Id.PrimaryAssetName.ToString());
                }
                Primary->SetObjectField(TEXT("primary_asset_id"), IdRecord);
                const TArray<FAssetBundleEntry> Bundles = SortedBundles(Asset);
                const TSharedRef<FUnrealMCPRecord> BundleSummary = MakeShared<FUnrealMCPRecord>();
                BundleSummary->SetNumberField(TEXT("count"), Bundles.Num());
                BundleSummary->SetStringField(TEXT("selector"), TEXT("asset_bundles"));
                Primary->SetObjectField(TEXT("asset_bundles"), BundleSummary);
                Result->SetObjectField(TEXT("primary_data_asset"), Primary);
                SelectorValues.Add(MakeShared<FUnrealMCPValueString>(TEXT("asset_bundles")));
            }
            Result->SetArrayField(TEXT("selectors"), SelectorValues);
        }
        else if (Context.Selector.Segments[0] == TEXT("properties"))
        {
            AddSelection(Context, Result);
            if (Context.Selector.Segments.Num() == 1)
            {
                TSharedPtr<FUnrealMCPRecord> Properties;
                if (!StructuredDataInspection::BuildPropertyPage(Source, TEXT("properties"), Context.PageIndex,
                    Context.PageSize, Context.Identity.SnapshotId, Properties, OutError)) return false;
                Result->SetObjectField(TEXT("properties"), Properties.ToSharedRef());
            }
            else
            {
                TArray<FString> FieldSegments = Context.Selector.Segments;
                FieldSegments.RemoveAt(0);
                TSharedPtr<FUnrealMCPRecord> Inspection;
                if (!StructuredDataInspection::InspectField(Source, TEXT("properties"), FieldSegments, FString(),
                    Context.PageIndex, Context.PageSize, Context.bHasPaging, Context.Identity.SnapshotId, Inspection, OutError)) return false;
                for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Inspection->Values) Result->SetField(Field.Key, Field.Value);
            }
        }
        else if (Context.Selector.Segments[0] == TEXT("asset_bundles") && bPrimary)
        {
            AddSelection(Context, Result);
            const TArray<FAssetBundleEntry> Bundles = SortedBundles(Asset);
            if (Context.Selector.Segments.Num() == 1)
            {
                const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
                const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Bundles.Num()));
                const int32 End = FMath::Min(Start + Context.PageSize, Bundles.Num());
                TArray<TSharedPtr<FUnrealMCPValue>> Items;
                for (int32 Index = Start; Index < End; ++Index)
                {
                    const FAssetBundleEntry& Bundle = Bundles[Index];
                    const TSharedRef<FUnrealMCPRecord> Item = MakeShared<FUnrealMCPRecord>();
                    Item->SetStringField(TEXT("name"), Bundle.BundleName.ToString());
                    const TSharedRef<FUnrealMCPRecord> Assets = MakeShared<FUnrealMCPRecord>();
                    Assets->SetNumberField(TEXT("count"), Bundle.AssetPaths.Num());
                    Assets->SetStringField(TEXT("selector"), TEXT("asset_bundles/")
                        + StructuredDataInspection::EncodeSelectorSegment(Bundle.BundleName.ToString()) + TEXT("/assets"));
                    Item->SetObjectField(TEXT("assets"), Assets);
                    Items.Add(MakeShared<FUnrealMCPValueObject>(Item));
                }
                Result->SetArrayField(TEXT("asset_bundles"), Items);
                Result->SetObjectField(TEXT("page"), PageRecord(Context.PageIndex, Context.PageSize,
                    Bundles.Num(), Items.Num(), Context.Identity.SnapshotId));
            }
            else if (Context.Selector.Segments.Num() == 3 && Context.Selector.Segments[2] == TEXT("assets"))
            {
                const FAssetBundleEntry* Bundle = Bundles.FindByPredicate([&Context](const FAssetBundleEntry& Candidate)
                    { return Candidate.BundleName.ToString() == Context.Selector.Segments[1]; });
                if (Bundle == nullptr)
                {
                    OutError = {TEXT("not_found"), TEXT("The selected Primary Data Asset bundle was not found")};
                    return false;
                }
                TArray<FString> Paths;
                for (const FTopLevelAssetPath& Path : Bundle->AssetPaths) Paths.Add(Path.ToString());
                Paths.Sort();
                const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
                const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Paths.Num()));
                const int32 End = FMath::Min(Start + Context.PageSize, Paths.Num());
                TArray<TSharedPtr<FUnrealMCPValue>> Items;
                for (int32 Index = Start; Index < End; ++Index)
                {
                    const TSharedRef<FUnrealMCPRecord> Item = MakeShared<FUnrealMCPRecord>();
                    Item->SetNumberField(TEXT("index"), Index); Item->SetStringField(TEXT("value"), Paths[Index]);
                    Items.Add(MakeShared<FUnrealMCPValueObject>(Item));
                }
                Result->SetArrayField(TEXT("items"), Items);
                Result->SetObjectField(TEXT("page"), PageRecord(Context.PageIndex, Context.PageSize,
                    Paths.Num(), Items.Num(), Context.Identity.SnapshotId));
            }
            else
            {
                OutError = {TEXT("not_found"), TEXT("The selected Primary Data Asset bundle child was not found")};
                return false;
            }
        }
        else
        {
            OutError = {TEXT("not_found"), TEXT("The selected Data Asset child was not found")};
            return false;
        }
        return Snapshot.Add(TEXT("released_snapshot"), Context.Identity.SnapshotId, OutError)
            && RegisterRoutes(Routes(), Selectors, OutError)
            && AddResult(Result, Document, OutError);
    }
};

struct FTableInspection
{
    FString ObjectPath;
    TArray<TSharedPtr<FUnrealMCPValue>> Records;
    TArray<TSharedPtr<FUnrealMCPValue>> Schema;
    FString Snapshot;
    TSharedPtr<FUnrealMCPRecord> Metadata;
};

bool InspectTable(UDataTable* Table, FTableInspection& Out, FUnrealMCPError& OutError)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("target"), TEXT("data_table"));
    Arguments->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Arguments->SetNumberField(TEXT("page_size"), 1);
    FString Target; FString Package;
    return GameDataInspectionBuilder::Build(*Arguments, Target, Out.ObjectPath, Package,
        Out.Records, Out.Schema, Out.Snapshot, Out.Metadata, OutError);
}

FString DataTableSnapshot(UObject* Asset)
{
    UDataTable* Table = Cast<UDataTable>(Asset);
    if (Table == nullptr) return FString();
    FTableInspection Inspection; FUnrealMCPError Error;
    return InspectTable(Table, Inspection, Error) ? Inspection.Snapshot : FString();
}

TArray<FName> SortedRowNames(const UDataTable* Table)
{
    TArray<FName> Names;
    if (Table != nullptr) Table->GetRowMap().GenerateKeyArray(Names);
    Names.Sort(FNameLexicalLess());
    return Names;
}

FProperty* FindField(const UScriptStruct* Struct, const FString& Name)
{
    if (Struct == nullptr) return nullptr;
    for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
        if (Struct->GetAuthoredNameForField(*It) == Name || It->GetName() == Name) return *It;
    return nullptr;
}

class FDataTableInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    static TArray<FUnrealMCPAssetFamilySelectorRoute> Routes()
    {
        return {
            {TEXT("data_table_columns"), {TEXT("columns")}, true, false},
            {TEXT("data_table_rows"), {TEXT("rows")}, true, false},
            {TEXT("data_table_schema"), {TEXT("schema")}, false, false}};
    }

    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        UDataTable* Table = Cast<UDataTable>(Context.Asset);
        if (Table == nullptr || Table->GetRowStruct() == nullptr)
        {
            OutError = {TEXT("unsupported_type"), TEXT("The Data Table adapter requires a valid UDataTable")};
            return false;
        }
        if (RejectPartialGraph(Context, OutError)) return false;
        FTableInspection Inspection;
        if (!InspectTable(Table, Inspection, OutError)) return false;
        if (Inspection.Snapshot != Context.Identity.SnapshotId)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The Data Table changed before semantic inspection"), MakeShared<FUnrealMCPRecord>(), true};
            return false;
        }
        const UScriptStruct* RowStruct = Table->GetRowStruct();
        const TArray<FName> Names = SortedRowNames(Table);
        const bool bCustomTableClass = Table->GetClass() != UDataTable::StaticClass();
        const TSharedRef<FUnrealMCPRecord> Result = BaseResult(Context, TEXT("data_table"),
            bCustomTableClass ? Table->GetClass()->GetPathName() : FString(),
            bCustomTableClass && Table->GetClass()->GetSuperClass() != nullptr
                ? Table->GetClass()->GetSuperClass()->GetPathName() : FString());

        const auto SourceForRow = [Table, RowStruct](FName Name)
        {
            return FUnrealMCPStructuredDataSource{RowStruct, Table->FindRowUnchecked(Name), Table, false};
        };
        const auto BuildSchema = [&]() -> TSharedPtr<FUnrealMCPRecord>
        {
            const TSharedRef<FUnrealMCPRecord> Schema = MakeShared<FUnrealMCPRecord>();
            Schema->SetNumberField(TEXT("field_count"), Inspection.Schema.Num());
            Schema->SetArrayField(TEXT("fields"), Inspection.Schema);
            return Schema;
        };

        if (Context.Selector.IsRoot())
        {
            const TSharedRef<FUnrealMCPRecord> DataTable = MakeShared<FUnrealMCPRecord>();
            DataTable->SetStringField(TEXT("load_behavior"), TEXT("lazy_on_demand"));
            const TSharedRef<FUnrealMCPRecord> Struct = MakeShared<FUnrealMCPRecord>();
            Struct->SetStringField(TEXT("path"), RowStruct->GetPathName());
            Struct->SetStringField(TEXT("kind"), RowStruct->IsA<UUserDefinedStruct>() ? TEXT("user_defined") : TEXT("native"));
            DataTable->SetObjectField(TEXT("row_struct"), Struct);
            DataTable->SetNumberField(TEXT("row_count"), Names.Num());
            DataTable->SetStringField(TEXT("client_build"), Table->bStripFromClientBuilds ? TEXT("stripped") : TEXT("included"));
            const TSharedRef<FUnrealMCPRecord> Import = MakeShared<FUnrealMCPRecord>();
            if (Table->ImportKeyField.IsEmpty()) Import->SetField(TEXT("key_field"), MakeShared<FUnrealMCPValueNull>());
            else Import->SetStringField(TEXT("key_field"), Table->ImportKeyField);
            Import->SetBoolField(TEXT("ignore_extra_fields"), Table->bIgnoreExtraFields);
            Import->SetBoolField(TEXT("ignore_missing_fields"), Table->bIgnoreMissingFields);
            Import->SetBoolField(TEXT("preserve_existing_values"), Table->bPreserveExistingValues);
            DataTable->SetObjectField(TEXT("import"), Import);
            Result->SetObjectField(TEXT("data_table"), DataTable);
            const TSharedPtr<FUnrealMCPRecord> Schema = BuildSchema();
            if (!Schema.IsValid())
            {
                OutError = {TEXT("internal_error"), TEXT("The Data Table schema could not be inspected")}; return false;
            }
            Result->SetObjectField(TEXT("schema"), Schema.ToSharedRef());
            const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
            const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Names.Num()));
            const int32 End = FMath::Min(Start + Context.PageSize, Names.Num());
            TArray<TSharedPtr<FUnrealMCPValue>> Index;
            for (int32 RowIndex = Start; RowIndex < End; ++RowIndex)
            {
                const TSharedRef<FUnrealMCPRecord> Item = MakeShared<FUnrealMCPRecord>();
                Item->SetStringField(TEXT("name"), Names[RowIndex].ToString());
                Item->SetStringField(TEXT("selector"), TEXT("rows/")
                    + StructuredDataInspection::EncodeSelectorSegment(Names[RowIndex].ToString()));
                Index.Add(MakeShared<FUnrealMCPValueObject>(Item));
            }
            const TSharedRef<FUnrealMCPRecord> Rows = MakeShared<FUnrealMCPRecord>();
            Rows->SetNumberField(TEXT("count"), Names.Num()); Rows->SetArrayField(TEXT("index"), Index);
            Rows->SetObjectField(TEXT("page"), PageRecord(Context.PageIndex, Context.PageSize,
                Names.Num(), Index.Num(), Context.Identity.SnapshotId));
            Result->SetObjectField(TEXT("rows"), Rows);
            Result->SetArrayField(TEXT("selectors"), {
                MakeShared<FUnrealMCPValueString>(TEXT("schema")), MakeShared<FUnrealMCPValueString>(TEXT("rows")),
                MakeShared<FUnrealMCPValueString>(TEXT("columns"))});
        }
        else if (Context.Selector.Segments[0] == TEXT("schema") && Context.Selector.Segments.Num() == 1)
        {
            if (Context.bHasPaging)
            {
                OutError = {TEXT("invalid_argument"), TEXT("The bounded Data Table schema is not paged")}; return false;
            }
            AddSelection(Context, Result);
            const TSharedPtr<FUnrealMCPRecord> Schema = BuildSchema();
            if (!Schema.IsValid()) { OutError = {TEXT("internal_error"), TEXT("The Data Table schema could not be inspected")}; return false; }
            Result->SetObjectField(TEXT("schema"), Schema.ToSharedRef());
        }
        else if (Context.Selector.Segments[0] == TEXT("rows"))
        {
            AddSelection(Context, Result);
            if (Context.Selector.Segments.Num() == 1)
            {
                const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
                const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Names.Num()));
                const int32 End = FMath::Min(Start + Context.PageSize, Names.Num());
                TArray<TSharedPtr<FUnrealMCPValue>> Rows;
                for (int32 RowIndex = Start; RowIndex < End; ++RowIndex)
                {
                    TSharedPtr<FUnrealMCPRecord> Values;
                    const FString Prefix = TEXT("rows/") + StructuredDataInspection::EncodeSelectorSegment(Names[RowIndex].ToString());
                    if (!StructuredDataInspection::BuildFieldValues(SourceForRow(Names[RowIndex]), Prefix, Values, OutError)) return false;
                    const TSharedRef<FUnrealMCPRecord> Row = MakeShared<FUnrealMCPRecord>();
                    Row->SetStringField(TEXT("name"), Names[RowIndex].ToString()); Row->SetObjectField(TEXT("values"), Values.ToSharedRef());
                    Rows.Add(MakeShared<FUnrealMCPValueObject>(Row));
                }
                Result->SetArrayField(TEXT("rows"), Rows);
                Result->SetObjectField(TEXT("page"), PageRecord(Context.PageIndex, Context.PageSize,
                    Names.Num(), Rows.Num(), Context.Identity.SnapshotId));
            }
            else
            {
                const FName RowName(*Context.Selector.Segments[1]);
                if (!Names.Contains(RowName))
                {
                    OutError = {TEXT("not_found"), TEXT("The selected Data Table row was not found")}; return false;
                }
                if (Context.Selector.Segments.Num() == 2)
                {
                    if (Context.bHasPaging)
                    {
                        OutError = {TEXT("invalid_argument"), TEXT("An exact Data Table row is not paged")}; return false;
                    }
                    TSharedPtr<FUnrealMCPRecord> Values;
                    const FString Prefix = TEXT("rows/") + StructuredDataInspection::EncodeSelectorSegment(RowName.ToString());
                    if (!StructuredDataInspection::BuildFieldValues(SourceForRow(RowName), Prefix, Values, OutError)) return false;
                    const TSharedRef<FUnrealMCPRecord> Row = MakeShared<FUnrealMCPRecord>();
                    Row->SetStringField(TEXT("name"), RowName.ToString()); Row->SetObjectField(TEXT("values"), Values.ToSharedRef());
                    Result->SetObjectField(TEXT("row"), Row);
                }
                else
                {
                    TArray<FString> FieldSegments = Context.Selector.Segments;
                    FieldSegments.RemoveAt(0, 2);
                    TSharedPtr<FUnrealMCPRecord> Field;
                    const FString Prefix = TEXT("rows/") + StructuredDataInspection::EncodeSelectorSegment(RowName.ToString());
                    if (!StructuredDataInspection::InspectField(SourceForRow(RowName), Prefix, FieldSegments, FString(),
                        Context.PageIndex, Context.PageSize, Context.bHasPaging, Context.Identity.SnapshotId, Field, OutError)) return false;
                    Result->SetStringField(TEXT("row"), RowName.ToString());
                    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Pair : Field->Values) Result->SetField(Pair.Key, Pair.Value);
                }
            }
        }
        else if (Context.Selector.Segments[0] == TEXT("columns") && Context.Selector.Segments.Num() == 2)
        {
            AddSelection(Context, Result);
            FProperty* Property = FindField(RowStruct, Context.Selector.Segments[1]);
            if (Property == nullptr)
            {
                OutError = {TEXT("not_found"), TEXT("The selected Data Table column was not found")}; return false;
            }
            const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
            const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Names.Num()));
            const int32 End = FMath::Min(Start + Context.PageSize, Names.Num());
            TArray<TSharedPtr<FUnrealMCPValue>> Values;
            for (int32 RowIndex = Start; RowIndex < End; ++RowIndex)
            {
                TSharedPtr<FUnrealMCPRecord> RowValues;
                const FString Prefix = TEXT("rows/") + StructuredDataInspection::EncodeSelectorSegment(Names[RowIndex].ToString());
                if (!StructuredDataInspection::BuildFieldValues(SourceForRow(Names[RowIndex]), Prefix, RowValues, OutError)) return false;
                const TSharedRef<FUnrealMCPRecord> Item = MakeShared<FUnrealMCPRecord>();
                Item->SetStringField(TEXT("row"), Names[RowIndex].ToString());
                Item->SetField(TEXT("value"), RowValues->Values.FindRef(Context.Selector.Segments[1]));
                Values.Add(MakeShared<FUnrealMCPValueObject>(Item));
            }
            const TSharedRef<FUnrealMCPRecord> Column = MakeShared<FUnrealMCPRecord>();
            Column->SetStringField(TEXT("name"), Context.Selector.Segments[1]);
            Column->SetObjectField(TEXT("type"), GameDataValueCodec::EncodeType(Property));
            Result->SetObjectField(TEXT("column"), Column);
            Result->SetArrayField(TEXT("values"), Values);
            Result->SetObjectField(TEXT("page"), PageRecord(Context.PageIndex, Context.PageSize,
                Names.Num(), Values.Num(), Context.Identity.SnapshotId));
        }
        else
        {
            OutError = {TEXT("not_found"), TEXT("The selected Data Table child was not found")}; return false;
        }
        return Snapshot.Add(TEXT("released_snapshot"), Context.Identity.SnapshotId, OutError)
            && RegisterRoutes(Routes(), Selectors, OutError)
            && AddResult(Result, Document, OutError);
    }
};

FUnrealMCPAssetFamilyDescriptor Descriptor(
    const FString& FamilyId,
    UClass* NativeClass,
    int32 Priority,
    TArray<FUnrealMCPAssetFamilySelectorRoute> Routes,
    TSharedPtr<IUnrealMCPAssetFamilyInspectionAdapter> Adapter,
    TFunction<FString(UObject*)> SnapshotBuilder)
{
    FUnrealMCPAssetFamilyDescriptor Result;
    Result.FamilyId = FamilyId;
    Result.NativeClass = NativeClass;
    Result.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Result.Priority = Priority;
    Result.RequiredModules = {TEXT("UnrealMCPContent"), TEXT("UnrealMCPBlueprint")};
    Result.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Result.Bounds.MaxValueNodes = 65536;
    Result.Limits = {
        {TEXT("page_size"), UnrealMCP::MaxAssetInspectPageSize},
        {TEXT("selector_bytes"), UnrealMCP::MaxAssetInspectSelectorBytes},
        {TEXT("structured_fields"), UnrealMCP::MaxGameDataFields},
        {TEXT("structured_depth"), UnrealMCP::MaxGameDataDepth},
        {TEXT("structured_scan_items"), UnrealMCP::MaxGameDataRows}};
    Result.Capabilities.bInspection = true;
    Result.SelectorRoutes = MoveTemp(Routes);
    Result.InspectionAdapter = MoveTemp(Adapter);
    Result.SnapshotBuilder = MoveTemp(SnapshotBuilder);
    return Result;
}
}

bool UnrealMCP::DataInspection::RegisterAdapters(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError)
{
    using namespace Private;
    return Registry.Register(Descriptor(TEXT("data_asset"), UDataAsset::StaticClass(), 80,
        FDataAssetInspectionAdapter::Routes(), MakeShared<FDataAssetInspectionAdapter>(), DataAssetSnapshot), OutError)
        && Registry.Register(Descriptor(TEXT("data_table"), UDataTable::StaticClass(), 90,
            FDataTableInspectionAdapter::Routes(), MakeShared<FDataTableInspectionAdapter>(), DataTableSnapshot), OutError);
}
