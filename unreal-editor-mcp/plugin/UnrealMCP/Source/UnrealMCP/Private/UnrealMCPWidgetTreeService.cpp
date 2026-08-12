#include "UnrealMCPWidgetTreeService.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPBlueprintReferenceScanner.h"
#include "UnrealMCPWidgetBindingService.h"
#include "UnrealMCPWidgetLayoutService.h"
#include "UnrealMCPWidgetStyleService.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "WidgetBlueprintOperationUtils.h"

namespace
{
using namespace UnrealMCP::BlueprintMutationPrivate;
using namespace UnrealMCP::WidgetTreePrivate;

bool HasExactFields(const FJsonObject& Arguments, std::initializer_list<const TCHAR*> Fields)
{
    return UnrealMCP::BlueprintMutationPrivate::HasOnlyFields(Arguments, Fields);
}

bool ResolveWidgetBlueprint(
    FUnrealMCPBlueprintInspector& Inspector,
    const FJsonObject& Arguments,
    UWidgetBlueprint*& OutBlueprint,
    FString& OutObjectPath,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Common = MakeShared<FJsonObject>();
    for (const TCHAR* Field : {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot")})
    {
        if (const TSharedPtr<FJsonValue>* Value = Arguments.Values.Find(Field))
        {
            Common->SetField(Field, *Value);
        }
    }
    UBlueprint* Blueprint = nullptr;
    FString PackageName;
    if (!ResolveMutableBlueprint(
        *Common, Blueprint, OutObjectPath, PackageName, OutError,
        UnrealMCP::BlueprintFamilyPolicy::EOperation::WidgetTree))
    {
        return false;
    }
    OutBlueprint = Cast<UWidgetBlueprint>(Blueprint);
    if (OutBlueprint == nullptr || OutBlueprint->WidgetTree == nullptr)
    {
        OutError = {TEXT("wrong_type"), TEXT("widget_tree_edit requires one Widget Blueprint")};
        return false;
    }
    return ValidateExpectedSnapshot(Inspector, Arguments, OutObjectPath, OutError);
}

UWidget* FindWidgetById(UWidgetBlueprint* Blueprint, const FString& Id)
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

bool FindNamedSlotById(
    UWidgetBlueprint* Blueprint,
    const FString& Id,
    FNamedSlotRef& Out)
{
    TArray<UWidget*> Widgets;
    CollectWidgets(Blueprint, Widgets);
    TArray<FNamedSlotRef> Slots;
    CollectNamedSlots(Blueprint, Widgets, Slots);
    for (const FNamedSlotRef& Slot : Slots)
    {
        if (Slot.Id == Id)
        {
            Out = Slot;
            return true;
        }
    }
    return false;
}

bool ResolveStableWidget(
    UWidgetBlueprint* Blueprint,
    const FJsonObject& Arguments,
    const TCHAR* Field,
    UWidget*& OutWidget,
    FUnrealMCPError& OutError)
{
    FString Id;
    if (!Arguments.TryGetStringField(Field, Id) || Id.Len() != 32)
    {
        OutError = {TEXT("invalid_argument"), FString(Field) + TEXT(" must be one stable 32-character widget identity")};
        return false;
    }
    OutWidget = FindWidgetById(Blueprint, Id);
    if (OutWidget == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The requested widget identity was not found")};
        return false;
    }
    return true;
}

bool ResolveWidgetClass(
    const FString& Path,
    UClass*& OutClass,
    FUnrealMCPError& OutError)
{
    if (Path.IsEmpty() || Path.Len() > 512 || !Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("\\")) || Path.Contains(TEXT("..")))
    {
        OutError = {TEXT("invalid_widget_class"), TEXT("widget_class must be one exact Unreal class object path")};
        return false;
    }
    OutClass = FindObject<UClass>(nullptr, *Path);
    if (OutClass == nullptr)
    {
        OutClass = LoadObject<UClass>(nullptr, *Path);
    }
    if (OutClass == nullptr || !OutClass->IsChildOf(UWidget::StaticClass())
        || OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || OutClass->IsEditorOnly()
        || OutClass->GetName().StartsWith(TEXT("SKEL_")) || OutClass->GetName().StartsWith(TEXT("REINST_")))
    {
        OutError = {TEXT("invalid_widget_class"), TEXT("widget_class is not one live usable UWidget class")};
        return false;
    }
    return true;
}

bool ReadWidgetName(const FJsonObject& Arguments, FString& OutName, FUnrealMCPError& OutError)
{
    if (!Arguments.TryGetStringField(TEXT("name"), OutName) || OutName.IsEmpty()
        || OutName.Len() > 128 || !FName::IsValidXName(OutName, INVALID_OBJECTNAME_CHARACTERS))
    {
        OutError = {TEXT("invalid_argument"), TEXT("name must be one valid widget object name of at most 128 characters")};
        return false;
    }
    return true;
}

UWidget* ConstructWidget(
    UWidgetBlueprint* Blueprint,
    UClass* WidgetClass,
    const FString& Name,
    FUnrealMCPError& OutError)
{
    if (Blueprint->WidgetTree->FindWidget(FName(*Name)) != nullptr)
    {
        OutError = {TEXT("write_conflict"), TEXT("A widget with the requested name already exists")};
        return nullptr;
    }
    UWidget* Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*Name));
    if (Widget == nullptr || Widget->GetName() != Name)
    {
        if (Widget != nullptr)
        {
            FWidgetBlueprintOperationUtils::RemoveTransientWidgetFromTree(Blueprint, Widget);
        }
        OutError = {TEXT("invalid_widget_class"), TEXT("Unreal could not construct the requested widget class with the exact name")};
        return nullptr;
    }
    if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
        UserWidget != nullptr && !Blueprint->IsWidgetFreeFromCircularReferences(UserWidget))
    {
        FWidgetBlueprintOperationUtils::RemoveTransientWidgetFromTree(Blueprint, Widget);
        OutError = {TEXT("invalid_widget_tree"), TEXT("The user-widget composition would create a circular dependency")};
        return nullptr;
    }
    return Widget;
}

bool ReadTarget(
    UWidgetBlueprint* Blueprint,
    const FJsonObject& Arguments,
    UPanelWidget*& OutPanel,
    FNamedSlotRef& OutNamedSlot,
    int32& OutIndex,
    FUnrealMCPError& OutError)
{
    const TSharedPtr<FJsonObject>* Target = nullptr;
    if (!Arguments.TryGetObjectField(TEXT("target"), Target) || Target == nullptr || !Target->IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("target must select one panel or named slot")};
        return false;
    }
    FString Kind;
    if (!(*Target)->TryGetStringField(TEXT("kind"), Kind))
    {
        OutError = {TEXT("invalid_argument"), TEXT("target.kind is required")};
        return false;
    }
    if (Kind == TEXT("panel"))
    {
        if (!UnrealMCP::BlueprintMutationPrivate::HasOnlyFields(
            **Target, {TEXT("kind"), TEXT("parent_id"), TEXT("index")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("A panel target accepts only kind, parent_id, and index")};
            return false;
        }
        UWidget* Parent = nullptr;
        if (!ResolveStableWidget(Blueprint, **Target, TEXT("parent_id"), Parent, OutError))
        {
            return false;
        }
        OutPanel = Cast<UPanelWidget>(Parent);
        if (OutPanel == nullptr)
        {
            OutError = {TEXT("invalid_widget_tree"), TEXT("The selected parent widget cannot contain panel children")};
            return false;
        }
        OutIndex = INDEX_NONE;
        if ((*Target)->HasField(TEXT("index")))
        {
            double Number = 0.0;
            if (!(*Target)->TryGetNumberField(TEXT("index"), Number)
                || Number < 0.0 || Number > OutPanel->GetChildrenCount()
                || FMath::FloorToDouble(Number) != Number)
            {
                OutError = {TEXT("invalid_argument"), TEXT("target.index must be a valid bounded child insertion index")};
                return false;
            }
            OutIndex = static_cast<int32>(Number);
        }
        return true;
    }
    if (Kind == TEXT("named_slot"))
    {
        if (!UnrealMCP::BlueprintMutationPrivate::HasOnlyFields(
            **Target, {TEXT("kind"), TEXT("slot_id")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("A named-slot target accepts only kind and slot_id")};
            return false;
        }
        FString SlotId;
        if (!(*Target)->TryGetStringField(TEXT("slot_id"), SlotId) || SlotId.Len() != 32)
        {
            OutError = {TEXT("invalid_argument"), TEXT("target.slot_id must be one stable 32-character slot identity")};
            return false;
        }
        if (!FindNamedSlotById(Blueprint, SlotId, OutNamedSlot))
        {
            OutError = {TEXT("not_found"), TEXT("The requested named-slot identity was not found")};
            return false;
        }
        if (OutNamedSlot.Content != nullptr)
        {
            OutError = {TEXT("write_conflict"), TEXT("The requested named slot already contains a widget")};
            return false;
        }
        return true;
    }
    OutError = {TEXT("invalid_argument"), TEXT("target.kind must be panel or named_slot")};
    return false;
}

bool ValidateNamedSlotContent(
    UWidgetBlueprint* Blueprint,
    const FNamedSlotRef& Slot,
    UWidget* Widget,
    FUnrealMCPError& OutError)
{
    INamedSlotInterface* Interface = Cast<INamedSlotInterface>(Slot.Host);
    if (Interface == nullptr)
    {
        OutError = {TEXT("invalid_widget_tree"), TEXT("The named-slot host is no longer usable")};
        return false;
    }
    if (!Slot.bTreeHost)
    {
        UWidget* HostWidget = Cast<UWidget>(Slot.Host);
        if (HostWidget == nullptr || !FWidgetBlueprintOperationUtils::IsParentChildCycleFree(Blueprint, Widget, HostWidget))
        {
            OutError = {TEXT("invalid_widget_tree"), TEXT("The named-slot move would create a widget-tree cycle")};
            return false;
        }
    }
    return true;
}

bool SetNamedSlotContent(
    UWidgetBlueprint* Blueprint,
    const FNamedSlotRef& Slot,
    UWidget* Widget,
    FUnrealMCPError& OutError)
{
    if (!ValidateNamedSlotContent(Blueprint, Slot, Widget, OutError))
    {
        return false;
    }
    INamedSlotInterface* Interface = CastChecked<INamedSlotInterface>(Slot.Host);
    Slot.Host->SetFlags(RF_Transactional);
    Slot.Host->Modify();
    Blueprint->WidgetTree->SetFlags(RF_Transactional);
    Blueprint->WidgetTree->Modify();
    Interface->SetContentForSlot(Slot.Name, Widget);
    return true;
}

void DetachNamedSlotContent(UWidgetBlueprint* Blueprint, UWidget* Widget)
{
    TArray<UWidget*> Widgets;
    CollectWidgets(Blueprint, Widgets);
    TArray<FNamedSlotRef> Slots;
    CollectNamedSlots(Blueprint, Widgets, Slots);
    for (const FNamedSlotRef& Slot : Slots)
    {
        if (Slot.Content == Widget)
        {
            if (INamedSlotInterface* Interface = Cast<INamedSlotInterface>(Slot.Host))
            {
                Slot.Host->SetFlags(RF_Transactional);
                Slot.Host->Modify();
                Interface->SetContentForSlot(Slot.Name, nullptr);
            }
            return;
        }
    }
}

bool HasDestructiveReferences(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    TSharedRef<FJsonObject>& OutDetails)
{
    TArray<UWidget*> Subtree;
    Subtree.Add(Widget);
    UWidgetTree::ForWidgetAndChildren(Widget, [&Subtree](UWidget* Item)
    {
        Subtree.AddUnique(Item);
    });
    int32 GraphReferences = 0;
    int32 BindingReferences = 0;
    TMap<FName, int32> DirectWidgetReferences;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node);
            if (Variable != nullptr && Variable->VariableReference.IsSelfContext())
            {
                ++DirectWidgetReferences.FindOrAdd(
                    Variable->VariableReference.GetMemberName());
            }
        }
    }
    for (UWidget* Item : Subtree)
    {
        const UnrealMCP::BlueprintReferences::FScanResult Scan =
            UnrealMCP::BlueprintReferences::ScanMemberVariable(Blueprint, Item->GetFName());
        GraphReferences += Scan.ReferenceCount + (Scan.bUnresolvedReferences ? 1 : 0);
        GraphReferences += DirectWidgetReferences.FindRef(Item->GetFName());
        for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
        {
            if (Binding.ObjectName == Item->GetName())
            {
                ++BindingReferences;
            }
        }
        for (const UWidgetAnimation* Animation : Blueprint->Animations)
        {
            if (Animation == nullptr)
            {
                continue;
            }
            for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            {
                if (Binding.WidgetName == Item->GetFName() || Binding.SlotWidgetName == Item->GetFName())
                {
                    ++BindingReferences;
                }
            }
        }
    }
    OutDetails->SetNumberField(TEXT("graph_reference_count"), GraphReferences);
    OutDetails->SetNumberField(TEXT("designer_reference_count"), BindingReferences);
    OutDetails->SetNumberField(TEXT("subtree_widget_count"), Subtree.Num());
    return GraphReferences > 0 || BindingReferences > 0;
}

TSharedRef<FJsonObject> BuildWidgetResult(
    UWidgetBlueprint* Blueprint,
    const FString& ObjectPath,
    const FString& Operation,
    const FString& Snapshot,
    const FString& WidgetIdentity,
    const TSharedPtr<FJsonObject>& Changed = nullptr)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("blueprint_family"), TEXT("widget"));
    Result->SetObjectField(
        TEXT("family_capabilities"),
        UnrealMCP::BlueprintFamilyPolicy::BuildLiveCapabilities(Blueprint));
    Result->SetStringField(TEXT("operation"), Operation);
    Result->SetStringField(TEXT("widget_id"), WidgetIdentity);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    if (Changed.IsValid())
    {
        Result->SetObjectField(TEXT("changed_property"), Changed);
    }
    return Result;
}
}

FUnrealMCPWidgetTreeService::FUnrealMCPWidgetTreeService(
    FUnrealMCPBlueprintInspector& InInspector)
    : Inspector(InInspector)
{
}

bool FUnrealMCPWidgetTreeService::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("operation is required")};
        return false;
    }
    if (Operation == TEXT("set_slot"))
    {
        return FUnrealMCPWidgetLayoutService(Inspector).Execute(
            Arguments, OutResult, OutError);
    }
    if (Operation == TEXT("set_style"))
    {
        return FUnrealMCPWidgetStyleService(Inspector).Execute(
            Arguments, OutResult, OutError);
    }
    if (Operation == TEXT("bind_property")
        || Operation == TEXT("unbind_property")
        || Operation == TEXT("bind_event")
        || Operation == TEXT("unbind_event"))
    {
        return FUnrealMCPWidgetBindingService(Inspector).Execute(
            Arguments, OutResult, OutError);
    }

    UWidgetBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    if (!ResolveWidgetBlueprint(Inspector, *Arguments, Blueprint, ObjectPath, OutError))
    {
        return false;
    }

    UWidget* AffectedWidget = nullptr;
    TSharedPtr<FJsonObject> Changed;
    if (Operation == TEXT("set_root") || Operation == TEXT("add"))
    {
        const bool bRoot = Operation == TEXT("set_root");
        if (!HasExactFields(*Arguments, bRoot
            ? std::initializer_list<const TCHAR*>{
                TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
                TEXT("operation"), TEXT("widget_class"), TEXT("name")}
            : std::initializer_list<const TCHAR*>{
                TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
                TEXT("operation"), TEXT("widget_class"), TEXT("name"), TEXT("target")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The widget add operation contains an unknown field")};
            return false;
        }
        if (bRoot && Blueprint->WidgetTree->RootWidget != nullptr)
        {
            OutError = {TEXT("write_conflict"), TEXT("The Widget Blueprint already has a root widget")};
            return false;
        }
        FString ClassPath;
        FString Name;
        UClass* WidgetClass = nullptr;
        if (!Arguments->TryGetStringField(TEXT("widget_class"), ClassPath)
            || !ResolveWidgetClass(ClassPath, WidgetClass, OutError)
            || !ReadWidgetName(*Arguments, Name, OutError))
        {
            return false;
        }
        UPanelWidget* Panel = nullptr;
        FNamedSlotRef NamedSlot;
        int32 Index = INDEX_NONE;
        if (!bRoot && !ReadTarget(Blueprint, *Arguments, Panel, NamedSlot, Index, OutError))
        {
            return false;
        }
        AffectedWidget = ConstructWidget(Blueprint, WidgetClass, Name, OutError);
        if (AffectedWidget == nullptr)
        {
            return false;
        }
        if (bRoot || Panel != nullptr)
        {
            FScopedTransaction Transaction(
                FText::FromString(TEXT("Unreal MCP add widget")));
            Blueprint->SetFlags(RF_Transactional);
            Blueprint->Modify();
            FText ErrorMessage;
            if (!FWidgetBlueprintOperationUtils::AddWidget(
                    Blueprint, AffectedWidget, bRoot ? nullptr : Panel, Index, ErrorMessage))
            {
                Transaction.Cancel();
                OutError = {TEXT("invalid_widget_tree"), ErrorMessage.ToString().Left(512)};
                return false;
            }
        }
        else
        {
            const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP add widget to named slot")));
            if (!SetNamedSlotContent(Blueprint, NamedSlot, AffectedWidget, OutError))
            {
                FWidgetBlueprintOperationUtils::RemoveTransientWidgetFromTree(Blueprint, AffectedWidget);
                return false;
            }
            Blueprint->Modify();
            Blueprint->OnVariableAdded(AffectedWidget->GetFName());
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
    }
    else if (Operation == TEXT("remove"))
    {
        if (!HasExactFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
            TEXT("operation"), TEXT("widget_id"), TEXT("policy")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("remove accepts only widget_id and reject_if_referenced policy")};
            return false;
        }
        FString Policy;
        if (!Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
            || !ResolveStableWidget(Blueprint, *Arguments, TEXT("widget_id"), AffectedWidget, OutError))
        {
            if (OutError.Code.IsEmpty())
            {
                OutError = {TEXT("invalid_argument"), TEXT("remove policy must be reject_if_referenced")};
            }
            return false;
        }
        if (Blueprint->WidgetTree->RootWidget == AffectedWidget)
        {
            OutError = {TEXT("invalid_widget_tree"), TEXT("The required root widget cannot be removed")};
            return false;
        }
        TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        if (HasDestructiveReferences(Blueprint, AffectedWidget, Details))
        {
            OutError = {TEXT("referenced"), TEXT("The widget or its subtree is still referenced"), Details};
            return false;
        }
        const FString RemovedId = WidgetId(Blueprint, AffectedWidget);
        FText ErrorMessage;
        if (!FWidgetBlueprintOperationUtils::RemoveWidget(Blueprint, AffectedWidget, ErrorMessage))
        {
            OutError = {TEXT("invalid_widget_tree"), ErrorMessage.ToString().Left(512)};
            return false;
        }
        FString Snapshot;
        if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
        {
            return false;
        }
        OutResult = BuildWidgetResult(Blueprint, ObjectPath, Operation, Snapshot, RemovedId);
        OutResult->SetBoolField(TEXT("removed"), true);
        return true;
    }
    else if (Operation == TEXT("rename"))
    {
        if (!HasExactFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
            TEXT("operation"), TEXT("widget_id"), TEXT("new_name")})
            || !ResolveStableWidget(Blueprint, *Arguments, TEXT("widget_id"), AffectedWidget, OutError))
        {
            if (OutError.Code.IsEmpty())
            {
                OutError = {TEXT("invalid_argument"), TEXT("rename accepts only widget_id and new_name")};
            }
            return false;
        }
        FString NewName;
        if (!Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty()
            || NewName.Len() > 128 || !FName::IsValidXName(NewName, INVALID_OBJECTNAME_CHARACTERS))
        {
            OutError = {TEXT("invalid_argument"), TEXT("new_name must be one valid widget name")};
            return false;
        }
        FText RenameError;
        if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(
                Blueprint, AffectedWidget, FText::FromString(NewName), RenameError)
            || !FWidgetBlueprintOperationUtils::RenameWidget(Blueprint, AffectedWidget, NewName))
        {
            OutError = {TEXT("write_conflict"), RenameError.ToString().Left(512)};
            return false;
        }
    }
    else if (Operation == TEXT("reparent"))
    {
        if (!HasExactFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
            TEXT("operation"), TEXT("widget_id"), TEXT("target")})
            || !ResolveStableWidget(Blueprint, *Arguments, TEXT("widget_id"), AffectedWidget, OutError))
        {
            if (OutError.Code.IsEmpty())
            {
                OutError = {TEXT("invalid_argument"), TEXT("reparent accepts only widget_id and target")};
            }
            return false;
        }
        if (Blueprint->WidgetTree->RootWidget == AffectedWidget)
        {
            OutError = {TEXT("invalid_widget_tree"), TEXT("The root widget cannot be reparented")};
            return false;
        }
        UPanelWidget* Panel = nullptr;
        FNamedSlotRef NamedSlot;
        int32 Index = INDEX_NONE;
        if (!ReadTarget(Blueprint, *Arguments, Panel, NamedSlot, Index, OutError))
        {
            return false;
        }
        if (Panel != nullptr)
        {
            const FScopedTransaction Transaction(
                FText::FromString(TEXT("Unreal MCP reparent widget")));
            FText ErrorMessage;
            if (!FWidgetBlueprintOperationUtils::MoveWidget(
                    Blueprint, AffectedWidget, Panel, Index, ErrorMessage))
            {
                OutError = {TEXT("invalid_widget_tree"), ErrorMessage.ToString().Left(512)};
                return false;
            }
        }
        else
        {
            if (!ValidateNamedSlotContent(Blueprint, NamedSlot, AffectedWidget, OutError))
            {
                return false;
            }
            const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP reparent widget to named slot")));
            Blueprint->SetFlags(RF_Transactional);
            Blueprint->Modify();
            AffectedWidget->SetFlags(RF_Transactional);
            AffectedWidget->Modify();
            if (AffectedWidget->GetParent() != nullptr)
            {
                AffectedWidget->RemoveFromParent();
            }
            else
            {
                DetachNamedSlotContent(Blueprint, AffectedWidget);
            }
            if (!SetNamedSlotContent(Blueprint, NamedSlot, AffectedWidget, OutError))
            {
                return false;
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
    }
    else if (Operation == TEXT("set_variable"))
    {
        if (!HasExactFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
            TEXT("operation"), TEXT("widget_id"), TEXT("is_variable")})
            || !ResolveStableWidget(Blueprint, *Arguments, TEXT("widget_id"), AffectedWidget, OutError))
        {
            if (OutError.Code.IsEmpty())
            {
                OutError = {TEXT("invalid_argument"), TEXT("set_variable accepts only widget_id and is_variable")};
            }
            return false;
        }
        bool bIsVariable = false;
        if (!Arguments->TryGetBoolField(TEXT("is_variable"), bIsVariable))
        {
            OutError = {TEXT("invalid_argument"), TEXT("is_variable must be Boolean")};
            return false;
        }
        if (!bIsVariable)
        {
            TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
            if (HasDestructiveReferences(Blueprint, AffectedWidget, Details))
            {
                OutError = {TEXT("referenced"), TEXT("A referenced widget cannot stop being exposed as a variable"), Details};
                return false;
            }
        }
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP set widget variable exposure")));
        AffectedWidget->SetFlags(RF_Transactional);
        AffectedWidget->Modify();
        FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(
            Blueprint, AffectedWidget, bIsVariable, true);
    }
    else if (Operation == TEXT("set_property"))
    {
        if (!HasExactFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"),
            TEXT("operation"), TEXT("widget_id"), TEXT("property_name"), TEXT("value")})
            || !ResolveStableWidget(Blueprint, *Arguments, TEXT("widget_id"), AffectedWidget, OutError))
        {
            if (OutError.Code.IsEmpty())
            {
                OutError = {TEXT("invalid_argument"), TEXT("set_property accepts only widget_id, property_name, and value")};
            }
            return false;
        }
        FString PropertyName;
        const TSharedPtr<FJsonValue>* Value = Arguments->Values.Find(TEXT("value"));
        if (!Arguments->TryGetStringField(TEXT("property_name"), PropertyName)
            || PropertyName.IsEmpty() || PropertyName.Len() > 128 || PropertyName.Contains(TEXT("."))
            || Value == nullptr || !Value->IsValid())
        {
            OutError = {TEXT("invalid_argument"), TEXT("property_name and value must identify one bounded widget default")};
            return false;
        }
        FString Kind;
        FProperty* Property = AffectedWidget->GetClass()->FindPropertyByName(FName(*PropertyName));
        if (!UnrealMCP::PropertyCodec::IsSupportedEditable(Property, Kind))
        {
            OutError = {TEXT("unsupported_property"), TEXT("The widget property is unavailable or not safely editable")};
            return false;
        }
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP set widget default")));
        AffectedWidget->SetFlags(RF_Transactional);
        AffectedWidget->Modify();
        if (!UnrealMCP::PropertyCodec::Set(
                AffectedWidget, PropertyName, *Value, Changed, OutError))
        {
            return false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown widget tree operation")};
        return false;
    }

    const FString Id = WidgetId(Blueprint, AffectedWidget);
    FString Snapshot;
    if (Id.IsEmpty() || !ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("internal_error"), TEXT("Widget mutation did not retain a stable identity")};
        }
        return false;
    }
    OutResult = BuildWidgetResult(Blueprint, ObjectPath, Operation, Snapshot, Id, Changed);
    return true;
}
