#include "UnrealMCPBlueprintCallableMutationSupport.h"


bool FUnrealMCPBlueprintMutator::MemberEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (!RequireMutationPreconditions(*Arguments, OutError)) return false;
    FString Target;
    if (Arguments->TryGetStringField(TEXT("target"), Target))
    {
        if (Target == TEXT("function")) return FunctionEdit(Arguments, OutResult, OutError);
        if (Target == TEXT("local_variable")) return LocalVariableEdit(Arguments, OutResult, OutError);
        if (Target == TEXT("macro")) return MacroEdit(Arguments, OutResult, OutError);
        if (Target == TEXT("custom_event")) return CustomEventEdit(Arguments, OutResult, OutError);
        OutError = {TEXT("invalid_argument"), TEXT("target must be function, local_variable, macro, or custom_event when supplied")};
        return false;
    }
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_member_edit requires one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("type"), TEXT("default"), TEXT("metadata")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("member_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("member_id"), TEXT("field"), TEXT("type"), TEXT("default"), TEXT("metadata"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("member_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown member edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The member edit contains a field not accepted by its operation")};
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
        OutError = {TEXT("unsupported_type"), TEXT("This live Blueprint kind does not support member-variable editing")};
        return false;
    }

    FString MemberId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    FBPVariableDescription* Variable = nullptr;
    UnrealMCP::BlueprintReferences::FScanResult ReferenceScan;
    FEdGraphPinType NewType;
    FString NewDefault;
    const TSharedPtr<FJsonObject>* TypeObject = nullptr;
    const TSharedPtr<FJsonObject>* DefaultObject = nullptr;
    const TSharedPtr<FJsonObject>* MetadataObject = nullptr;

    if (Operation == TEXT("add"))
    {
        if (!Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateMemberName(Blueprint, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
        if (NewType.bIsReference || NewType.bIsConst)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Member-variable types cannot be reference or const")};
            return false;
        }
        if (Arguments->HasField(TEXT("default")))
        {
            if (!Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(NewType, *DefaultObject, NewDefault, OutError)) return false;
        }
        if (Arguments->HasField(TEXT("metadata")))
        {
            if (!Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr) return false;
            FBPVariableDescription Preview;
            Preview.VarType = NewType;
            Preview.PropertyFlags = CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance;
            if (!ValidateAndApplyMetadata(Blueprint, Preview, *MetadataObject, false, OutError)) return false;
        }
    }
    else
    {
        if (Operation == TEXT("update")
            && (!Arguments->TryGetStringField(TEXT("field"), Field)
                || (Field != TEXT("type") && Field != TEXT("default") && Field != TEXT("metadata"))))
        {
            OutError = {TEXT("invalid_argument"), TEXT("update requires field type, default, or metadata")};
            return false;
        }
        if (!Arguments->TryGetStringField(TEXT("member_id"), MemberId)
            || (Variable = FindLocalMember(Blueprint, MemberId)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local member identity is unavailable")};
            return false;
        }
        Name = Variable->VarName.ToString();
        ReferenceScan = UnrealMCP::BlueprintReferences::ScanMemberVariable(Blueprint, Variable->VarName);
        if (Operation == TEXT("remove") || (Operation == TEXT("update") && Field == TEXT("type")))
        {
            if (!Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Removal and type change require policy reject_if_referenced")};
                return false;
            }
            if (ReferenceScan.bReferenced)
            {
                OutError = {TEXT("referenced_member"), TEXT("The member is referenced and the reject-only policy forbids this mutation")};
                OutError.Details->SetStringField(TEXT("member_id"), MemberId);
                OutError.Details->SetNumberField(TEXT("reference_count"), ReferenceScan.ReferenceCount);
                return false;
            }
        }
    }

    if (Operation == TEXT("rename"))
    {
        if (!Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
            || !ValidateMemberName(Blueprint, NewName, Variable->VarName, OutError)) return false;
    }
    else if (Operation == TEXT("update"))
    {
        if (Field == TEXT("type"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("member_id"), TEXT("field"), TEXT("type"), TEXT("policy")})
                || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
            if (NewType.bIsReference || NewType.bIsConst)
            {
                OutError = {TEXT("invalid_argument"), TEXT("Member-variable types cannot be reference or const")};
                return false;
            }
            if ((NewType.IsSet() || NewType.IsMap()) && (Variable->PropertyFlags & CPF_Net) != 0)
            {
                OutError = {TEXT("invalid_member"), TEXT("Replicated members cannot change to a live K2 set or map type")};
                return false;
            }
        }
        else if (Field == TEXT("default"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("member_id"), TEXT("field"), TEXT("default")})
                || !Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Variable->VarType, *DefaultObject, NewDefault, OutError)) return false;
        }
        else if (Field == TEXT("metadata"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("member_id"), TEXT("field"), TEXT("metadata")})
                || !Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                || !ValidateAndApplyMetadata(Blueprint, *Variable, *MetadataObject, false, OutError)) return false;
        }
        else
        {
            OutError = {TEXT("invalid_argument"), TEXT("update field must be type, default, or metadata")};
            return false;
        }
    }
    else if (Operation == TEXT("remove")
        && (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("member_id"), TEXT("policy")})))
    {
        OutError = {TEXT("invalid_argument"), TEXT("remove accepts only member_id and reject_if_referenced policy")};
        return false;
    }

    bool bApplied = false;
    if (Operation == TEXT("rename"))
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP member rename")));
        Blueprint->Modify();
        const FName RepNotifyFunction = Variable->RepNotifyFunc;
        Variable->RepNotifyFunc = NAME_None;
        FBlueprintEditorUtils::RenameMemberVariable(Blueprint, Variable->VarName, FName(*NewName));
        Variable = FindLocalMember(Blueprint, MemberId);
        if (Variable != nullptr) Variable->RepNotifyFunc = RepNotifyFunction;
        bApplied = Variable != nullptr && Variable->VarName.ToString() == NewName;
        if (bApplied) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    else
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP member edit")));
        Blueprint->Modify();
        if (Operation == TEXT("add"))
        {
            bApplied = FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), NewType, NewDefault);
            Variable = FindLocalMemberByName(Blueprint, Name);
            bApplied = bApplied && Variable != nullptr && Variable->VarGuid.IsValid();
            if (bApplied && MetadataObject != nullptr)
            {
                bApplied = ValidateAndApplyMetadata(Blueprint, *Variable, *MetadataObject, true, OutError);
                if (bApplied) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            }
            if (bApplied) MemberId = GuidString(Variable->VarGuid);
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Variable->VarName);
            bApplied = FindLocalMember(Blueprint, MemberId) == nullptr;
        }
        else if (Field == TEXT("type"))
        {
            Variable->VarType = NewType;
            Variable->DefaultValue.Reset();
            const UClass* ObjectClass = Cast<UClass>(NewType.PinSubCategoryObject.Get());
            SetPropertyFlag(Variable->PropertyFlags, CPF_DisableEditOnTemplate,
                NewType.PinCategory == UEdGraphSchema_K2::PC_Object && ObjectClass != nullptr && ObjectClass->IsChildOf(AActor::StaticClass()));
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Variable = FindLocalMember(Blueprint, MemberId);
            bApplied = Variable != nullptr && Variable->VarType == NewType;
        }
        else if (Field == TEXT("default"))
        {
            Variable->DefaultValue = NewDefault;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Variable = FindLocalMember(Blueprint, MemberId);
            bApplied = Variable != nullptr && Variable->DefaultValue == NewDefault;
        }
        else
        {
            bApplied = ValidateAndApplyMetadata(Blueprint, *Variable, *MetadataObject, true, OutError);
            if (bApplied)
            {
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                Variable = FindLocalMember(Blueprint, MemberId);
                bApplied = Variable != nullptr;
            }
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the member edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Member;
    TSharedPtr<FJsonObject> ResultReferences = UnrealMCP::BlueprintReferences::Encode(ReferenceScan);
    if (Operation != TEXT("remove"))
    {
        if (!ReadInspectedMember(Inspector, ObjectPath, MemberId, Member, OutError))
        {
            RestoreFailedTransaction(OutError);
            return false;
        }
        const TSharedPtr<FJsonObject>* ReadReferences = nullptr;
        if (Member->TryGetObjectField(TEXT("reference_summary"), ReadReferences) && ReadReferences != nullptr)
        {
            ResultReferences = *ReadReferences;
        }
    }
    else
    {
        Member = MakeShared<FJsonObject>();
        Member->SetStringField(TEXT("id"), MemberId);
        Member->SetStringField(TEXT("name"), Name);
        Member->SetBoolField(TEXT("removed"), true);
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Member,
        Operation == TEXT("add") ? TArray<FString>{MemberId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("member"), Member);
    OutResult->SetObjectField(TEXT("reference_summary"), ResultReferences);
    return true;
}
