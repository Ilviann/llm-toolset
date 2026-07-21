#pragma once

#include "UnrealMCPBlueprintActionCatalogSupport.h"

namespace UnrealMCP::BlueprintActionCatalogPrivate
{
struct FActionCatalogQuery
{
    FString AssetPath;
    FString GraphId;
    FString ExpectedSnapshot;
    FString Text;
    FString OwnerClass;
    FString Function;
    FString Member;
    FString Family;
    int32 Limit = UnrealMCP::DefaultActionResults;
};

static bool DecodeActionCatalogQuery(
    const TSharedPtr<FJsonObject>& Arguments,
    FActionCatalogQuery& Out,
    FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid() || !HasOnlyFields(*Arguments, {TEXT("asset_path"), TEXT("graph_id"), TEXT("expected_snapshot"),
        TEXT("text"), TEXT("owner_class"), TEXT("function"), TEXT("member"), TEXT("node_family"), TEXT("pin_context"), TEXT("limit")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Action catalog arguments have an invalid shape")};
        return false;
    }
    FString RawAssetPath;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAssetPath) || !NormalizeAssetPath(RawAssetPath, Out.AssetPath)
        || !Arguments->TryGetStringField(TEXT("graph_id"), Out.GraphId) || !IsGuidString(Out.GraphId, 32)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), Out.ExpectedSnapshot) || !IsGuidString(Out.ExpectedSnapshot, 40))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path, graph_id, or expected_snapshot is invalid")};
        return false;
    }
    if (!ReadOptionalString(*Arguments, TEXT("text"), 128, Out.Text, OutError)
        || !ReadOptionalString(*Arguments, TEXT("owner_class"), 512, Out.OwnerClass, OutError)
        || !ReadOptionalString(*Arguments, TEXT("function"), 128, Out.Function, OutError)
        || !ReadOptionalString(*Arguments, TEXT("member"), 128, Out.Member, OutError)
        || !ReadOptionalString(*Arguments, TEXT("node_family"), 32, Out.Family, OutError)) return false;
    if (!Out.OwnerClass.IsEmpty()
        && (!Out.OwnerClass.StartsWith(TEXT("/")) || Out.OwnerClass.Contains(TEXT("..")) || Out.OwnerClass.Contains(TEXT("\\"))))
    {
        OutError = {TEXT("invalid_argument"), TEXT("owner_class must be an exact class path")};
        return false;
    }
    const bool bFunctionFamily = Out.Family.IsEmpty() || Out.Family == TEXT("function_call")
        || Out.Family == TEXT("event") || Out.Family == TEXT("literal") || Out.Family == TEXT("operator");
    const bool bMemberFamily = Out.Family.IsEmpty() || Out.Family == TEXT("variable_get") || Out.Family == TEXT("variable_set");
    if ((!Out.Function.IsEmpty() && !Out.Member.IsEmpty())
        || (!Out.Function.IsEmpty() && !bFunctionFamily)
        || (!Out.Member.IsEmpty() && !bMemberFamily)
        || (!Out.Family.IsEmpty() && Out.Family != TEXT("function_call")
            && Out.Family != TEXT("variable_get") && Out.Family != TEXT("variable_set")
            && Out.Family != TEXT("event") && Out.Family != TEXT("flow_control")
            && Out.Family != TEXT("cast") && Out.Family != TEXT("literal")
            && Out.Family != TEXT("operator")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Function, member, and node-family filters conflict")};
        return false;
    }
    if (Arguments->HasField(TEXT("limit")))
    {
        double Number = 0.0;
        if (!Arguments->TryGetNumberField(TEXT("limit"), Number) || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
            || Number < 1 || Number > UnrealMCP::MaxActionResults)
        {
            OutError = {TEXT("invalid_argument"), TEXT("limit is outside the supported range")};
            return false;
        }
        Out.Limit = static_cast<int32>(Number);
    }
    return true;
}
}
