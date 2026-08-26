#include "UnrealMCPGameDataInspectionBuilder.h"

#include "UnrealMCPGameDataRequestValidation.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPVersion.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UObjectGlobals.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace UnrealMCP::GameDataInspectionBuilder
{
using namespace UnrealMCP::GameDataRequestValidation;

namespace
{
FString HashJson(const TSharedRef<FJsonObject>& Value)
{
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Value, Writer);
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

FString Guid(const FGuid& Value)
{
    return Value.IsValid() ? Value.ToString(EGuidFormats::Digits).ToLower() : FString();
}

TSharedRef<FJsonObject> SchemaRecord(const UStruct* Struct, const FProperty* Property)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Struct->GetAuthoredNameForField(Property));
    Result->SetStringField(TEXT("property_name"), Property->GetName());
    Result->SetObjectField(TEXT("type"), UnrealMCP::GameDataValueCodec::EncodeType(Property));
    return Result;
}
}

bool GatherDependencies(const FString& PackageName, TArray<FString>& Out, bool& bTruncated)
{
    TArray<FName> Referencers;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetReferencers(
        FName(*PackageName), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
    Referencers.Sort(FNameLexicalLess());
    bTruncated = Referencers.Num() > UnrealMCP::MaxGameDataDependencies;
    for (int32 Index = 0; Index < FMath::Min(Referencers.Num(), UnrealMCP::MaxGameDataDependencies); ++Index)
        Out.Add(Referencers[Index].ToString());
    return true;
}

bool Build(
    const FJsonObject& Arguments,
    FString& OutTarget,
    FString& OutObjectPath,
    FString& OutPackage,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    TArray<TSharedPtr<FJsonValue>>& OutSchema,
    FString& OutSnapshot,
    TSharedPtr<FJsonObject>& OutMetadata,
    FUnrealMCPError& OutError)
{
    FString InputPath;
    if (!Arguments.TryGetStringField(TEXT("target"), OutTarget)
        || (OutTarget != TEXT("user_defined_struct") && OutTarget != TEXT("data_table") && OutTarget != TEXT("data_asset"))
        || !Arguments.TryGetStringField(TEXT("asset_path"), InputPath)
        || !NormalizeAssetPath(InputPath, OutObjectPath, OutPackage))
    {
        OutError = {TEXT("invalid_argument"), TEXT("target and asset_path must identify one supported game-data asset")};
        return false;
    }
    if ((OutTarget == TEXT("user_defined_struct")
            && !HasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("page_size")}))
        || (OutTarget == TEXT("data_table")
            && !HasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("row_names"), TEXT("page_size")}))
        || (OutTarget == TEXT("data_asset")
            && !HasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("property_names"), TEXT("page_size")})))
    {
        OutError = {TEXT("invalid_argument"), TEXT("game_data_inspect accepts only fields for its exact target")};
        return false;
    }
    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *OutObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (Asset == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The game-data asset was not found")};
        return false;
    }
    OutMetadata = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> FingerprintRecords;
    if (OutTarget == TEXT("user_defined_struct"))
    {
        UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset);
        if (Struct == nullptr)
        {
            OutError = {TEXT("wrong_type"), TEXT("The asset is not a user-defined struct")};
            return false;
        }
        OutMetadata->SetStringField(TEXT("compile_state"), Struct->Status == UDSS_UpToDate
            ? TEXT("up_to_date") : Struct->Status == UDSS_Error ? TEXT("error") : TEXT("dirty"));
        const TArray<FStructVariableDescription>& Members = FStructureEditorUtils::GetVarDesc(Struct);
        if (Members.Num() > UnrealMCP::MaxGameDataFields)
        {
            OutError = {TEXT("data_limit_exceeded"), TEXT("The struct exceeds the member limit")};
            return false;
        }
        for (int32 Index = 0; Index < Members.Num(); ++Index)
        {
            const FStructVariableDescription& Member = Members[Index];
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("kind"), TEXT("member"));
            Record->SetStringField(TEXT("id"), Guid(Member.VarGuid));
            Record->SetBoolField(TEXT("identity_stable"), Member.VarGuid.IsValid());
            Record->SetStringField(TEXT("name"), Member.FriendlyName);
            Record->SetStringField(TEXT("property_name"), Member.VarName.ToString());
            Record->SetNumberField(TEXT("order"), Index);
            const FEdGraphPinType PinType = Member.ToPinType();
            Record->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(PinType));
            Record->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(PinType, Member.DefaultValue));
            Record->SetStringField(TEXT("tooltip"), Member.ToolTip.Left(512));
            const TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Record);
            OutRecords.Add(Value);
            FingerprintRecords.Add(Value);
        }
        TArray<FString> Dependencies;
        bool bTruncated = false;
        GatherDependencies(OutPackage, Dependencies, bTruncated);
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& Item : Dependencies) Values.Add(MakeShared<FJsonValueString>(Item));
        OutMetadata->SetArrayField(TEXT("dependencies"), Values);
        OutMetadata->SetBoolField(TEXT("dependencies_truncated"), bTruncated);
    }
    else if (OutTarget == TEXT("data_table"))
    {
        UDataTable* Table = Cast<UDataTable>(Asset);
        if (Table == nullptr || Table->GetRowStruct() == nullptr)
        {
            OutError = {TEXT("wrong_type"), TEXT("The asset is not a valid Data Table")};
            return false;
        }
        const UScriptStruct* RowStruct = Table->GetRowStruct();
        OutMetadata->SetStringField(TEXT("row_struct"), RowStruct->GetPathName());
        OutMetadata->SetStringField(TEXT("row_struct_kind"), RowStruct->IsA<UUserDefinedStruct>() ? TEXT("user_defined") : TEXT("native"));
        int32 Fields = 0;
        for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
        {
            if (++Fields > UnrealMCP::MaxGameDataFields)
            {
                OutError = {TEXT("data_limit_exceeded"), TEXT("The row schema exceeds the field limit")};
                return false;
            }
            OutSchema.Add(MakeShared<FJsonValueObject>(SchemaRecord(RowStruct, *It)));
        }
        TSet<FName> Filter;
        if (Arguments.HasField(TEXT("row_names")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
            if (!Arguments.TryGetArrayField(TEXT("row_names"), Names) || Names == nullptr
                || Names->IsEmpty() || Names->Num() > UnrealMCP::MaxGameDataBatchRows)
            {
                OutError = {TEXT("invalid_argument"), TEXT("row_names must be one bounded non-empty array")};
                return false;
            }
            for (const TSharedPtr<FJsonValue>& Value : *Names)
            {
                FString Name;
                if (!Value->TryGetString(Name) || !ValidName(Name) || Filter.Contains(FName(*Name)))
                {
                    OutError = {TEXT("invalid_argument"), TEXT("row_names contains an invalid, duplicate, or case-conflicting name")};
                    return false;
                }
                Filter.Add(FName(*Name));
            }
        }
        if (Table->GetRowMap().Num() > UnrealMCP::MaxGameDataRows)
        {
            OutError = {TEXT("data_limit_exceeded"), TEXT("The Data Table exceeds the row scan limit")};
            return false;
        }
        TArray<FName> Names;
        Table->GetRowMap().GenerateKeyArray(Names);
        Names.Sort(FNameLexicalLess());
        for (const FName Name : Names)
        {
            bool bEncoded = false;
            FUnrealMCPError ValueError;
            const TSharedRef<FJsonObject> Values = UnrealMCP::GameDataValueCodec::EncodeFields(
                RowStruct, Table->FindRowUnchecked(Name), 0, ValueError, bEncoded);
            if (!bEncoded)
            {
                OutError = ValueError;
                return false;
            }
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("kind"), TEXT("row"));
            Record->SetStringField(TEXT("name"), Name.ToString());
            Record->SetObjectField(TEXT("values"), Values);
            const TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Record);
            FingerprintRecords.Add(Value);
            if (Filter.IsEmpty() || Filter.Contains(Name)) OutRecords.Add(Value);
        }
        if (!Filter.IsEmpty() && OutRecords.Num() != Filter.Num())
        {
            OutError = {TEXT("invalid_row"), TEXT("One or more requested rows do not exist")};
            return false;
        }
        OutMetadata->SetNumberField(TEXT("row_count"), Table->GetRowMap().Num());
    }
    else
    {
        UDataAsset* DataAsset = Cast<UDataAsset>(Asset);
        if (DataAsset == nullptr)
        {
            OutError = {TEXT("wrong_type"), TEXT("The asset is not a Data Asset")};
            return false;
        }
        TSet<FString> Filter;
        if (Arguments.HasField(TEXT("property_names")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
            if (!Arguments.TryGetArrayField(TEXT("property_names"), Names) || Names == nullptr
                || Names->IsEmpty() || Names->Num() > UnrealMCP::MaxGameDataFields)
            {
                OutError = {TEXT("invalid_argument"), TEXT("property_names must be one bounded non-empty array")};
                return false;
            }
            for (const TSharedPtr<FJsonValue>& Value : *Names)
            {
                FString Name;
                if (!Value->TryGetString(Name) || !ValidName(Name) || Filter.Contains(Name))
                {
                    OutError = {TEXT("invalid_argument"), TEXT("property_names contains an invalid or duplicate exact name")};
                    return false;
                }
                Filter.Add(Name);
            }
        }
        OutMetadata->SetStringField(TEXT("class_path"), DataAsset->GetClass()->GetPathName());
        OutMetadata->SetBoolField(TEXT("primary_data_asset"), DataAsset->IsA<UPrimaryDataAsset>());
        int32 FieldCount = 0;
        int32 UnsupportedCount = 0;
        TSet<FString> Found;
        for (TFieldIterator<FProperty> It(DataAsset->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit)
                || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) continue;
            if (++FieldCount > UnrealMCP::MaxGameDataFields)
            {
                OutError = {TEXT("data_limit_exceeded"), TEXT("The Data Asset exceeds the reflected property limit")};
                return false;
            }
            const FString AuthoredName = DataAsset->GetClass()->GetAuthoredNameForField(Property);
            const bool bSelected = Filter.IsEmpty() || Filter.Contains(Property->GetName()) || Filter.Contains(AuthoredName);
            if (bSelected)
            {
                Found.Add(Filter.Contains(Property->GetName()) ? Property->GetName() : AuthoredName);
            }
            OutSchema.Add(MakeShared<FJsonValueObject>(SchemaRecord(DataAsset->GetClass(), Property)));
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("kind"), TEXT("property"));
            Record->SetStringField(TEXT("name"), AuthoredName);
            Record->SetStringField(TEXT("property_name"), Property->GetName());
            Record->SetObjectField(TEXT("type"), UnrealMCP::GameDataValueCodec::EncodeType(Property));
            TSharedPtr<FJsonValue> Encoded;
            FUnrealMCPError ValueError;
            const bool bSupported = UnrealMCP::GameDataValueCodec::Encode(
                Property, Property->ContainerPtrToValuePtr<void>(DataAsset), 0, Encoded, ValueError);
            Record->SetBoolField(TEXT("supported"), bSupported);
            if (bSupported) Record->SetField(TEXT("value"), Encoded);
            else
            {
                ++UnsupportedCount;
                Record->SetStringField(TEXT("error"), ValueError.Code.IsEmpty() ? TEXT("unsupported_type") : ValueError.Code);
                Record->SetStringField(TEXT("message"), ValueError.Message.Left(256));
            }
            const TSharedPtr<FJsonValue> RecordValue = MakeShared<FJsonValueObject>(Record);
            FingerprintRecords.Add(RecordValue);
            if (bSelected) OutRecords.Add(RecordValue);
        }
        if (!Filter.IsEmpty() && Found.Num() != Filter.Num())
        {
            OutError = {TEXT("not_found"), TEXT("One or more requested Data Asset properties do not exist")};
            return false;
        }
        OutMetadata->SetNumberField(TEXT("property_count"), FieldCount);
        OutMetadata->SetNumberField(TEXT("unsupported_property_count"), UnsupportedCount);
    }
    const TSharedRef<FJsonObject> Fingerprint = MakeShared<FJsonObject>();
    Fingerprint->SetStringField(TEXT("target"), OutTarget);
    Fingerprint->SetStringField(TEXT("asset_path"), OutObjectPath);
    Fingerprint->SetObjectField(TEXT("metadata"), OutMetadata);
    Fingerprint->SetArrayField(TEXT("schema"), OutSchema);
    Fingerprint->SetArrayField(TEXT("records"), FingerprintRecords);
    OutSnapshot = HashJson(Fingerprint);
    return true;
}

TSharedRef<FJsonObject> BuildEditResult(const FString& Target, const FString& ObjectPath, const FString& Snapshot)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("target"), Target);
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("saved"), true);
    Result->SetBoolField(TEXT("dirty"), false);
    return Result;
}
}
