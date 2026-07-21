#include "UnrealMCPBlueprintCallableMutationSupport.h"


bool FUnrealMCPBlueprintMutator::FunctionEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    FString Operation;
    FString Target;
    if (!Arguments->TryGetStringField(TEXT("target"), Target) || Target != TEXT("function")
        || !Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Function edits require target function and one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("signature"), TEXT("metadata")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("function_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("function_id"), TEXT("field"), TEXT("signature"), TEXT("metadata"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("function_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown function edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The function edit contains a field not accepted by its operation")};
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
        OutError = {TEXT("unsupported_type"), TEXT("This live Blueprint kind does not support user function editing")};
        return false;
    }

    FString FunctionId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    UEdGraph* Graph = nullptr;
    UK2Node_FunctionEntry* Entry = nullptr;
    FFunctionSignatureSpec Signature;
    const TSharedPtr<FJsonObject>* SignatureObject = nullptr;
    const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
    UnrealMCP::BlueprintReferences::FScanResult ReferenceScan;

    if (Operation == TEXT("add"))
    {
        if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("name"), TEXT("signature"), TEXT("metadata")})
            || !Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateMemberName(Blueprint, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
            || !DecodeFunctionSignature(*SignatureObject, Signature, OutError)) return false;
        if (Arguments->HasField(TEXT("metadata"))
            && (!Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                || !ValidateFunctionMetadata(*MetadataObject, OutError))) return false;
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("function_id"), FunctionId)
            || (Graph = FindLocalFunction(Blueprint, FunctionId)) == nullptr || !IsUserOwnedFunction(Blueprint, Graph)
            || (Entry = FindFunctionEntry(Graph)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable user-owned function identity is unavailable")};
            return false;
        }
        Name = Graph->GetName();
        ReferenceScan = UnrealMCP::BlueprintReferences::ScanFunction(Blueprint, Graph);
        if (Operation == TEXT("rename"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("new_name")})
                || !Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
                || !ValidateMemberName(Blueprint, NewName, Graph->GetFName(), OutError)) return false;
        }
        else if (Operation == TEXT("update"))
        {
            if (!Arguments->TryGetStringField(TEXT("field"), Field))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Function update requires field signature or metadata")};
                return false;
            }
            if (Field == TEXT("signature"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("function_id"), TEXT("field"), TEXT("signature"), TEXT("policy")})
                    || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
                    || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
                    || !DecodeFunctionSignature(*SignatureObject, Signature, OutError)) return false;
                if (ReferenceScan.bReferenced)
                {
                    OutError = {TEXT("referenced_member"), TEXT("The function is referenced and the reject-only policy forbids a signature change")};
                    OutError.Details->SetStringField(TEXT("function_id"), FunctionId);
                    OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
                    return false;
                }
                bool bRepNotify = false;
                for (const FBPVariableDescription& Variable : Blueprint->NewVariables) bRepNotify |= Variable.RepNotifyFunc == Graph->GetFName();
                if (bRepNotify && (Signature.bPure || !Signature.Parameters.IsEmpty()))
                {
                    OutError = {TEXT("invalid_member"), TEXT("A RepNotify function must remain impure with no inputs or outputs")};
                    return false;
                }
            }
            else if (Field == TEXT("metadata"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("function_id"), TEXT("field"), TEXT("metadata")})
                    || !Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                    || !ValidateFunctionMetadata(*MetadataObject, OutError)) return false;
            }
            else
            {
                OutError = {TEXT("invalid_argument"), TEXT("Function update field must be signature or metadata")};
                return false;
            }
        }
        else if (Operation == TEXT("remove"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("policy")})
                || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Function removal requires policy reject_if_referenced")};
                return false;
            }
            if (ReferenceScan.bReferenced)
            {
                OutError = {TEXT("referenced_member"), TEXT("The function is referenced and the reject-only policy forbids removal")};
                OutError.Details->SetStringField(TEXT("function_id"), FunctionId);
                OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
                return false;
            }
            for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
            {
                if (Variable.RepNotifyFunc == Graph->GetFName())
                {
                    OutError = {TEXT("referenced_member"), TEXT("The function is coupled to a RepNotify member and cannot be removed")};
                    return false;
                }
            }
        }
    }

    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP function edit")));
        Blueprint->Modify();
        if (Operation == TEXT("add"))
        {
            Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (Graph != nullptr)
            {
                FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
                Entry = FindFunctionEntry(Graph);
                bApplied = Entry != nullptr && ApplyFunctionSignature(Blueprint, Graph, Signature, OutError);
                if (bApplied && MetadataObject != nullptr) ApplyFunctionMetadata(Entry, *MetadataObject);
                if (bApplied)
                {
                    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                    FunctionId = GuidString(Graph->GraphGuid);
                    bApplied = FunctionId.Len() == 32;
                }
            }
        }
        else if (Operation == TEXT("rename"))
        {
            Graph->Modify();
            FBlueprintEditorUtils::RenameGraph(Graph, NewName);
            bApplied = Graph->GetName() == NewName;
            if (bApplied)
            {
                for (FBPVariableDescription& Variable : Blueprint->NewVariables)
                {
                    if (Variable.RepNotifyFunc == FName(*Name)) Variable.RepNotifyFunc = FName(*NewName);
                }
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            }
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
            bApplied = FindLocalFunction(Blueprint, FunctionId) == nullptr;
        }
        else if (Field == TEXT("signature"))
        {
            Graph->Modify();
            bApplied = ApplyFunctionSignature(Blueprint, Graph, Signature, OutError);
        }
        else
        {
            ApplyFunctionMetadata(Entry, *MetadataObject);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            bApplied = true;
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the function edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Function;
    if (Operation == TEXT("remove"))
    {
        Function = MakeShared<FJsonObject>();
        Function->SetStringField(TEXT("id"), FunctionId);
        Function->SetStringField(TEXT("name"), Name);
        Function->SetBoolField(TEXT("removed"), true);
    }
    else if (!ReadInspectedScopedRecord(Inspector, ObjectPath, TEXT("function_id"), FunctionId, TEXT("functions"), Function, OutError))
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
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Function,
        Operation == TEXT("add") ? TArray<FString>{FunctionId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("function"), Function);
    OutResult->SetObjectField(TEXT("reference_summary"), Operation == TEXT("remove")
        ? UnrealMCP::BlueprintReferences::Encode(ReferenceScan) : Function->GetObjectField(TEXT("reference_summary")));
    return true;
}
