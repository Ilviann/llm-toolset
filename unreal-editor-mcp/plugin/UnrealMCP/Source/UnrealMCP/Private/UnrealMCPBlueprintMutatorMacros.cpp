#include "UnrealMCPBlueprintCallableMutationSupport.h"


bool FUnrealMCPBlueprintMutator::MacroEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    FString Target;
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("target"), Target) || Target != TEXT("macro")
        || !Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Macro edits require target macro and one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("signature"), TEXT("metadata")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("macro_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("macro_id"), TEXT("field"), TEXT("signature"), TEXT("metadata"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("macro_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown macro edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The macro edit contains a field not accepted by its operation")};
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
        OutError = {TEXT("unsupported_type"), TEXT("This live Blueprint kind does not support macro editing")};
        return false;
    }

    FString MacroId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    UEdGraph* Graph = nullptr;
    FMacroSignatureSpec Signature;
    const TSharedPtr<FJsonObject>* SignatureObject = nullptr;
    const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
    UnrealMCP::BlueprintReferences::FScanResult ReferenceScan;

    if (Operation == TEXT("add"))
    {
        if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("name"), TEXT("signature"), TEXT("metadata")})
            || !Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateMemberName(Blueprint, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
            || !DecodeMacroSignature(*SignatureObject, Signature, OutError)) return false;
        if (Arguments->HasField(TEXT("metadata"))
            && (!Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                || !ValidateMacroMetadata(*MetadataObject, OutError))) return false;
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("macro_id"), MacroId)
            || (Graph = FindLocalMacro(Blueprint, MacroId)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local macro identity is unavailable")};
            return false;
        }
        Name = Graph->GetName();
        ReferenceScan = UnrealMCP::BlueprintReferences::ScanMacro(Blueprint, Graph);
        if (Operation == TEXT("rename"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("macro_id"), TEXT("new_name")})
                || !Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
                || !ValidateMemberName(Blueprint, NewName, Graph->GetFName(), OutError)) return false;
        }
        else if (Operation == TEXT("update"))
        {
            if (!Arguments->TryGetStringField(TEXT("field"), Field))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Macro update requires field signature or metadata")};
                return false;
            }
            if (Field == TEXT("signature"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("macro_id"), TEXT("field"), TEXT("signature"), TEXT("policy")})
                    || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
                    || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
                    || !DecodeMacroSignature(*SignatureObject, Signature, OutError)) return false;
                if (ReferenceScan.bReferenced)
                {
                    OutError = {TEXT("referenced_member"), TEXT("The macro is referenced and the reject-only policy forbids a signature change")};
                    OutError.Details->SetStringField(TEXT("macro_id"), MacroId);
                    OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
                    return false;
                }
            }
            else if (Field == TEXT("metadata"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("macro_id"), TEXT("field"), TEXT("metadata")})
                    || !Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                    || !ValidateMacroMetadata(*MetadataObject, OutError)) return false;
            }
            else
            {
                OutError = {TEXT("invalid_argument"), TEXT("Macro update field must be signature or metadata")};
                return false;
            }
        }
        else if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("macro_id"), TEXT("policy")})
            || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Macro removal requires policy reject_if_referenced")};
            return false;
        }
        else if (ReferenceScan.bReferenced)
        {
            OutError = {TEXT("referenced_member"), TEXT("The macro is referenced and the reject-only policy forbids removal")};
            OutError.Details->SetStringField(TEXT("macro_id"), MacroId);
            OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
            return false;
        }
    }

    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP macro edit")));
        Blueprint->Modify();
        if (Operation == TEXT("add"))
        {
            Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (Graph != nullptr)
            {
                FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, true, nullptr);
                bApplied = ApplyMacroSignature(Blueprint, Graph, Signature, OutError);
                UK2Node_Tunnel* Entry = nullptr;
                UK2Node_Tunnel* Exit = nullptr;
                bool bPure = false;
                FKismetEditorUtilities::GetInformationOnMacro(Graph, Entry, Exit, bPure);
                if (bApplied && MetadataObject != nullptr && Entry != nullptr)
                {
                    Entry->Modify();
                    ApplyCallableMetadata(Entry->MetaData, nullptr, *MetadataObject);
                }
                MacroId = GuidString(Graph->GraphGuid);
                bApplied = bApplied && MacroId.Len() == 32;
            }
        }
        else if (Operation == TEXT("rename"))
        {
            Graph->Modify();
            FBlueprintEditorUtils::RenameGraph(Graph, NewName);
            bApplied = Graph->GetName() == NewName;
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
            bApplied = FindLocalMacro(Blueprint, MacroId) == nullptr;
        }
        else if (Field == TEXT("signature"))
        {
            Graph->Modify();
            bApplied = ApplyMacroSignature(Blueprint, Graph, Signature, OutError);
        }
        else
        {
            UK2Node_Tunnel* Entry = nullptr;
            UK2Node_Tunnel* Exit = nullptr;
            bool bPure = false;
            FKismetEditorUtilities::GetInformationOnMacro(Graph, Entry, Exit, bPure);
            if (Entry != nullptr)
            {
                Entry->Modify();
                ApplyCallableMetadata(Entry->MetaData, nullptr, *MetadataObject);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                bApplied = true;
            }
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the macro edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Macro;
    if (Operation == TEXT("remove"))
    {
        Macro = MakeShared<FJsonObject>();
        Macro->SetStringField(TEXT("id"), MacroId);
        Macro->SetStringField(TEXT("name"), Name);
        Macro->SetBoolField(TEXT("removed"), true);
    }
    else if (!ReadInspectedScopedRecord(Inspector, ObjectPath, TEXT("macro_id"), MacroId, TEXT("macros"), Macro, OutError))
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
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Macro,
        Operation == TEXT("add") ? TArray<FString>{MacroId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("macro"), Macro);
    OutResult->SetObjectField(TEXT("reference_summary"), Operation == TEXT("remove")
        ? UnrealMCP::BlueprintReferences::Encode(ReferenceScan) : Macro->GetObjectField(TEXT("reference_summary")));
    return true;
}
