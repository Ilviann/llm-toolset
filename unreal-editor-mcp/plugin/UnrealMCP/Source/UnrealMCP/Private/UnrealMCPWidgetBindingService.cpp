#include "UnrealMCPWidgetBindingService.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWidgetAuthoringSupport.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

namespace
{
using namespace UnrealMCP::WidgetTreePrivate;

FDelegateProperty* FindTargetDelegate(
    UWidget* Widget,
    const FString& PropertyName)
{
    return Widget != nullptr
        ? FindFProperty<FDelegateProperty>(
            Widget->GetClass(),
            FName(*(PropertyName + TEXT("Delegate"))))
        : nullptr;
}

UK2Node_ComponentBoundEvent* FindEvent(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    const FName DelegateName)
{
    if (Blueprint == nullptr || Widget == nullptr)
    {
        return nullptr;
    }
    return const_cast<UK2Node_ComponentBoundEvent*>(
        FKismetEditorUtilities::FindBoundEventForComponent(
            Blueprint, DelegateName, Widget->GetFName()));
}

bool HasConnections(const UEdGraphNode* Node)
{
    if (Node == nullptr)
    {
        return false;
    }
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && !Pin->LinkedTo.IsEmpty())
        {
            return true;
        }
    }
    return false;
}

bool HasBindingCapacity(
    UWidgetBlueprint* Blueprint,
    FUnrealMCPError& OutError)
{
    TArray<FUnrealMCPWidgetBindingRecord> Existing;
    if (!FUnrealMCPWidgetBindingService::Collect(
            Blueprint, Existing, OutError))
    {
        return false;
    }
    if (Existing.Num() >= UnrealMCP::MaxWidgetBindings)
    {
        OutError = {
            TEXT("binding_limit_exceeded"),
            TEXT("The Widget Blueprint reached the binding limit")};
        return false;
    }
    return true;
}

TSharedRef<FJsonObject> ChangedBinding(
    const FString& Kind,
    const FString& Name,
    const FString& SourceKind = FString(),
    const FString& SourceName = FString())
{
    const TSharedRef<FJsonObject> Changed = MakeShared<FJsonObject>();
    Changed->SetStringField(TEXT("binding_kind"), Kind);
    Changed->SetStringField(TEXT("name"), Name);
    if (!SourceKind.IsEmpty())
    {
        Changed->SetStringField(TEXT("source_kind"), SourceKind);
        Changed->SetStringField(TEXT("source_name"), SourceName);
    }
    return Changed;
}
}

FUnrealMCPWidgetBindingService::FUnrealMCPWidgetBindingService(
    FUnrealMCPBlueprintInspector& InInspector)
    : Inspector(InInspector)
{
}

bool FUnrealMCPWidgetBindingService::Collect(
    UWidgetBlueprint* Blueprint,
    TArray<FUnrealMCPWidgetBindingRecord>& OutRecords,
    FUnrealMCPError& OutError)
{
    OutRecords.Reset();
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return true;
    }
    for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
    {
        UWidget* Widget =
            Blueprint->WidgetTree->FindWidget(FName(*Binding.ObjectName));
        const FString StableWidgetId = WidgetId(Blueprint, Widget);
        if (Widget == nullptr || StableWidgetId.IsEmpty())
        {
            continue;
        }
        const FString SourceKind =
            Binding.Kind == EBindingKind::Function
                ? TEXT("function") : TEXT("property");
        const FString SourceName =
            Binding.Kind == EBindingKind::Function
                ? Binding.FunctionName.ToString()
                : Binding.SourceProperty.ToString();
        FDelegateProperty* Target =
            FindTargetDelegate(Widget, Binding.PropertyName.ToString());
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("section"), TEXT("widget_bindings"));
        Record->SetStringField(TEXT("record_type"), TEXT("property_binding"));
        Record->SetStringField(
            TEXT("id"),
            DeterministicId(
                TEXT("property_binding|") + StableWidgetId + TEXT("|")
                + Binding.PropertyName.ToString()));
        Record->SetStringField(TEXT("widget_id"), StableWidgetId);
        Record->SetStringField(
            TEXT("target_property"), Binding.PropertyName.ToString());
        Record->SetStringField(TEXT("source_kind"), SourceKind);
        Record->SetStringField(TEXT("source_name"), SourceName);
        Record->SetStringField(TEXT("cost"), TEXT("polling"));
        Record->SetBoolField(TEXT("target_valid"), Target != nullptr);
        const FString Fingerprint =
            TEXT("property|") + StableWidgetId + TEXT("|")
            + Binding.PropertyName.ToString() + TEXT("|") + SourceKind
            + TEXT("|") + SourceName + TEXT("|")
            + GuidString(Binding.MemberGuid);
        OutRecords.Add({StableWidgetId, Fingerprint, Record});
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_ComponentBoundEvent* Event =
                Cast<UK2Node_ComponentBoundEvent>(Node);
            if (Event == nullptr)
            {
                continue;
            }
            UWidget* Widget = Blueprint->WidgetTree->FindWidget(
                Event->ComponentPropertyName);
            const FString StableWidgetId = WidgetId(Blueprint, Widget);
            if (Widget == nullptr || StableWidgetId.IsEmpty())
            {
                continue;
            }
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("section"), TEXT("widget_bindings"));
            Record->SetStringField(TEXT("record_type"), TEXT("event_binding"));
            Record->SetStringField(TEXT("id"), GuidString(Event->NodeGuid));
            Record->SetStringField(TEXT("widget_id"), StableWidgetId);
            Record->SetStringField(
                TEXT("delegate_name"), Event->DelegatePropertyName.ToString());
            Record->SetStringField(TEXT("cost"), TEXT("event_driven"));
            Record->SetStringField(
                TEXT("graph_id"), GuidString(Graph->GraphGuid));
            Record->SetStringField(
                TEXT("node_id"), GuidString(Event->NodeGuid));
            Record->SetBoolField(
                TEXT("connected"), HasConnections(Event));
            const FString Fingerprint =
                TEXT("event|") + StableWidgetId + TEXT("|")
                + Event->DelegatePropertyName.ToString() + TEXT("|")
                + GuidString(Graph->GraphGuid) + TEXT("|")
                + GuidString(Event->NodeGuid);
            OutRecords.Add({StableWidgetId, Fingerprint, Record});
        }
    }
    if (OutRecords.Num() > UnrealMCP::MaxWidgetBindings)
    {
        OutError = {
            TEXT("response_too_large"),
            TEXT("The Widget Blueprint exceeds the supported binding limit")};
        return false;
    }
    OutRecords.Sort([](
        const FUnrealMCPWidgetBindingRecord& Left,
        const FUnrealMCPWidgetBindingRecord& Right)
    {
        return Left.Fingerprint < Right.Fingerprint;
    });
    return true;
}

bool FUnrealMCPWidgetBindingService::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    using namespace UnrealMCP::WidgetAuthoring;

    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {
            TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {
            TEXT("invalid_argument"), TEXT("operation is required")};
        return false;
    }
    const bool bBindProperty = Operation == TEXT("bind_property");
    const bool bUnbindProperty = Operation == TEXT("unbind_property");
    const bool bBindEvent = Operation == TEXT("bind_event");
    const bool bUnbindEvent = Operation == TEXT("unbind_event");
    if (!bBindProperty && !bUnbindProperty && !bBindEvent && !bUnbindEvent)
    {
        OutError = {
            TEXT("invalid_argument"), TEXT("Unknown widget binding operation")};
        return false;
    }

    const bool bExact = bBindProperty
        ? HasOnlyAuthoringFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"),
            TEXT("expected_snapshot"), TEXT("operation"), TEXT("widget_id"),
            TEXT("target_property"), TEXT("source_kind"), TEXT("source_name")})
        : bUnbindProperty
            ? HasOnlyAuthoringFields(*Arguments, {
                TEXT("operation_id"), TEXT("asset_path"),
                TEXT("expected_snapshot"), TEXT("operation"),
                TEXT("widget_id"), TEXT("target_property")})
            : bBindEvent
                ? HasOnlyAuthoringFields(*Arguments, {
                    TEXT("operation_id"), TEXT("asset_path"),
                    TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("widget_id"), TEXT("delegate_name")})
                : HasOnlyAuthoringFields(*Arguments, {
                    TEXT("operation_id"), TEXT("asset_path"),
                    TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("widget_id"), TEXT("delegate_name"), TEXT("policy")});
    if (!bExact)
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("The widget binding operation contains an unknown field")};
        return false;
    }

    UWidgetBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    UWidget* Widget = nullptr;
    if (!ResolveBlueprint(
            Inspector, *Arguments, Blueprint, ObjectPath, OutError)
        || !ResolveWidget(Blueprint, *Arguments, Widget, OutError))
    {
        return false;
    }
    if (!Widget->bIsVariable)
    {
        OutError = {
            TEXT("invalid_binding"),
            TEXT("The widget must be exposed as a variable before binding")};
        return false;
    }
    const FString StableWidgetId = WidgetId(Blueprint, Widget);
    TSharedPtr<FJsonObject> Changed;

    if (bBindProperty || bUnbindProperty)
    {
        FString TargetProperty;
        if (!Arguments->TryGetStringField(
                TEXT("target_property"), TargetProperty)
            || TargetProperty.IsEmpty() || TargetProperty.Len() > 128)
        {
            OutError = {
                TEXT("invalid_argument"),
                TEXT("target_property must be one bounded widget attribute")};
            return false;
        }
        FDelegateProperty* TargetDelegate =
            FindTargetDelegate(Widget, TargetProperty);
        if (TargetDelegate == nullptr)
        {
            OutError = {
                TEXT("invalid_binding"),
                TEXT("The widget property has no compatible bindable delegate")};
            return false;
        }
        FDelegateEditorBinding Match;
        Match.ObjectName = Widget->GetName();
        Match.PropertyName = FName(*TargetProperty);
        const int32 ExistingIndex = Blueprint->Bindings.IndexOfByKey(Match);
        if (bUnbindProperty)
        {
            if (ExistingIndex == INDEX_NONE)
            {
                OutError = {
                    TEXT("not_found"),
                    TEXT("The exact widget property binding was not found")};
                return false;
            }
            const FScopedTransaction Transaction(
                FText::FromString(TEXT("Unreal MCP remove widget property binding")));
            Blueprint->Modify();
            Blueprint->Bindings.RemoveAt(ExistingIndex);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Changed = ChangedBinding(TEXT("property"), TargetProperty);
        }
        else
        {
            FString SourceKind;
            FString SourceName;
            if (!Arguments->TryGetStringField(TEXT("source_kind"), SourceKind)
                || (SourceKind != TEXT("function")
                    && SourceKind != TEXT("property"))
                || !Arguments->TryGetStringField(TEXT("source_name"), SourceName)
                || SourceName.IsEmpty() || SourceName.Len() > 128
                || Blueprint->SkeletonGeneratedClass == nullptr)
            {
                OutError = {
                    TEXT("invalid_argument"),
                    TEXT("source_kind and source_name must select one live Blueprint member")};
                return false;
            }
            FDelegateEditorBinding Binding = Match;
            TArray<FFieldVariant> Chain;
            if (SourceKind == TEXT("function"))
            {
                UFunction* Function = Blueprint->SkeletonGeneratedClass
                    ->FindFunctionByName(FName(*SourceName));
                if (Function == nullptr
                    || !Function->HasAnyFunctionFlags(
                        FUNC_BlueprintPure | FUNC_Const)
                    || !Function->IsSignatureCompatibleWith(
                        TargetDelegate->SignatureFunction,
                        UFunction::GetDefaultIgnoredSignatureCompatibilityFlags()
                            | CPF_ReturnParm))
                {
                    OutError = {
                        TEXT("invalid_binding"),
                        TEXT("The source function is missing, impure, or signature-incompatible")};
                    return false;
                }
                Binding.Kind = EBindingKind::Function;
                Binding.FunctionName = Function->GetFName();
                UBlueprint::GetGuidFromClassByFieldName<UFunction>(
                    Blueprint->SkeletonGeneratedClass,
                    Function->GetFName(),
                    Binding.MemberGuid);
                Chain.Add(FFieldVariant(Function));
            }
            else
            {
                FProperty* Source = FindFProperty<FProperty>(
                    Blueprint->SkeletonGeneratedClass, FName(*SourceName));
                if (Source == nullptr
                    || Source->IsA<FDelegateProperty>()
                    || Source->IsA<FMulticastDelegateProperty>())
                {
                    OutError = {
                        TEXT("invalid_binding"),
                        TEXT("The source property is missing or unsupported")};
                    return false;
                }
                Binding.Kind = EBindingKind::Property;
                Binding.SourceProperty = Source->GetFName();
                UBlueprint::GetGuidFromClassByFieldName<FProperty>(
                    Blueprint->SkeletonGeneratedClass,
                    Source->GetFName(),
                    Binding.MemberGuid);
                Chain.Add(FFieldVariant(Source));
            }
            Binding.SourcePath = FEditorPropertyPath(Chain);
            FText ValidationError;
            if (!Binding.SourcePath.Validate(
                    TargetDelegate, ValidationError))
            {
                OutError = {
                    TEXT("invalid_binding"),
                    ValidationError.ToString().Left(512)};
                return false;
            }
            if (ExistingIndex == INDEX_NONE
                && !HasBindingCapacity(Blueprint, OutError))
            {
                return false;
            }
            const FScopedTransaction Transaction(
                FText::FromString(TEXT("Unreal MCP set widget property binding")));
            Blueprint->Modify();
            if (ExistingIndex != INDEX_NONE)
            {
                Blueprint->Bindings[ExistingIndex] = Binding;
            }
            else
            {
                Blueprint->Bindings.Add(Binding);
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Changed = ChangedBinding(
                TEXT("property"), TargetProperty, SourceKind, SourceName);
        }
    }
    else
    {
        FString DelegateName;
        if (!Arguments->TryGetStringField(TEXT("delegate_name"), DelegateName)
            || DelegateName.IsEmpty() || DelegateName.Len() > 128)
        {
            OutError = {
                TEXT("invalid_argument"),
                TEXT("delegate_name must select one widget multicast delegate")};
            return false;
        }
        FMulticastDelegateProperty* Delegate =
            FindFProperty<FMulticastDelegateProperty>(
                Widget->GetClass(), FName(*DelegateName));
        if (Delegate == nullptr
            || !Delegate->HasAnyPropertyFlags(CPF_BlueprintAssignable))
        {
            OutError = {
                TEXT("invalid_binding"),
                TEXT("The selected widget delegate is not Blueprint-assignable")};
            return false;
        }
        UK2Node_ComponentBoundEvent* Existing =
            FindEvent(Blueprint, Widget, Delegate->GetFName());
        if (bBindEvent)
        {
            if (Existing != nullptr)
            {
                OutError = {
                    TEXT("write_conflict"),
                    TEXT("The exact widget event is already bound")};
                return false;
            }
            if (!HasBindingCapacity(Blueprint, OutError))
            {
                return false;
            }
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            int32 NodeCount = 0;
            for (UEdGraph* Graph : Graphs)
            {
                NodeCount += Graph != nullptr ? Graph->Nodes.Num() : 0;
            }
            if (NodeCount >= UnrealMCP::MaxGraphNodes)
            {
                OutError = {
                    TEXT("graph_limit_exceeded"),
                    TEXT("The Widget Blueprint has no bounded event-node capacity")};
                return false;
            }
            FObjectProperty* VariableProperty = FindFProperty<FObjectProperty>(
                Blueprint->SkeletonGeneratedClass, Widget->GetFName());
            if (VariableProperty == nullptr)
            {
                OutError = {
                    TEXT("invalid_binding"),
                    TEXT("Compile after exposing the widget variable before binding its events")};
                return false;
            }
            FText NativeError;
            {
                const FScopedTransaction Transaction(
                    FText::FromString(TEXT("Unreal MCP bind widget event")));
                Blueprint->Modify();
                if (!FWidgetBlueprintOperationUtils::BindToEventProperty(
                        Blueprint, Delegate->GetFName(), Widget->GetFName(),
                        Widget->GetClass(), false, NativeError))
                {
                    OutError = {
                        TEXT("invalid_binding"),
                        NativeError.ToString().Left(512)};
                    return false;
                }
            }
            Existing = FindEvent(Blueprint, Widget, Delegate->GetFName());
            if (Existing == nullptr || Existing->GetGraph() == nullptr)
            {
                OutError = {
                    TEXT("internal_error"),
                    TEXT("Unreal did not retain the requested widget event node")};
                RestoreFailedTransaction(OutError);
                return false;
            }
            Changed = ChangedBinding(TEXT("event"), DelegateName);
            Changed->SetStringField(
                TEXT("graph_id"),
                UnrealMCP::WidgetTreePrivate::GuidString(
                    Existing->GetGraph()->GraphGuid));
            Changed->SetStringField(
                TEXT("node_id"),
                UnrealMCP::WidgetTreePrivate::GuidString(
                    Existing->NodeGuid));
        }
        else
        {
            FString Policy;
            if (!Arguments->TryGetStringField(TEXT("policy"), Policy)
                || Policy != TEXT("reject_if_connected"))
            {
                OutError = {
                    TEXT("invalid_argument"),
                    TEXT("unbind_event policy must be reject_if_connected")};
                return false;
            }
            if (Existing == nullptr)
            {
                OutError = {
                    TEXT("not_found"),
                    TEXT("The exact widget event binding was not found")};
                return false;
            }
            if (HasConnections(Existing))
            {
                OutError = {
                    TEXT("referenced"),
                    TEXT("The widget event node is connected to graph logic")};
                return false;
            }
            const FScopedTransaction Transaction(
                FText::FromString(TEXT("Unreal MCP unbind widget event")));
            Blueprint->Modify();
            FBlueprintEditorUtils::RemoveNode(Blueprint, Existing, true);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Changed = ChangedBinding(TEXT("event"), DelegateName);
        }
    }

    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildResult(
        Blueprint, ObjectPath, Operation, Snapshot, StableWidgetId, Changed);
    return true;
}
