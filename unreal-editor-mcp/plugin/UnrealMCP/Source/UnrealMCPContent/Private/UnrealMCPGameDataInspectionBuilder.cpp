#include "UnrealMCPGameDataInspectionBuilder.h"

#include "UnrealMCPGameDataRequestValidation.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPVersion.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/DataTable.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPJsonCodec.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UObjectGlobals.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace UnrealMCP::GameDataInspectionBuilder
{
using namespace UnrealMCP::GameDataRequestValidation;

namespace
{
FString HashJson(const TSharedRef<FUnrealMCPRecord>& Value)
{
    FString Text;
    UnrealMCP::JsonCodec::Serialize(Value, Text);
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

FString Guid(const FGuid& Value)
{
    return Value.IsValid() ? Value.ToString(EGuidFormats::Digits).ToLower() : FString();
}

TSharedRef<FUnrealMCPRecord> SchemaRecord(const UScriptStruct* Struct, const FProperty* Property)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("name"), Struct->GetAuthoredNameForField(Property));
    Result->SetStringField(TEXT("property_name"), Property->GetName());
    const UStruct* DeclaringType = Property->GetOwnerStruct();
    Result->SetStringField(TEXT("declared_by"), DeclaringType != nullptr ? DeclaringType->GetPathName() : FString());
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
    const FUnrealMCPRecord& Arguments,
    FString& OutTarget,
    FString& OutObjectPath,
    FString& OutPackage,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutSchema,
    FString& OutSnapshot,
    TSharedPtr<FUnrealMCPRecord>& OutMetadata,
    FUnrealMCPError& OutError)
{
    FString InputPath;
    if (!Arguments.TryGetStringField(TEXT("target"), OutTarget)
        || (OutTarget != TEXT("user_defined_struct") && OutTarget != TEXT("data_table"))
        || !Arguments.TryGetStringField(TEXT("asset_path"), InputPath)
        || !NormalizeAssetPath(InputPath, OutObjectPath, OutPackage))
    {
        OutError = {TEXT("invalid_argument"), TEXT("target and asset_path must identify one supported game-data asset")};
        return false;
    }
    if ((OutTarget == TEXT("user_defined_struct")
            && !HasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("page_size")}))
        || (OutTarget == TEXT("data_table")
            && !HasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("row_names"), TEXT("page_size")})))
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
    OutMetadata = MakeShared<FUnrealMCPRecord>();
    TArray<TSharedPtr<FUnrealMCPValue>> FingerprintRecords;
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
            const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
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
            const TSharedPtr<FUnrealMCPValue> Value = MakeShared<FUnrealMCPValueObject>(Record);
            OutRecords.Add(Value);
            FingerprintRecords.Add(Value);
        }
        TArray<FString> Dependencies;
        bool bTruncated = false;
        GatherDependencies(OutPackage, Dependencies, bTruncated);
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        for (const FString& Item : Dependencies) Values.Add(MakeShared<FUnrealMCPValueString>(Item));
        OutMetadata->SetArrayField(TEXT("dependencies"), Values);
        OutMetadata->SetBoolField(TEXT("dependencies_truncated"), bTruncated);
    }
    else
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
            OutSchema.Add(MakeShared<FUnrealMCPValueObject>(SchemaRecord(RowStruct, *It)));
        }
        TSet<FName> Filter;
        if (Arguments.HasField(TEXT("row_names")))
        {
            const TArray<TSharedPtr<FUnrealMCPValue>>* Names = nullptr;
            if (!Arguments.TryGetArrayField(TEXT("row_names"), Names) || Names == nullptr
                || Names->IsEmpty() || Names->Num() > UnrealMCP::MaxGameDataBatchRows)
            {
                OutError = {TEXT("invalid_argument"), TEXT("row_names must be one bounded non-empty array")};
                return false;
            }
            for (const TSharedPtr<FUnrealMCPValue>& Value : *Names)
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
            const TSharedRef<FUnrealMCPRecord> Values = UnrealMCP::GameDataValueCodec::EncodeFields(
                RowStruct, Table->FindRowUnchecked(Name), 0, ValueError, bEncoded);
            if (!bEncoded)
            {
                OutError = ValueError;
                return false;
            }
            const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
            Record->SetStringField(TEXT("kind"), TEXT("row"));
            Record->SetStringField(TEXT("name"), Name.ToString());
            Record->SetObjectField(TEXT("values"), Values);
            const TSharedPtr<FUnrealMCPValue> Value = MakeShared<FUnrealMCPValueObject>(Record);
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
    const TSharedRef<FUnrealMCPRecord> Fingerprint = MakeShared<FUnrealMCPRecord>();
    Fingerprint->SetStringField(TEXT("target"), OutTarget);
    Fingerprint->SetStringField(TEXT("asset_path"), OutObjectPath);
    Fingerprint->SetObjectField(TEXT("metadata"), OutMetadata);
    Fingerprint->SetArrayField(TEXT("schema"), OutSchema);
    Fingerprint->SetArrayField(TEXT("records"), FingerprintRecords);
    OutSnapshot = HashJson(Fingerprint);
    return true;
}

TSharedRef<FUnrealMCPRecord> BuildEditResult(const FString& Target, const FString& ObjectPath, const FString& Snapshot)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("target"), Target);
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("saved"), true);
    Result->SetBoolField(TEXT("dirty"), false);
    return Result;
}
}
