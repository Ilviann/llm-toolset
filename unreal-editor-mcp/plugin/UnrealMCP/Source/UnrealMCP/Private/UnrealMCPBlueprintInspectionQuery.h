#pragma once

#include "UnrealMCPBlueprintInspectionSupport.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{
struct FInspectionQuery
{
    FString AssetPath;
    bool bIncludeInherited = false;
    TSet<FString> Sections;
    FString GraphFilter;
    FString ComponentFilter;
    FString MemberFilter;
    FString FunctionFilter;
    FString LocalFilter;
    FString MacroFilter;
    FString CustomEventFilter;
    TSet<FString> PropertyNames;
};

static bool DecodeInspectionQuery(const FJsonObject& Arguments, FInspectionQuery& Out, FUnrealMCPError& OutError)
{
    if (!HasOnlyFields(Arguments, {TEXT("mode"), TEXT("asset_path"), TEXT("sections"), TEXT("graph_id"), TEXT("component_id"), TEXT("member_id"),
        TEXT("function_id"), TEXT("local_id"), TEXT("macro_id"), TEXT("custom_event_id"),
        TEXT("property_names"), TEXT("include_inherited"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Inspection arguments contain an unknown field")};
        return false;
    }
    FString RawAssetPath;
    if (!Arguments.TryGetStringField(TEXT("asset_path"), RawAssetPath) || !NormalizeAssetPath(RawAssetPath, Out.AssetPath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must identify one valid mounted-content asset")};
        return false;
    }
    if (!ReadOptionalBool(Arguments, TEXT("include_inherited"), false, Out.bIncludeInherited, OutError)) return false;
    Out.Sections = {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
        TEXT("variables"), TEXT("functions"), TEXT("macros"), TEXT("custom_events"), TEXT("local_variables"), TEXT("graphs")};
    if (Arguments.HasField(TEXT("sections")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Arguments.TryGetArrayField(TEXT("sections"), Values) || Values == nullptr || Values->IsEmpty() || Values->Num() > 15)
        {
            OutError = {TEXT("invalid_argument"), TEXT("sections must be a non-empty bounded array")};
            return false;
        }
        Out.Sections.Reset();
        for (const TSharedPtr<FJsonValue>& Item : *Values)
        {
            FString Section;
            if (!Item.IsValid() || !Item->TryGetString(Section) || !InspectSections.Contains(Section) || Out.Sections.Contains(Section))
            {
                OutError = {TEXT("invalid_argument"), TEXT("sections contains an invalid or duplicate value")};
                return false;
            }
            Out.Sections.Add(Section);
        }
    }
    struct FIdentityField { const TCHAR* Field; FString* Value; const TCHAR* Message; };
    const FIdentityField IdentityFields[] = {
        {TEXT("graph_id"), &Out.GraphFilter, TEXT("graph_id must be a 32-character graph identity")},
        {TEXT("component_id"), &Out.ComponentFilter, TEXT("component_id must be a 32-character stable component identity")},
        {TEXT("member_id"), &Out.MemberFilter, TEXT("member_id must be a 32-character stable member identity")},
        {TEXT("function_id"), &Out.FunctionFilter, TEXT("function_id must be a 32-character stable function identity")},
        {TEXT("local_id"), &Out.LocalFilter, TEXT("local_id must be a 32-character stable local-variable identity")},
        {TEXT("macro_id"), &Out.MacroFilter, TEXT("macro_id must be a 32-character stable macro identity")},
        {TEXT("custom_event_id"), &Out.CustomEventFilter, TEXT("custom_event_id must be a 32-character stable custom-event identity")}};
    for (const FIdentityField& Identity : IdentityFields)
    {
        if (Arguments.HasField(Identity.Field)
            && (!Arguments.TryGetStringField(Identity.Field, *Identity.Value) || Identity.Value->Len() != 32))
        {
            OutError = {TEXT("invalid_argument"), Identity.Message};
            return false;
        }
    }
    if (!ReadPropertyNames(Arguments, Out.PropertyNames, OutError)) return false;
    if (Out.Sections.Contains(TEXT("class_defaults")) && Out.PropertyNames.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("class_defaults inspection requires one or more targeted property_names")};
        return false;
    }
    return true;
}
}
