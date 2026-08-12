#include "UnrealMCPWidgetLayoutService.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBoxSlot.h"
#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPWidgetAuthoringSupport.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "UnrealMCPPropertyCodec.h"

namespace
{
void AddLayoutProperties(
    TArray<FString>& Out,
    std::initializer_list<const TCHAR*> Names)
{
    for (const TCHAR* Name : Names)
    {
        Out.Add(Name);
    }
}
}

FUnrealMCPWidgetLayoutService::FUnrealMCPWidgetLayoutService(
    FUnrealMCPBlueprintInspector& InInspector)
    : Inspector(InInspector)
{
}

TArray<FString> FUnrealMCPWidgetLayoutService::SupportedProperties(
    const UPanelSlot* Slot)
{
    TArray<FString> Result;
    if (Slot == nullptr)
    {
        return Result;
    }
    if (Slot->IsA<UCanvasPanelSlot>())
    {
        AddLayoutProperties(Result, {TEXT("LayoutData"), TEXT("bAutoSize"), TEXT("ZOrder")});
    }
    else if (Slot->IsA<UGridSlot>())
    {
        AddLayoutProperties(Result, {
            TEXT("Padding"), TEXT("HorizontalAlignment"),
            TEXT("VerticalAlignment"), TEXT("Row"), TEXT("RowSpan"),
            TEXT("Column"), TEXT("ColumnSpan"), TEXT("Layer"), TEXT("Nudge")});
    }
    else if (Slot->IsA<UUniformGridSlot>())
    {
        AddLayoutProperties(Result, {
            TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"),
            TEXT("Row"), TEXT("Column")});
    }
    else if (Slot->IsA<UHorizontalBoxSlot>()
        || Slot->IsA<UVerticalBoxSlot>()
        || Slot->IsA<UScrollBoxSlot>())
    {
        AddLayoutProperties(Result, {
            TEXT("Size"), TEXT("Padding"), TEXT("HorizontalAlignment"),
            TEXT("VerticalAlignment")});
    }
    else if (Slot->IsA<UOverlaySlot>() || Slot->IsA<USizeBoxSlot>())
    {
        AddLayoutProperties(Result, {
            TEXT("Padding"), TEXT("HorizontalAlignment"),
            TEXT("VerticalAlignment")});
    }
    else if (Slot->IsA<UWrapBoxSlot>())
    {
        AddLayoutProperties(Result, {
            TEXT("Padding"), TEXT("HorizontalAlignment"),
            TEXT("VerticalAlignment"), TEXT("bFillEmptySpace"),
            TEXT("FillSpanWhenLessThan"), TEXT("bForceNewLine")});
    }
    Result.RemoveAll([Slot](const FString& Name)
    {
        return Slot->GetClass()->FindPropertyByName(FName(*Name)) == nullptr;
    });
    Result.Sort();
    return Result;
}

TSharedRef<FUnrealMCPRecord> FUnrealMCPWidgetLayoutService::Encode(
    const UPanelSlot* Slot)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    if (Slot == nullptr)
    {
        return Result;
    }
    for (const FString& Name : SupportedProperties(Slot))
    {
        FProperty* Property =
            Slot->GetClass()->FindPropertyByName(FName(*Name));
        const TSharedRef<FUnrealMCPRecord> Encoded =
            UnrealMCP::WidgetAuthoring::EncodeProperty(
                const_cast<UPanelSlot*>(Slot), Property);
        if (Encoded->GetBoolField(TEXT("supported")))
        {
            const TSharedPtr<FUnrealMCPValue> EncodedValue =
                Encoded->Values.FindRef(TEXT("value"));
            if (EncodedValue.IsValid())
            {
                Result->SetField(Name, EncodedValue);
            }
        }
    }
    return Result;
}

FString FUnrealMCPWidgetLayoutService::Fingerprint(const UPanelSlot* Slot)
{
    if (Slot == nullptr)
    {
        return FString();
    }
    TArray<FString> Parts;
    for (const FString& Name : SupportedProperties(Slot))
    {
        FProperty* Property =
            Slot->GetClass()->FindPropertyByName(FName(*Name));
        FString Value;
        if (Property != nullptr)
        {
            UnrealMCP::PropertyCodec::ExportValueText(Slot, Property, Value);
        }
        Parts.Add(Name + TEXT("=") + Value);
    }
    return FString::Join(Parts, TEXT(";"));
}

bool FUnrealMCPWidgetLayoutService::Execute(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    using namespace UnrealMCP::WidgetAuthoring;
    using UnrealMCP::WidgetTreePrivate::WidgetId;

    check(IsInGameThread());
    if (!Arguments.IsValid()
        || !HasOnlyAuthoringFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"),
            TEXT("expected_snapshot"), TEXT("operation"), TEXT("slot_id"),
            TEXT("property_name"), TEXT("value")}))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("set_slot accepts only one slot property mutation")};
        return false;
    }
    FString Operation;
    FString SlotId;
    FString PropertyName;
    const TSharedPtr<FUnrealMCPValue>* Value = Arguments->Values.Find(TEXT("value"));
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation)
        || Operation != TEXT("set_slot")
        || !Arguments->TryGetStringField(TEXT("slot_id"), SlotId)
        || SlotId.Len() != 32
        || !Arguments->TryGetStringField(TEXT("property_name"), PropertyName)
        || PropertyName.IsEmpty() || PropertyName.Len() > 128
        || Value == nullptr || !Value->IsValid())
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("set_slot requires one stable slot and bounded property value")};
        return false;
    }

    UWidgetBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    if (!ResolveBlueprint(
            Inspector, *Arguments, Blueprint, ObjectPath, OutError))
    {
        return false;
    }
    UPanelSlot* Slot = FindPanelSlot(Blueprint, SlotId);
    if (Slot == nullptr)
    {
        OutError = {
            TEXT("not_found"),
            TEXT("The requested live panel-slot identity was not found")};
        return false;
    }
    if (!SupportedProperties(Slot).Contains(PropertyName))
    {
        OutError = {
            TEXT("unsupported_slot"),
            TEXT("The selected layout property is unsupported by this exact slot class")};
        return false;
    }
    FProperty* Property =
        Slot->GetClass()->FindPropertyByName(FName(*PropertyName));
    TSharedPtr<FUnrealMCPRecord> Changed;
    FString Snapshot;
    if (!ApplyProperty(
            Inspector, *Arguments, Blueprint, ObjectPath, Slot, Property, *Value,
            TEXT("Unreal MCP set widget slot layout"), Changed, Snapshot, OutError))
    {
        return false;
    }

    UWidget* Content = Slot->Content;
    if (Content == nullptr)
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {
                TEXT("internal_error"),
                TEXT("The edited slot lost its widget content")};
        }
        RestoreFailedTransaction(OutError);
        return false;
    }
    const FString ContentId = WidgetId(Blueprint, Content);
    const FString VerifiedSlotId = UnrealMCP::WidgetTreePrivate::PanelSlotId(
        WidgetId(Blueprint, Content->GetParent()), ContentId);
    if (VerifiedSlotId != SlotId)
    {
        OutError = {
            TEXT("internal_error"),
            TEXT("The edited slot failed identity read-back verification")};
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildResult(
        Blueprint, ObjectPath, Operation, Snapshot, ContentId, Changed);
    OutResult->SetStringField(TEXT("slot_id"), SlotId);
    OutResult->SetStringField(TEXT("changed_layout_property"), PropertyName);
    return true;
}
