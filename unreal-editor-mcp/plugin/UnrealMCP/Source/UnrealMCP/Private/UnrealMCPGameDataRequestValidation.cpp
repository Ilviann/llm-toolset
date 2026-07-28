#include "UnrealMCPGameDataRequestValidation.h"

#include "Misc/PackageName.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::GameDataRequestValidation
{
bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
        if (!Names.Contains(Pair.Key)) return false;
    return true;
}

bool ValidateEditShape(const FJsonObject& Arguments, const FString& Target, const FString& Operation, FUnrealMCPError& OutError)
{
    bool bValid = false;
    if (Target == TEXT("user_defined_struct"))
    {
        if (Operation == TEXT("create"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("members")});
        else if (Operation == TEXT("add_member"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member")});
        else if (Operation == TEXT("rename_member"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("new_name")});
        else if (Operation == TEXT("reorder_member"))
        {
            FString Position;
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("relative_to_member_id"), TEXT("position")})
                && Arguments.TryGetStringField(TEXT("position"), Position)
                && (Position == TEXT("above") || Position == TEXT("below"));
        }
        else if (Operation == TEXT("remove_member"))
        {
            FString Policy;
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("policy")})
                && Arguments.TryGetStringField(TEXT("policy"), Policy) && Policy == TEXT("reject_if_referenced");
        }
        else if (Operation == TEXT("update_member"))
        {
            FString Field;
            FString Policy;
            bValid = Arguments.TryGetStringField(TEXT("field"), Field)
                && (Field == TEXT("type")
                    ? HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("field"), TEXT("type"), TEXT("policy")})
                        && Arguments.TryGetStringField(TEXT("policy"), Policy) && Policy == TEXT("reject_if_referenced")
                    : Field == TEXT("default") && HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("field"), TEXT("default")}));
        }
    }
    else if (Target == TEXT("data_table"))
    {
        if (Operation == TEXT("create"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("row_struct"), TEXT("rows")});
        else if (Operation == TEXT("add_row"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name"), TEXT("values")});
        else if (Operation == TEXT("replace_row"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name"), TEXT("values"), TEXT("preserve_unspecified")});
        else if (Operation == TEXT("rename_row"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name"), TEXT("new_row_name")});
        else if (Operation == TEXT("remove_row"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name")});
        else if (Operation == TEXT("batch"))
            bValid = HasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("upserts"), TEXT("remove_rows")});
    }
    if (!bValid) OutError = {TEXT("invalid_argument"), TEXT("game_data_edit accepts only its exact target and operation fields")};
    return bValid;
}

FString ObjectPathForPackage(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

bool NormalizePackagePath(const FString& Input, FString& OutPackage)
{
    OutPackage = Input;
    return Input.StartsWith(TEXT("/")) && !Input.StartsWith(TEXT("//")) && !Input.EndsWith(TEXT("/"))
        && !Input.Contains(TEXT("..")) && !Input.Contains(TEXT("\\")) && !Input.Contains(TEXT("."))
        && Input.Len() <= 512 && FPackageName::IsValidLongPackageName(Input, true)
        && !FPackageName::GetLongPackageAssetName(Input).IsEmpty();
}

bool NormalizeAssetPath(const FString& Input, FString& OutObject, FString& OutPackage)
{
    if (!Input.StartsWith(TEXT("/")) || Input.StartsWith(TEXT("//")) || Input.Contains(TEXT(".."))
        || Input.Contains(TEXT("\\")) || Input.Len() > 512) return false;
    OutPackage = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(OutPackage, true)) return false;
    OutObject = Input.Contains(TEXT(".")) ? Input : ObjectPathForPackage(OutPackage);
    return FPackageName::IsValidObjectPath(OutObject)
        && FPackageName::ObjectPathToObjectName(OutObject) == FPackageName::GetLongPackageAssetName(OutPackage);
}

bool ReadPageSize(const FJsonObject& Arguments, int32& Out, FUnrealMCPError& OutError)
{
    Out = UnrealMCP::DefaultInspectPageSize;
    if (Arguments.HasField(TEXT("page_size")))
    {
        double Value = 0.0;
        if (!Arguments.TryGetNumberField(TEXT("page_size"), Value) || Value != FMath::FloorToDouble(Value)
            || Value < 1 || Value > UnrealMCP::MaxInspectPageSize)
        {
            OutError = {TEXT("invalid_argument"), TEXT("page_size must be a bounded integer")};
            return false;
        }
        Out = static_cast<int32>(Value);
    }
    return true;
}

bool ParseGuidField(const FJsonObject& Arguments, const TCHAR* Name, FGuid& Out, FUnrealMCPError& OutError)
{
    FString Text;
    if (!Arguments.TryGetStringField(Name, Text) || !FGuid::ParseExact(Text, EGuidFormats::Digits, Out))
    {
        OutError = {TEXT("invalid_argument"), FString(Name) + TEXT(" must be one stable 32-character identity")};
        return false;
    }
    return true;
}

bool ValidName(const FString& Value)
{
    return !Value.IsEmpty() && Value.Len() <= 128 && FName::IsValidXName(Value, INVALID_NAME_CHARACTERS)
        && !Value.Equals(TEXT("None"), ESearchCase::IgnoreCase);
}
}
