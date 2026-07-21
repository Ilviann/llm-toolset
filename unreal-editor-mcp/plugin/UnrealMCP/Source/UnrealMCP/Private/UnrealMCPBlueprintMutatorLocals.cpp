#include "UnrealMCPBlueprintCallableMutationSupport.h"


bool FUnrealMCPBlueprintMutator::LocalVariableEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    FString Target;
    FString Operation;
    FString FunctionId;
    if (!Arguments->TryGetStringField(TEXT("target"), Target) || Target != TEXT("local_variable")
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || !Arguments->TryGetStringField(TEXT("function_id"), FunctionId))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Local-variable edits require a function scope and one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"), TEXT("function_id")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("type"), TEXT("default")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("local_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("local_id"), TEXT("field"), TEXT("type"), TEXT("default"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("local_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown local-variable edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The local-variable edit contains a field not accepted by its operation")};
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
    UEdGraph* Graph = FindLocalFunction(Blueprint, FunctionId);
    UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
    if (Graph == nullptr || Entry == nullptr || !IsUserOwnedFunction(Blueprint, Graph)
        || !FBlueprintEditorUtils::DoesSupportLocalVariables(Graph))
    {
        OutError = {TEXT("unsupported_type"), TEXT("The requested function does not support editable local variables")};
        return false;
    }

    FString LocalId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    FBPVariableDescription* Variable = nullptr;
    FEdGraphPinType NewType;
    FString NewDefault;
    const TSharedPtr<FJsonObject>* TypeObject = nullptr;
    const TSharedPtr<FJsonObject>* DefaultObject = nullptr;
    UnrealMCP::BlueprintReferences::FScanResult ReferenceScan;

    if (Operation == TEXT("add"))
    {
        if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("function_id"), TEXT("name"), TEXT("type"), TEXT("default")})
            || !Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateLocalName(Blueprint, Entry, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
        if (NewType.bIsReference || NewType.bIsConst)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Local-variable types cannot be reference or const")};
            return false;
        }
        if (Arguments->HasField(TEXT("default"))
            && (!Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(NewType, *DefaultObject, NewDefault, OutError))) return false;
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("local_id"), LocalId) || (Variable = FindLocalById(Entry, LocalId)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local-variable identity is unavailable")};
            return false;
        }
        Name = Variable->VarName.ToString();
        ReferenceScan = UnrealMCP::BlueprintReferences::ScanLocalVariable(Blueprint, Graph, Variable->VarName);
        if (Operation == TEXT("rename"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("local_id"), TEXT("new_name")})
                || !Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
                || !ValidateLocalName(Blueprint, Entry, NewName, Variable->VarName, OutError)) return false;
        }
        else if (Operation == TEXT("update"))
        {
            if (!Arguments->TryGetStringField(TEXT("field"), Field) || (Field != TEXT("type") && Field != TEXT("default")))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Local-variable update field must be type or default")};
                return false;
            }
            if (Field == TEXT("type"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("function_id"), TEXT("local_id"), TEXT("field"), TEXT("type"), TEXT("policy")})
                    || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
                    || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
                    || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
                if (NewType.bIsReference || NewType.bIsConst)
                {
                    OutError = {TEXT("invalid_argument"), TEXT("Local-variable types cannot be reference or const")};
                    return false;
                }
                if (ReferenceScan.bReferenced)
                {
                    OutError = {TEXT("referenced_member"), TEXT("The local variable is referenced and the reject-only policy forbids a type change")};
                    OutError.Details->SetStringField(TEXT("local_id"), LocalId);
                    return false;
                }
            }
            else if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("local_id"), TEXT("field"), TEXT("default")})
                || !Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Variable->VarType, *DefaultObject, NewDefault, OutError)) return false;
        }
        else if (Operation == TEXT("remove"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("local_id"), TEXT("policy")})
                || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Local-variable removal requires policy reject_if_referenced")};
                return false;
            }
            if (ReferenceScan.bReferenced)
            {
                OutError = {TEXT("referenced_member"), TEXT("The local variable is referenced and the reject-only policy forbids removal")};
                OutError.Details->SetStringField(TEXT("local_id"), LocalId);
                return false;
            }
        }
    }

    UStruct* Scope = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(Graph->GetFName()) : nullptr;
    if (Operation != TEXT("add") && Scope == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The live generated function scope is unavailable; compile and inspect before retrying"), MakeShared<FJsonObject>(), true};
        return false;
    }
    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP local variable edit")));
        Blueprint->Modify();
        Entry->Modify();
        if (Operation == TEXT("add"))
        {
            bApplied = FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, FName(*Name), NewType, NewDefault);
            Variable = FindLocalByName(FindFunctionEntry(Graph), Name);
            bApplied = bApplied && Variable != nullptr && Variable->VarGuid.IsValid();
            if (bApplied) LocalId = GuidString(Variable->VarGuid);
        }
        else if (Operation == TEXT("rename"))
        {
            FBlueprintEditorUtils::RenameLocalVariable(Blueprint, Scope, Variable->VarName, FName(*NewName));
            Variable = FindLocalById(FindFunctionEntry(Graph), LocalId);
            bApplied = Variable != nullptr && Variable->VarName == FName(*NewName);
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveLocalVariable(Blueprint, Scope, Variable->VarName);
            bApplied = FindLocalById(FindFunctionEntry(Graph), LocalId) == nullptr;
        }
        else if (Field == TEXT("type"))
        {
            FBlueprintEditorUtils::ChangeLocalVariableType(Blueprint, Scope, Variable->VarName, NewType);
            Variable = FindLocalById(FindFunctionEntry(Graph), LocalId);
            bApplied = Variable != nullptr && Variable->VarType == NewType;
        }
        else
        {
            Variable->DefaultValue = NewDefault;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Variable = FindLocalById(FindFunctionEntry(Graph), LocalId);
            bApplied = Variable != nullptr && Variable->DefaultValue == NewDefault;
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the local-variable edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Local;
    TSharedPtr<FJsonObject> ResultReferences = UnrealMCP::BlueprintReferences::Encode(ReferenceScan);
    if (Operation == TEXT("remove"))
    {
        Local = MakeShared<FJsonObject>();
        Local->SetStringField(TEXT("id"), LocalId);
        Local->SetStringField(TEXT("name"), Name);
        Local->SetBoolField(TEXT("removed"), true);
    }
    else
    {
        if (!ReadInspectedScopedRecord(Inspector, ObjectPath, TEXT("local_id"), LocalId, TEXT("local_variables"), Local, OutError))
        {
            RestoreFailedTransaction(OutError);
            return false;
        }
        const TSharedPtr<FJsonObject>* ReadReferences = nullptr;
        if (Local->TryGetObjectField(TEXT("reference_summary"), ReadReferences) && ReadReferences != nullptr) ResultReferences = *ReadReferences;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Local,
        Operation == TEXT("add") ? TArray<FString>{LocalId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("local_variable"), Local);
    OutResult->SetObjectField(TEXT("reference_summary"), ResultReferences);
    return true;
}
