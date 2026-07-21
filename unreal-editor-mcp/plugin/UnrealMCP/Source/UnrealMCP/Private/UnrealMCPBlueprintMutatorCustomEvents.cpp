#include "UnrealMCPBlueprintCallableMutationSupport.h"


bool FUnrealMCPBlueprintMutator::CustomEventEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    FString Target;
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("target"), Target) || Target != TEXT("custom_event")
        || !Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Custom-event edits require target custom_event and one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("graph_id"), TEXT("name"), TEXT("signature"), TEXT("metadata")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("custom_event_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("custom_event_id"), TEXT("field"), TEXT("signature"), TEXT("metadata"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("custom_event_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown custom-event edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The custom-event edit contains a field not accepted by its operation")};
            return false;
        }
    }

    FString RawAsset;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAsset))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must identify one exact Blueprint asset")};
        return false;
    }
    const TSharedRef<FJsonObject> AssetOnly = MakeShared<FJsonObject>();
    AssetOnly->SetStringField(TEXT("asset_path"), RawAsset);
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*AssetOnly, Blueprint, ObjectPath, PackageName, OutError)
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    if (Blueprint->BlueprintType != BPTYPE_Normal)
    {
        OutError = {TEXT("unsupported_type"), TEXT("This live Blueprint kind does not support custom events")};
        return false;
    }

    FString EventId;
    FString GraphId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    UEdGraph* Graph = nullptr;
    UK2Node_CustomEvent* Event = nullptr;
    FCustomEventSignatureSpec Signature;
    const TSharedPtr<FJsonObject>* SignatureObject = nullptr;
    const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
    UnrealMCP::BlueprintReferences::FScanResult ReferenceScan;

    if (Operation == TEXT("add"))
    {
        if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("graph_id"), TEXT("name"), TEXT("signature"), TEXT("metadata")})
            || !Arguments->TryGetStringField(TEXT("graph_id"), GraphId) || (Graph = FindLocalEventGraph(Blueprint, GraphId)) == nullptr
            || !Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateMemberName(Blueprint, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
            || !DecodeCustomEventSignature(*SignatureObject, Signature, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Custom events can be added only to one stable local event graph")};
            return false;
        }
        if (Arguments->HasField(TEXT("metadata"))
            && (!Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                || !ValidateFunctionMetadata(*MetadataObject, OutError))) return false;
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("custom_event_id"), EventId)
            || (Event = FindLocalCustomEvent(Blueprint, EventId)) == nullptr || Event->IsOverride() || !Event->IsEditable()
            || (Graph = Event->GetGraph()) == nullptr || !FBlueprintEditorUtils::IsEventGraph(Graph))
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable editable custom-event identity is unavailable")};
            return false;
        }
        Name = Event->CustomFunctionName.ToString();
        ReferenceScan = UnrealMCP::BlueprintReferences::ScanCustomEvent(Blueprint, Event);
        if (Operation == TEXT("rename"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("custom_event_id"), TEXT("new_name")})
                || !Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
                || !ValidateMemberName(Blueprint, NewName, Event->CustomFunctionName, OutError)) return false;
        }
        else if (Operation == TEXT("update"))
        {
            if (!Arguments->TryGetStringField(TEXT("field"), Field))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Custom-event update requires field signature or metadata")};
                return false;
            }
            if (Field == TEXT("signature"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("custom_event_id"), TEXT("field"), TEXT("signature"), TEXT("policy")})
                    || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
                    || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
                    || !DecodeCustomEventSignature(*SignatureObject, Signature, OutError)) return false;
                if (ReferenceScan.bReferenced)
                {
                    OutError = {TEXT("referenced_member"), TEXT("The custom event is referenced and the reject-only policy forbids a signature change")};
                    OutError.Details->SetStringField(TEXT("custom_event_id"), EventId);
                    OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
                    return false;
                }
            }
            else if (Field == TEXT("metadata"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("custom_event_id"), TEXT("field"), TEXT("metadata")})
                    || !Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                    || !ValidateFunctionMetadata(*MetadataObject, OutError)) return false;
            }
            else
            {
                OutError = {TEXT("invalid_argument"), TEXT("Custom-event update field must be signature or metadata")};
                return false;
            }
        }
        else if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("custom_event_id"), TEXT("policy")})
            || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Custom-event removal requires policy reject_if_referenced")};
            return false;
        }
        else if (ReferenceScan.bReferenced)
        {
            OutError = {TEXT("referenced_member"), TEXT("The custom event is referenced and the reject-only policy forbids removal")};
            OutError.Details->SetStringField(TEXT("custom_event_id"), EventId);
            OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
            return false;
        }
    }

    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP custom event edit")));
        Blueprint->Modify();
        Graph->Modify();
        if (Operation == TEXT("add"))
        {
            Event = NewObject<UK2Node_CustomEvent>(Graph);
            if (Event != nullptr)
            {
                Event->CreateNewGuid();
                Event->CustomFunctionName = FName(*Name);
                Event->bIsEditable = true;
                Event->SetFlags(RF_Transactional);
                Event->AllocateDefaultPins();
                Event->PostPlacedNewNode();
                Graph->AddNode(Event, true, false);
                bApplied = ApplyCustomEventSignature(Blueprint, Event, Signature, OutError);
                if (bApplied && MetadataObject != nullptr)
                {
                    bool bCallInEditor = Event->bCallInEditor;
                    ApplyCallableMetadata(Event->GetUserDefinedMetaData(), &bCallInEditor, *MetadataObject);
                    Event->bCallInEditor = bCallInEditor;
                }
                EventId = GuidString(Event->NodeGuid);
                bApplied = bApplied && EventId.Len() == 32;
            }
        }
        else if (Operation == TEXT("rename"))
        {
            Event->Modify();
            Event->OnRenameNode(NewName);
            bApplied = Event->CustomFunctionName == FName(*NewName);
        }
        else if (Operation == TEXT("remove"))
        {
            Event->Modify();
            Event->DestroyNode();
            bApplied = FindLocalCustomEvent(Blueprint, EventId) == nullptr;
            if (bApplied) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        else if (Field == TEXT("signature"))
        {
            bApplied = ApplyCustomEventSignature(Blueprint, Event, Signature, OutError);
        }
        else
        {
            Event->Modify();
            bool bCallInEditor = Event->bCallInEditor;
            ApplyCallableMetadata(Event->GetUserDefinedMetaData(), &bCallInEditor, *MetadataObject);
            Event->bCallInEditor = bCallInEditor;
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            bApplied = true;
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the custom-event edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> CustomEvent;
    if (Operation == TEXT("remove"))
    {
        CustomEvent = MakeShared<FJsonObject>();
        CustomEvent->SetStringField(TEXT("id"), EventId);
        CustomEvent->SetStringField(TEXT("name"), Name);
        CustomEvent->SetBoolField(TEXT("removed"), true);
    }
    else if (!ReadInspectedScopedRecord(Inspector, ObjectPath, TEXT("custom_event_id"), EventId, TEXT("custom_events"), CustomEvent, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, CustomEvent,
        Operation == TEXT("add") ? TArray<FString>{EventId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("custom_event"), CustomEvent);
    OutResult->SetObjectField(TEXT("reference_summary"), Operation == TEXT("remove")
        ? UnrealMCP::BlueprintReferences::Encode(ReferenceScan) : CustomEvent->GetObjectField(TEXT("reference_summary")));
    return true;
}
