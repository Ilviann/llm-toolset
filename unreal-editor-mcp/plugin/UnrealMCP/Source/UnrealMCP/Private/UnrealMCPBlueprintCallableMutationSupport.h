#pragma once

#include "UnrealMCPBlueprintMutationCommon.h"

namespace UnrealMCP::BlueprintMutationPrivate
{
struct FFunctionParameterSpec
{
    FName Name;
    FString Direction;
    FEdGraphPinType Type;
    FString DefaultValue;
};

struct FFunctionSignatureSpec
{
    FString Access;
    bool bPure = false;
    bool bConst = false;
    TArray<FFunctionParameterSpec> Parameters;
};

static bool DecodeFunctionSignature(
    const TSharedPtr<FJsonObject>& Signature,
    FFunctionSignatureSpec& Out,
    FUnrealMCPError& OutError)
{
    if (!Signature.IsValid() || !HasOnlyFields(*Signature, {TEXT("access"), TEXT("pure"), TEXT("const"), TEXT("parameters")})
        || !Signature->TryGetStringField(TEXT("access"), Out.Access)
        || !Signature->TryGetBoolField(TEXT("pure"), Out.bPure)
        || !Signature->TryGetBoolField(TEXT("const"), Out.bConst)
        || (Out.Access != TEXT("public") && Out.Access != TEXT("protected") && Out.Access != TEXT("private")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("signature requires exact access, pure, const, and parameters fields")};
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
    if (!Signature->TryGetArrayField(TEXT("parameters"), Parameters) || Parameters == nullptr || Parameters->Num() > 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("signature parameters must be one bounded array")};
        return false;
    }
    TSet<FName> Names;
    for (const TSharedPtr<FJsonValue>& Value : *Parameters)
    {
        const TSharedPtr<FJsonObject>* Parameter = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Parameter) || Parameter == nullptr
            || !HasOnlyFields(**Parameter, {TEXT("name"), TEXT("direction"), TEXT("type"), TEXT("default")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Each function parameter must be one exact object")};
            return false;
        }
        FString Name;
        FFunctionParameterSpec Spec;
        const TSharedPtr<FJsonObject>* Type = nullptr;
        if (!(*Parameter)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 128
            || FName(*Name).IsNone() || !FName(*Name).IsValidXName() || Names.Contains(FName(*Name))
            || !(*Parameter)->TryGetStringField(TEXT("direction"), Spec.Direction)
            || (Spec.Direction != TEXT("input") && Spec.Direction != TEXT("output"))
            || !(*Parameter)->TryGetObjectField(TEXT("type"), Type) || Type == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*Type, Spec.Type, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Function parameter names and directions must be legal and unique")};
            return false;
        }
        Spec.Name = FName(*Name);
        if (Spec.Direction == TEXT("output") && (Spec.Type.bIsReference || Spec.Type.bIsConst || (*Parameter)->HasField(TEXT("default"))))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Output parameters cannot be reference, const, or have defaults")};
            return false;
        }
        if (Spec.Direction == TEXT("input") && Spec.Type.bIsReference && (*Parameter)->HasField(TEXT("default")))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Reference input parameters cannot have defaults")};
            return false;
        }
        if ((*Parameter)->HasField(TEXT("default")))
        {
            const TSharedPtr<FJsonObject>* Default = nullptr;
            if (!(*Parameter)->TryGetObjectField(TEXT("default"), Default) || Default == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Spec.Type, *Default, Spec.DefaultValue, OutError)) return false;
        }
        Names.Add(Spec.Name);
        Out.Parameters.Add(MoveTemp(Spec));
    }
    return true;
}

struct FMacroSignatureSpec
{
    bool bPure = false;
    TArray<FFunctionParameterSpec> Parameters;
};

struct FCustomEventSignatureSpec
{
    TArray<FFunctionParameterSpec> Parameters;
};

static bool DecodeMacroSignature(const TSharedPtr<FJsonObject>& Signature, FMacroSignatureSpec& Out, FUnrealMCPError& OutError)
{
    if (!Signature.IsValid() || !HasOnlyFields(*Signature, {TEXT("pure"), TEXT("parameters")})
        || !Signature->TryGetBoolField(TEXT("pure"), Out.bPure))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Macro signature requires exact pure and parameters fields")};
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
    if (!Signature->TryGetArrayField(TEXT("parameters"), Parameters) || Parameters == nullptr || Parameters->Num() > 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Macro signature parameters must be one bounded array")};
        return false;
    }
    TSet<FName> Names;
    for (const TSharedPtr<FJsonValue>& Value : *Parameters)
    {
        const TSharedPtr<FJsonObject>* Parameter = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Parameter) || Parameter == nullptr
            || !HasOnlyFields(**Parameter, {TEXT("name"), TEXT("direction"), TEXT("type"), TEXT("default")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Each macro parameter must be one exact object")};
            return false;
        }
        FString Name;
        FFunctionParameterSpec Spec;
        const TSharedPtr<FJsonObject>* Type = nullptr;
        if (!(*Parameter)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 128
            || FName(*Name).IsNone() || !FName(*Name).IsValidXName() || Names.Contains(FName(*Name))
            || !(*Parameter)->TryGetStringField(TEXT("direction"), Spec.Direction)
            || (Spec.Direction != TEXT("input") && Spec.Direction != TEXT("output"))
            || !(*Parameter)->TryGetObjectField(TEXT("type"), Type) || Type == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*Type, Spec.Type, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Macro parameter names and directions must be legal and unique")};
            return false;
        }
        Spec.Name = FName(*Name);
        if ((Spec.Type.bIsConst && !Spec.Type.bIsReference)
            || (Spec.Direction == TEXT("output") && (Spec.Type.bIsReference || Spec.Type.bIsConst || (*Parameter)->HasField(TEXT("default"))))
            || (Spec.Direction == TEXT("input") && Spec.Type.bIsReference && (*Parameter)->HasField(TEXT("default"))))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Macro parameter direction, reference, const, or default combination is unsupported")};
            return false;
        }
        if ((*Parameter)->HasField(TEXT("default")))
        {
            const TSharedPtr<FJsonObject>* Default = nullptr;
            if (!(*Parameter)->TryGetObjectField(TEXT("default"), Default) || Default == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Spec.Type, *Default, Spec.DefaultValue, OutError)) return false;
        }
        Names.Add(Spec.Name);
        Out.Parameters.Add(MoveTemp(Spec));
    }
    return true;
}

static bool DecodeCustomEventSignature(
    const TSharedPtr<FJsonObject>& Signature,
    FCustomEventSignatureSpec& Out,
    FUnrealMCPError& OutError)
{
    if (!Signature.IsValid() || !HasOnlyFields(*Signature, {TEXT("parameters")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Custom-event signature requires one exact parameters field")};
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
    if (!Signature->TryGetArrayField(TEXT("parameters"), Parameters) || Parameters == nullptr || Parameters->Num() > 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Custom-event signature parameters must be one bounded array")};
        return false;
    }
    TSet<FName> Names;
    for (const TSharedPtr<FJsonValue>& Value : *Parameters)
    {
        const TSharedPtr<FJsonObject>* Parameter = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Parameter) || Parameter == nullptr
            || !HasOnlyFields(**Parameter, {TEXT("name"), TEXT("type"), TEXT("default")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Each custom-event parameter must be one exact object")};
            return false;
        }
        FString Name;
        FFunctionParameterSpec Spec;
        const TSharedPtr<FJsonObject>* Type = nullptr;
        if (!(*Parameter)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 128
            || FName(*Name).IsNone() || !FName(*Name).IsValidXName() || Names.Contains(FName(*Name))
            || !(*Parameter)->TryGetObjectField(TEXT("type"), Type) || Type == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*Type, Spec.Type, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Custom-event parameter names must be legal and unique")};
            return false;
        }
        Spec.Name = FName(*Name);
        Spec.Direction = TEXT("input");
        if ((Spec.Type.bIsConst && !Spec.Type.bIsReference) || (Spec.Type.bIsReference && (*Parameter)->HasField(TEXT("default"))))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Custom-event reference/const parameters cannot carry defaults")};
            return false;
        }
        if ((*Parameter)->HasField(TEXT("default")))
        {
            const TSharedPtr<FJsonObject>* Default = nullptr;
            if (!(*Parameter)->TryGetObjectField(TEXT("default"), Default) || Default == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Spec.Type, *Default, Spec.DefaultValue, OutError)) return false;
        }
        Names.Add(Spec.Name);
        Out.Parameters.Add(MoveTemp(Spec));
    }
    return true;
}

static bool ValidateFunctionMetadata(const TSharedPtr<FJsonObject>& Metadata, FUnrealMCPError& OutError)
{
    if (!Metadata.IsValid() || Metadata->Values.IsEmpty()
        || !HasOnlyFields(*Metadata, {TEXT("category"), TEXT("tooltip"), TEXT("keywords"), TEXT("call_in_editor")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("function metadata must contain supported exact fields")};
        return false;
    }
    FString Text;
    bool Flag = false;
    if ((Metadata->HasField(TEXT("category")) && (!Metadata->TryGetStringField(TEXT("category"), Text) || Text.Len() > 128))
        || (Metadata->HasField(TEXT("tooltip")) && (!Metadata->TryGetStringField(TEXT("tooltip"), Text) || Text.Len() > 512))
        || (Metadata->HasField(TEXT("keywords")) && (!Metadata->TryGetStringField(TEXT("keywords"), Text) || Text.Len() > 256))
        || (Metadata->HasField(TEXT("call_in_editor")) && !Metadata->TryGetBoolField(TEXT("call_in_editor"), Flag)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("function metadata contains an invalid bounded value")};
        return false;
    }
    return true;
}

struct FCustomEventRpcSpec
{
    FString Mode = TEXT("not_replicated");
    FString Reliability = TEXT("unreliable");
    bool bSupplied = false;
};

static bool ReadCustomEventRpc(const UK2Node_CustomEvent* Event, FCustomEventRpcSpec& Out, FUnrealMCPError& OutError)
{
    if (Event == nullptr) return true;
    const uint32 Flags = Event->FunctionFlags & (FUNC_Net | FUNC_NetReliable | FUNC_NetServer | FUNC_NetClient | FUNC_NetMulticast);
    const bool bNet = (Flags & FUNC_Net) != 0;
    const int32 Directions = ((Flags & FUNC_NetServer) != 0 ? 1 : 0) + ((Flags & FUNC_NetClient) != 0 ? 1 : 0)
        + ((Flags & FUNC_NetMulticast) != 0 ? 1 : 0);
    if ((!bNet && Flags != 0) || (bNet && Directions != 1))
    {
        OutError = {TEXT("invalid_member"), TEXT("The custom event contains conflicting or forged network function flags")};
        return false;
    }
    if (bNet)
    {
        Out.Mode = (Flags & FUNC_NetServer) != 0 ? TEXT("server")
            : (Flags & FUNC_NetClient) != 0 ? TEXT("client") : TEXT("multicast");
        Out.Reliability = (Flags & FUNC_NetReliable) != 0 ? TEXT("reliable") : TEXT("unreliable");
    }
    return true;
}

static bool ValidateCustomEventMetadata(
    UBlueprint* Blueprint,
    const UK2Node_CustomEvent* Event,
    const TSharedPtr<FJsonObject>& Metadata,
    FCustomEventRpcSpec& OutRpc,
    FUnrealMCPError& OutError)
{
    if (!Metadata.IsValid() || Metadata->Values.IsEmpty()
        || !HasOnlyFields(*Metadata, {TEXT("category"), TEXT("tooltip"), TEXT("keywords"), TEXT("call_in_editor"),
            TEXT("rpc_mode"), TEXT("reliability")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Custom-event metadata must contain supported exact fields")};
        return false;
    }
    FString Text;
    bool bCallInEditor = Event != nullptr && Event->bCallInEditor;
    if ((Metadata->HasField(TEXT("category")) && (!Metadata->TryGetStringField(TEXT("category"), Text) || Text.Len() > 128))
        || (Metadata->HasField(TEXT("tooltip")) && (!Metadata->TryGetStringField(TEXT("tooltip"), Text) || Text.Len() > 512))
        || (Metadata->HasField(TEXT("keywords")) && (!Metadata->TryGetStringField(TEXT("keywords"), Text) || Text.Len() > 256))
        || (Metadata->HasField(TEXT("call_in_editor")) && !Metadata->TryGetBoolField(TEXT("call_in_editor"), bCallInEditor))
        || !ReadCustomEventRpc(Event, OutRpc, OutError))
    {
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_argument"), TEXT("Custom-event metadata contains an invalid bounded value")};
        return false;
    }
    OutRpc.bSupplied = Metadata->HasField(TEXT("rpc_mode")) || Metadata->HasField(TEXT("reliability"));
    if (Metadata->HasField(TEXT("rpc_mode")) && !Metadata->TryGetStringField(TEXT("rpc_mode"), OutRpc.Mode)) return false;
    if (Metadata->HasField(TEXT("reliability")) && !Metadata->TryGetStringField(TEXT("reliability"), OutRpc.Reliability)) return false;
    if (OutRpc.Mode != TEXT("not_replicated") && OutRpc.Mode != TEXT("server")
        && OutRpc.Mode != TEXT("client") && OutRpc.Mode != TEXT("multicast"))
    {
        OutError = {TEXT("invalid_argument"), TEXT("rpc_mode must be not_replicated, server, client, or multicast")};
        return false;
    }
    if (OutRpc.Reliability != TEXT("unreliable") && OutRpc.Reliability != TEXT("reliable"))
    {
        OutError = {TEXT("invalid_argument"), TEXT("reliability must be unreliable or reliable")};
        return false;
    }
    if (OutRpc.Mode == TEXT("not_replicated"))
    {
        if (Metadata->HasField(TEXT("reliability")) && OutRpc.Reliability != TEXT("unreliable"))
        {
            OutError = {TEXT("invalid_argument"), TEXT("A non-replicated custom event cannot be reliable")};
            return false;
        }
        OutRpc.Reliability = TEXT("unreliable");
    }
    if (Blueprint == nullptr || !UnrealMCP::BlueprintFamilyPolicy::SupportsRpcMode(Blueprint->ParentClass, OutRpc.Mode))
    {
        OutError = {TEXT("invalid_member"), TEXT("The selected Blueprint family does not support this RPC mode")};
        return false;
    }
    if (OutRpc.Mode != TEXT("not_replicated") && bCallInEditor)
    {
        OutError = {TEXT("invalid_member"), TEXT("Replicated custom events cannot also be call-in-editor events")};
        return false;
    }
    return true;
}

static void ApplyCustomEventRpc(UK2Node_CustomEvent* Event, const FCustomEventRpcSpec& Rpc)
{
    if (Event == nullptr || !Rpc.bSupplied) return;
    Event->FunctionFlags &= ~(FUNC_Net | FUNC_NetReliable | FUNC_NetServer | FUNC_NetClient | FUNC_NetMulticast);
    if (Rpc.Mode != TEXT("not_replicated"))
    {
        Event->FunctionFlags |= FUNC_Net;
        Event->FunctionFlags |= Rpc.Mode == TEXT("server") ? FUNC_NetServer
            : Rpc.Mode == TEXT("client") ? FUNC_NetClient : FUNC_NetMulticast;
        if (Rpc.Reliability == TEXT("reliable")) Event->FunctionFlags |= FUNC_NetReliable;
    }
}

static bool ValidateMacroMetadata(const TSharedPtr<FJsonObject>& Metadata, FUnrealMCPError& OutError)
{
    if (!Metadata.IsValid() || Metadata->Values.IsEmpty()
        || !HasOnlyFields(*Metadata, {TEXT("category"), TEXT("tooltip"), TEXT("keywords")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Macro metadata must contain supported exact fields")};
        return false;
    }
    FString Text;
    if ((Metadata->HasField(TEXT("category")) && (!Metadata->TryGetStringField(TEXT("category"), Text) || Text.Len() > 128))
        || (Metadata->HasField(TEXT("tooltip")) && (!Metadata->TryGetStringField(TEXT("tooltip"), Text) || Text.Len() > 512))
        || (Metadata->HasField(TEXT("keywords")) && (!Metadata->TryGetStringField(TEXT("keywords"), Text) || Text.Len() > 256)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Macro metadata contains an invalid bounded value")};
        return false;
    }
    return true;
}

static void ApplyFunctionMetadata(UK2Node_FunctionEntry* Entry, const TSharedPtr<FJsonObject>& Metadata)
{
    FString Text;
    bool Flag = false;
    Entry->Modify();
    if (Metadata->TryGetStringField(TEXT("category"), Text)) Entry->MetaData.Category = FText::FromString(Text);
    if (Metadata->TryGetStringField(TEXT("tooltip"), Text)) Entry->MetaData.ToolTip = FText::FromString(Text);
    if (Metadata->TryGetStringField(TEXT("keywords"), Text)) Entry->MetaData.Keywords = FText::FromString(Text);
    if (Metadata->TryGetBoolField(TEXT("call_in_editor"), Flag)) Entry->MetaData.bCallInEditor = Flag;
}

static void ApplyCallableMetadata(FKismetUserDeclaredFunctionMetadata& Target, bool* bCallInEditor, const TSharedPtr<FJsonObject>& Metadata)
{
    FString Text;
    bool Flag = false;
    if (Metadata->TryGetStringField(TEXT("category"), Text)) Target.Category = FText::FromString(Text);
    if (Metadata->TryGetStringField(TEXT("tooltip"), Text)) Target.ToolTip = FText::FromString(Text);
    if (Metadata->TryGetStringField(TEXT("keywords"), Text)) Target.Keywords = FText::FromString(Text);
    if (bCallInEditor != nullptr && Metadata->TryGetBoolField(TEXT("call_in_editor"), Flag)) *bCallInEditor = Flag;
}

static bool ApplyFunctionSignature(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FFunctionSignatureSpec& Signature,
    FUnrealMCPError& OutError)
{
    UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
    if (Entry == nullptr)
    {
        OutError = {TEXT("invalid_member"), TEXT("The function graph has no required entry node")};
        return false;
    }
    UK2Node_FunctionResult* PrimaryResult = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
    if (PrimaryResult == nullptr)
    {
        OutError = {TEXT("invalid_member"), TEXT("Unreal could not create the required function result node")};
        return false;
    }
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Results);
    Entry->Modify();
    for (UK2Node_FunctionResult* Result : Results) Result->Modify();
    for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Entry->UserDefinedPins)) Entry->RemoveUserDefinedPin(Pin);
    for (UK2Node_FunctionResult* Result : Results)
    {
        for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Result->UserDefinedPins)) Result->RemoveUserDefinedPin(Pin);
    }
    int32 Flags = Entry->GetExtraFlags();
    Flags &= ~(FUNC_Public | FUNC_Protected | FUNC_Private | FUNC_BlueprintPure | FUNC_Const);
    Flags |= Signature.Access == TEXT("private") ? FUNC_Private : Signature.Access == TEXT("protected") ? FUNC_Protected : FUNC_Public;
    if (Signature.bPure) Flags |= FUNC_BlueprintPure;
    if (Signature.bConst) Flags |= FUNC_Const;
    Entry->SetExtraFlags(Flags);
    for (const FFunctionParameterSpec& Parameter : Signature.Parameters)
    {
        if (Parameter.Direction == TEXT("input"))
        {
            FText Reason;
            if (!Entry->CanCreateUserDefinedPin(Parameter.Type, EGPD_Output, Reason)
                || Entry->CreateUserDefinedPin(Parameter.Name, Parameter.Type, EGPD_Output, false) == nullptr)
            {
                OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live function entry rejected a parameter type") : Reason.ToString().Left(512)};
                return false;
            }
            if (!Parameter.DefaultValue.IsEmpty() && !Entry->UserDefinedPins.IsEmpty()
                && !Entry->ModifyUserDefinedPinDefaultValue(Entry->UserDefinedPins.Last(), Parameter.DefaultValue))
            {
                OutError = {TEXT("invalid_argument"), TEXT("The live function entry rejected a parameter default")};
                return false;
            }
        }
        else
        {
            for (UK2Node_FunctionResult* Result : Results)
            {
                FText Reason;
                if (!Result->CanCreateUserDefinedPin(Parameter.Type, EGPD_Input, Reason)
                    || Result->CreateUserDefinedPin(Parameter.Name, Parameter.Type, EGPD_Input, false) == nullptr)
                {
                    OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live function result rejected a parameter type") : Reason.ToString().Left(512)};
                    return false;
                }
            }
        }
    }
    Entry->ReconstructNode();
    for (UK2Node_FunctionResult* Result : Results) Result->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

static bool ApplyMacroSignature(UBlueprint* Blueprint, UEdGraph* Graph, const FMacroSignatureSpec& Signature, FUnrealMCPError& OutError)
{
    UK2Node_Tunnel* Entry = nullptr;
    UK2Node_Tunnel* Exit = nullptr;
    bool bWasPure = false;
    FKismetEditorUtilities::GetInformationOnMacro(Graph, Entry, Exit, bWasPure);
    if (Entry == nullptr || Exit == nullptr)
    {
        OutError = {TEXT("invalid_member"), TEXT("The macro graph is missing its required tunnel nodes")};
        return false;
    }
    Entry->Modify();
    Exit->Modify();
    for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Entry->UserDefinedPins)) Entry->RemoveUserDefinedPin(Pin);
    for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Exit->UserDefinedPins)) Exit->RemoveUserDefinedPin(Pin);
    if (!Signature.bPure)
    {
        FEdGraphPinType ExecType;
        ExecType.PinCategory = UEdGraphSchema_K2::PC_Exec;
        FText Reason;
        if (!Entry->CanCreateUserDefinedPin(ExecType, EGPD_Output, Reason)
            || Entry->CreateUserDefinedPin(UEdGraphSchema_K2::PN_Execute, ExecType, EGPD_Output, false) == nullptr
            || !Exit->CanCreateUserDefinedPin(ExecType, EGPD_Input, Reason)
            || Exit->CreateUserDefinedPin(UEdGraphSchema_K2::PN_Then, ExecType, EGPD_Input, false) == nullptr)
        {
            OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live macro tunnels rejected execution pins") : Reason.ToString().Left(512)};
            return false;
        }
    }
    for (const FFunctionParameterSpec& Parameter : Signature.Parameters)
    {
        UK2Node_Tunnel* Node = Parameter.Direction == TEXT("input") ? Entry : Exit;
        const EEdGraphPinDirection Direction = Parameter.Direction == TEXT("input") ? EGPD_Output : EGPD_Input;
        FText Reason;
        if (Node->FindPin(Parameter.Name) != nullptr
            || !Node->CanCreateUserDefinedPin(Parameter.Type, Direction, Reason)
            || Node->CreateUserDefinedPin(Parameter.Name, Parameter.Type, Direction, false) == nullptr)
        {
            OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live macro tunnel rejected a parameter type") : Reason.ToString().Left(512)};
            return false;
        }
        if (!Parameter.DefaultValue.IsEmpty() && !Node->UserDefinedPins.IsEmpty()
            && !Node->ModifyUserDefinedPinDefaultValue(Node->UserDefinedPins.Last(), Parameter.DefaultValue))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The live macro tunnel rejected a parameter default")};
            return false;
        }
    }
    Entry->ReconstructNode();
    Exit->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

static bool ApplyCustomEventSignature(
    UBlueprint* Blueprint,
    UK2Node_CustomEvent* Event,
    const FCustomEventSignatureSpec& Signature,
    FUnrealMCPError& OutError)
{
    Event->Modify();
    for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Event->UserDefinedPins)) Event->RemoveUserDefinedPin(Pin);
    for (const FFunctionParameterSpec& Parameter : Signature.Parameters)
    {
        FText Reason;
        if (Event->FindPin(Parameter.Name) != nullptr
            || !Event->CanCreateUserDefinedPin(Parameter.Type, EGPD_Output, Reason)
            || Event->CreateUserDefinedPin(Parameter.Name, Parameter.Type, EGPD_Output, false) == nullptr)
        {
            OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live custom event rejected a parameter type") : Reason.ToString().Left(512)};
            return false;
        }
        if (!Parameter.DefaultValue.IsEmpty() && !Event->UserDefinedPins.IsEmpty()
            && !Event->ModifyUserDefinedPinDefaultValue(Event->UserDefinedPins.Last(), Parameter.DefaultValue))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The live custom event rejected a parameter default")};
            return false;
        }
    }
    Event->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

static FBPVariableDescription* FindLocalById(UK2Node_FunctionEntry* Entry, const FString& Identity)
{
    if (Entry == nullptr || Identity.Len() != 32) return nullptr;
    for (FBPVariableDescription& Variable : Entry->LocalVariables)
    {
        if (GuidString(Variable.VarGuid) == Identity) return &Variable;
    }
    return nullptr;
}

static FBPVariableDescription* FindLocalByName(UK2Node_FunctionEntry* Entry, const FString& Name)
{
    if (Entry == nullptr) return nullptr;
    for (FBPVariableDescription& Variable : Entry->LocalVariables)
    {
        if (Variable.VarName.ToString() == Name) return &Variable;
    }
    return nullptr;
}

static bool ValidateLocalName(UBlueprint* Blueprint, UK2Node_FunctionEntry* Entry, const FString& Name, const FName Existing, FUnrealMCPError& OutError)
{
    if (Name.IsEmpty() || Name.Len() > 128 || FName(*Name).IsNone() || !FName(*Name).IsValidXName())
    {
        OutError = {TEXT("invalid_member"), TEXT("The local-variable name is not one legal bounded Blueprint name")};
        return false;
    }
    for (const FBPVariableDescription& Local : Entry->LocalVariables)
    {
        if (Local.VarName == FName(*Name) && Local.VarName != Existing)
        {
            OutError = {TEXT("invalid_member"), TEXT("The local-variable name collides in its function scope")};
            return false;
        }
    }
    for (const TSharedPtr<FUserPinInfo>& Parameter : Entry->UserDefinedPins)
    {
        if (Parameter.IsValid() && Parameter->PinName == FName(*Name))
        {
            OutError = {TEXT("invalid_member"), TEXT("The local-variable name collides with a function parameter")};
            return false;
        }
    }
    return ValidateMemberName(Blueprint, Name, Existing, OutError);
}

static void SetPropertyFlag(uint64& Flags, EPropertyFlags Flag, bool bEnabled)
{
    if (bEnabled) Flags |= static_cast<uint64>(Flag);
    else Flags &= ~static_cast<uint64>(Flag);
}

static bool ReadMetadataBool(const FJsonObject& Metadata, const TCHAR* Name, bool& InOut, FUnrealMCPError& OutError)
{
    if (!Metadata.HasField(Name)) return true;
    if (!Metadata.TryGetBoolField(Name, InOut))
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("metadata.%s must be boolean"), Name)};
        return false;
    }
    return true;
}

static bool ValidateAndApplyMetadata(
    UBlueprint* Blueprint,
    FBPVariableDescription& Variable,
    const TSharedPtr<FJsonObject>& Metadata,
    bool bApply,
    FUnrealMCPError& OutError)
{
    if (!Metadata.IsValid() || Metadata->Values.IsEmpty()
        || !HasOnlyFields(*Metadata, {TEXT("category"), TEXT("tooltip"), TEXT("instance_editable"), TEXT("blueprint_visible"),
            TEXT("blueprint_read_only"), TEXT("expose_on_spawn"), TEXT("private"), TEXT("save_game"), TEXT("advanced_display"), TEXT("replication"),
            TEXT("rep_notify_function"), TEXT("replication_condition")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("metadata must contain one or more supported exact fields")};
        return false;
    }
    FString Category = Variable.Category.ToString();
    FString Tooltip = Variable.HasMetaData(TEXT("tooltip")) ? Variable.GetMetaData(TEXT("tooltip")) : FString();
    FString Replication;
    FString RepNotifyFunction = Variable.RepNotifyFunc.ToString();
    FString ReplicationCondition = StaticEnum<ELifetimeCondition>()->GetNameStringByValue(Variable.ReplicationCondition);
    bool bInstanceEditable = (Variable.PropertyFlags & CPF_Edit) != 0 && (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0;
    bool bBlueprintVisible = (Variable.PropertyFlags & CPF_BlueprintVisible) != 0;
    bool bBlueprintReadOnly = (Variable.PropertyFlags & CPF_BlueprintReadOnly) != 0;
    bool bExposeOnSpawn = Variable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
    bool bPrivate = Variable.HasMetaData(FBlueprintMetadata::MD_Private);
    bool bSaveGame = (Variable.PropertyFlags & CPF_SaveGame) != 0;
    bool bAdvancedDisplay = (Variable.PropertyFlags & CPF_AdvancedDisplay) != 0;
    if ((Metadata->HasField(TEXT("category")) && (!Metadata->TryGetStringField(TEXT("category"), Category) || Category.Len() > 128))
        || (Metadata->HasField(TEXT("tooltip")) && (!Metadata->TryGetStringField(TEXT("tooltip"), Tooltip) || Tooltip.Len() > 512))
        || !ReadMetadataBool(*Metadata, TEXT("instance_editable"), bInstanceEditable, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("blueprint_visible"), bBlueprintVisible, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("blueprint_read_only"), bBlueprintReadOnly, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("expose_on_spawn"), bExposeOnSpawn, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("private"), bPrivate, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("save_game"), bSaveGame, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("advanced_display"), bAdvancedDisplay, OutError)
        || (Metadata->HasField(TEXT("replication")) && !Metadata->TryGetStringField(TEXT("replication"), Replication))
        || (Metadata->HasField(TEXT("rep_notify_function"))
            && (!Metadata->TryGetStringField(TEXT("rep_notify_function"), RepNotifyFunction) || RepNotifyFunction.IsEmpty() || RepNotifyFunction.Len() > 128))
        || (Metadata->HasField(TEXT("replication_condition"))
            && (!Metadata->TryGetStringField(TEXT("replication_condition"), ReplicationCondition) || ReplicationCondition.IsEmpty() || ReplicationCondition.Len() > 64)))
    {
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_argument"), TEXT("metadata contains an invalid bounded value")};
        return false;
    }
    if (bExposeOnSpawn && (!bInstanceEditable || !bBlueprintVisible || bPrivate))
    {
        OutError = {TEXT("invalid_member"), TEXT("Expose-on-spawn requires a visible, non-private, instance-editable variable")};
        return false;
    }
    const FString FinalReplication = !Replication.IsEmpty() ? Replication
        : !Variable.RepNotifyFunc.IsNone() ? TEXT("rep_notify")
        : (Variable.PropertyFlags & CPF_Net) != 0 ? TEXT("replicated") : TEXT("none");
    if (FinalReplication != TEXT("none") && FinalReplication != TEXT("replicated") && FinalReplication != TEXT("rep_notify"))
    {
        OutError = {TEXT("invalid_argument"), TEXT("replication must be none, replicated, or rep_notify")};
        return false;
    }
    if (FinalReplication != TEXT("none") && (Blueprint == nullptr
        || !UnrealMCP::BlueprintFamilyPolicy::SupportsReplicatedVariables(Blueprint->ParentClass)))
    {
        OutError = {TEXT("invalid_member"), TEXT("The selected Blueprint family does not support replicated member variables")};
        return false;
    }
    if (FinalReplication != TEXT("rep_notify") && Metadata->HasField(TEXT("rep_notify_function")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("rep_notify_function is accepted only with rep_notify replication")};
        return false;
    }
    if (FinalReplication != TEXT("none") && (Variable.VarType.IsSet() || Variable.VarType.IsMap()))
    {
        OutError = {TEXT("invalid_member"), TEXT("The live Blueprint capability does not support replicated set or map variables")};
        return false;
    }
    const int64 ConditionValue = StaticEnum<ELifetimeCondition>()->GetValueByNameString(ReplicationCondition);
    if (ConditionValue == INDEX_NONE || ConditionValue < 0 || ConditionValue > MAX_uint8)
    {
        OutError = {TEXT("invalid_argument"), TEXT("replication_condition is not one exact live lifetime condition")};
        return false;
    }
    if (FinalReplication == TEXT("none") && Metadata->HasField(TEXT("replication_condition")) && ConditionValue != COND_None)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Non-replicated variables require condition COND_None")};
        return false;
    }
    if (FinalReplication == TEXT("rep_notify"))
    {
        UEdGraph* NotifyGraph = nullptr;
        if (Blueprint != nullptr)
        {
            for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
            {
                if (Candidate != nullptr && Candidate->GetName() == RepNotifyFunction) { NotifyGraph = Candidate; break; }
            }
        }
        UK2Node_FunctionEntry* NotifyEntry = FindFunctionEntry(NotifyGraph);
        TArray<UK2Node_FunctionResult*> NotifyResults;
        if (NotifyGraph != nullptr) NotifyGraph->GetNodesOfClass(NotifyResults);
        bool bHasOutputs = false;
        for (UK2Node_FunctionResult* Result : NotifyResults) bHasOutputs |= Result != nullptr && !Result->UserDefinedPins.IsEmpty();
        if (NotifyGraph == nullptr || NotifyEntry == nullptr || !IsUserOwnedFunction(Blueprint, NotifyGraph)
            || !NotifyEntry->UserDefinedPins.IsEmpty() || bHasOutputs || (NotifyEntry->GetFunctionFlags() & FUNC_BlueprintPure) != 0)
        {
            OutError = {TEXT("invalid_member"), TEXT("rep_notify_function must identify an impure user-owned function with no parameters or return values")};
            return false;
        }
    }
    if (!bApply) return true;
    Variable.Category = FText::FromString(Category);
    if (Tooltip.IsEmpty()) Variable.RemoveMetaData(TEXT("tooltip")); else Variable.SetMetaData(TEXT("tooltip"), Tooltip);
    SetPropertyFlag(Variable.PropertyFlags, CPF_Edit, true);
    SetPropertyFlag(Variable.PropertyFlags, CPF_DisableEditOnInstance, !bInstanceEditable);
    SetPropertyFlag(Variable.PropertyFlags, CPF_BlueprintVisible, bBlueprintVisible);
    SetPropertyFlag(Variable.PropertyFlags, CPF_BlueprintReadOnly, bBlueprintReadOnly);
    SetPropertyFlag(Variable.PropertyFlags, CPF_SaveGame, bSaveGame);
    SetPropertyFlag(Variable.PropertyFlags, CPF_AdvancedDisplay, bAdvancedDisplay);
    if (bExposeOnSpawn) Variable.SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
    else Variable.RemoveMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
    if (bPrivate) Variable.SetMetaData(FBlueprintMetadata::MD_Private, TEXT("true"));
    else Variable.RemoveMetaData(FBlueprintMetadata::MD_Private);
    if (!Replication.IsEmpty() || Metadata->HasField(TEXT("rep_notify_function")) || Metadata->HasField(TEXT("replication_condition")))
    {
        SetPropertyFlag(Variable.PropertyFlags, CPF_Net, FinalReplication != TEXT("none"));
        SetPropertyFlag(Variable.PropertyFlags, CPF_RepNotify, FinalReplication == TEXT("rep_notify"));
        Variable.RepNotifyFunc = FinalReplication == TEXT("rep_notify") ? FName(*RepNotifyFunction) : NAME_None;
        Variable.ReplicationCondition = FinalReplication == TEXT("none") ? COND_None : static_cast<ELifetimeCondition>(ConditionValue);
    }
    return true;
}

static bool ReadInspectedMember(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& ObjectPath,
    const FString& MemberId,
    TSharedPtr<FJsonObject>& OutMember,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), ObjectPath);
    Arguments->SetStringField(TEXT("member_id"), MemberId);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    Arguments->SetNumberField(TEXT("page_size"), 1);
    TSharedPtr<FJsonObject> Inspection;
    if (!Inspector.Execute(Arguments, Inspection, OutError) || !Inspection.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Inspection->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1)
    {
        OutError = {TEXT("internal_error"), TEXT("Member read-back did not return one exact variable record")};
        return false;
    }
    const TSharedPtr<FJsonObject>* Record = nullptr;
    if (!(*Records)[0].IsValid() || !(*Records)[0]->TryGetObject(Record) || Record == nullptr || !Record->IsValid())
    {
        OutError = {TEXT("internal_error"), TEXT("Member read-back returned an invalid variable record")};
        return false;
    }
    OutMember = *Record;
    return true;
}


static bool ReadInspectedScopedRecord(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& ObjectPath,
    const TCHAR* FilterName,
    const FString& Identity,
    const TCHAR* Section,
    TSharedPtr<FJsonObject>& OutRecord,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), ObjectPath);
    Arguments->SetStringField(FilterName, Identity);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(Section)});
    Arguments->SetNumberField(TEXT("page_size"), 1);
    TSharedPtr<FJsonObject> Inspection;
    if (!Inspector.Execute(Arguments, Inspection, OutError) || !Inspection.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Inspection->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1)
    {
        OutError = {TEXT("internal_error"), TEXT("Scoped member read-back did not return one exact record")};
        return false;
    }
    const TSharedPtr<FJsonObject>* Record = nullptr;
    if (!(*Records)[0].IsValid() || !(*Records)[0]->TryGetObject(Record) || Record == nullptr || !Record->IsValid())
    {
        OutError = {TEXT("internal_error"), TEXT("Scoped member read-back returned an invalid record")};
        return false;
    }
    OutRecord = *Record;
    return true;
}
}
