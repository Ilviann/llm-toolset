#include "UnrealMCPWidgetAuthoringSupport.h"

#include "UnrealMCPAssetAuthoringKernel.h"
#include "Components/PanelSlot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UnrealMCP::WidgetAuthoring
{
using namespace UnrealMCP::BlueprintMutationPrivate;
using namespace UnrealMCP::WidgetTreePrivate;

bool HasOnlyAuthoringFields(
    const FUnrealMCPRecord& Arguments,
    std::initializer_list<const TCHAR*> Fields)
{
    return HasOnlyFields(Arguments, Fields);
}

bool ResolveBlueprint(
    FUnrealMCPBlueprintInspector& Inspector,
    const FUnrealMCPRecord& Arguments,
    UWidgetBlueprint*& OutBlueprint,
    FString& OutObjectPath,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FUnrealMCPRecord> Common = MakeShared<FUnrealMCPRecord>();
    for (const TCHAR* Field : {
        TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot")})
    {
        if (const TSharedPtr<FUnrealMCPValue>* Value = Arguments.Values.Find(Field))
        {
            Common->SetField(Field, *Value);
        }
    }
    UBlueprint* Blueprint = nullptr;
    FString PackageName;
    if (!ResolveMutableBlueprint(
            *Common, Blueprint, OutObjectPath, PackageName, OutError,
            BlueprintFamilyPolicy::EOperation::WidgetTree))
    {
        return false;
    }
    OutBlueprint = Cast<UWidgetBlueprint>(Blueprint);
    if (OutBlueprint == nullptr || OutBlueprint->WidgetTree == nullptr)
    {
        OutError = {
            TEXT("wrong_type"),
            TEXT("UMG authoring requires one Widget Blueprint")};
        return false;
    }
    return ValidateExpectedSnapshot(
        Inspector, Arguments, OutObjectPath, OutError);
}

UWidget* FindWidget(UWidgetBlueprint* Blueprint, const FString& Id)
{
    TArray<UWidget*> Widgets;
    CollectWidgets(Blueprint, Widgets);
    for (UWidget* Widget : Widgets)
    {
        if (WidgetId(Blueprint, Widget) == Id)
        {
            return Widget;
        }
    }
    return nullptr;
}

UPanelSlot* FindPanelSlot(UWidgetBlueprint* Blueprint, const FString& Id)
{
    TArray<UWidget*> Widgets;
    CollectWidgets(Blueprint, Widgets);
    for (UWidget* Widget : Widgets)
    {
        UPanelWidget* Parent = Widget != nullptr ? Widget->GetParent() : nullptr;
        if (Parent == nullptr || Widget->Slot == nullptr)
        {
            continue;
        }
        if (PanelSlotId(
                WidgetId(Blueprint, Parent),
                WidgetId(Blueprint, Widget)) == Id)
        {
            return Widget->Slot;
        }
    }
    return nullptr;
}

bool ResolveWidget(
    UWidgetBlueprint* Blueprint,
    const FUnrealMCPRecord& Arguments,
    UWidget*& OutWidget,
    FUnrealMCPError& OutError)
{
    FString Id;
    if (!Arguments.TryGetStringField(TEXT("widget_id"), Id)
        || Id.Len() != 32)
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("widget_id must be one stable 32-character widget identity")};
        return false;
    }
    OutWidget = FindWidget(Blueprint, Id);
    if (OutWidget == nullptr)
    {
        OutError = {
            TEXT("not_found"),
            TEXT("The requested widget identity was not found")};
        return false;
    }
    return true;
}

TSharedRef<FUnrealMCPRecord> EncodeProperty(UObject* Target, FProperty* Property)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(
        TEXT("name"), Property != nullptr ? Property->GetName() : FString());
    if (Target == nullptr || Property == nullptr)
    {
        Result->SetBoolField(TEXT("supported"), false);
        return Result;
    }
    TSharedPtr<FUnrealMCPValue> Value;
    FUnrealMCPError Error;
    const bool bEncoded = GameDataValueCodec::Encode(
        Property,
        Property->ContainerPtrToValuePtr<void>(Target),
        0,
        Value,
        Error);
    Result->SetBoolField(TEXT("supported"), bEncoded);
    Result->SetObjectField(TEXT("type"), GameDataValueCodec::EncodeType(Property));
    if (bEncoded)
    {
        Result->SetField(TEXT("value"), Value);
    }
    return Result;
}

bool ApplyProperty(
    FUnrealMCPBlueprintInspector& Inspector,
    const FUnrealMCPRecord& Arguments,
    UWidgetBlueprint* Blueprint,
    const FString& ObjectPath,
    UObject* Target,
    FProperty* Property,
    const TSharedPtr<FUnrealMCPValue>& Value,
    const FString& TransactionLabel,
    TSharedPtr<FUnrealMCPRecord>& OutChanged,
    FString& OutSnapshot,
    FUnrealMCPError& OutError)
{
    if (Blueprint == nullptr || Target == nullptr || Property == nullptr
        || !Value.IsValid() || Property->ArrayDim != 1
        || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(
            CPF_Transient | CPF_Deprecated | CPF_DisableEditOnTemplate
            | CPF_InstancedReference | CPF_ContainsInstancedReference)
        || Property->IsA<FDelegateProperty>()
        || Property->IsA<FMulticastDelegateProperty>()
        || Property->IsA<FInterfaceProperty>()
        || Property->IsA<FSetProperty>()
        || Property->IsA<FMapProperty>())
    {
        OutError = {
            TEXT("unsupported_property"),
            TEXT("The selected UMG property is not safely authorable")};
        return false;
    }

    void* Existing = Property->ContainerPtrToValuePtr<void>(Target);
    void* Decoded = FMemory::Malloc(
        Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Decoded);
    Property->CopyCompleteValue(Decoded, Existing);
    const bool bDecoded = GameDataValueCodec::Decode(
        Property, Decoded, Value, 0, OutError);
    if (!bDecoded)
    {
        Property->DestroyValue(Decoded);
        FMemory::Free(Decoded);
        if (OutError.Code == TEXT("invalid_row"))
        {
            OutError.Code = TEXT("invalid_argument");
        }
        return false;
    }

    FString OperationId;
    FString ExpectedSnapshot;
    Arguments.TryGetStringField(TEXT("operation_id"), OperationId);
    Arguments.TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot);
    FUnrealMCPAssetEditRequest Request{
        OperationId,
        ObjectPath,
        ExpectedSnapshot,
        TransactionLabel,
        Blueprint,
        false,
        true};
    FUnrealMCPAssetEditHooks Hooks;
    Hooks.ReadBack = [&Inspector, &ObjectPath](UObject*, FString& Snapshot, FUnrealMCPError& Error)
    {
        return ReadSnapshot(Inspector, ObjectPath, Snapshot, Error);
    };
    Hooks.Mutate = [&](UObject*, FUnrealMCPError&)
    {
        Target->SetFlags(RF_Transactional);
        Target->Modify();
        Property->CopyCompleteValue(Existing, Decoded);
        FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
        Target->PostEditChangeProperty(Event);
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    };
    FUnrealMCPAssetEditResult EditResult;
    const bool bEdited = FUnrealMCPAssetAuthoringKernel::ExecuteEdit(
        Request, Hooks, EditResult, OutError);
    Property->DestroyValue(Decoded);
    FMemory::Free(Decoded);
    if (!bEdited)
    {
        return false;
    }
    OutChanged = EncodeProperty(Target, Property);
    if (!OutChanged->GetBoolField(TEXT("supported")))
    {
        OutError = {
            TEXT("internal_error"),
            TEXT("The UMG property could not be read back after mutation")};
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutSnapshot = EditResult.SnapshotId;
    return true;
}

TSharedRef<FUnrealMCPRecord> BuildResult(
    UWidgetBlueprint* Blueprint,
    const FString& ObjectPath,
    const FString& Operation,
    const FString& Snapshot,
    const FString& WidgetIdValue,
    const TSharedPtr<FUnrealMCPRecord>& Changed)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("blueprint_family"), TEXT("widget"));
    Result->SetObjectField(
        TEXT("family_capabilities"),
        BlueprintFamilyPolicy::BuildLiveCapabilities(Blueprint));
    Result->SetStringField(TEXT("operation"), Operation);
    if (!WidgetIdValue.IsEmpty())
    {
        Result->SetStringField(TEXT("widget_id"), WidgetIdValue);
    }
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(
        TEXT("package_dirty"),
        Blueprint != nullptr && Blueprint->GetOutermost()->IsDirty());
    if (Changed.IsValid())
    {
        Result->SetObjectField(TEXT("changed"), Changed);
    }
    return Result;
}
}
