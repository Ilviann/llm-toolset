#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase6FunctionAndLocalTest, "UnrealMCP.Phase6.FunctionsAndLocals", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase6FunctionAndLocalTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase6");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Phase 6 Blueprint fixture is created"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> IntType = K2Type(TEXT("int"));
    TSharedRef<FJsonObject> ConstRefString = K2Type(TEXT("string"));
    ConstRefString->SetBoolField(TEXT("reference"), true);
    ConstRefString->SetBoolField(TEXT("const"), true);
    TArray<TSharedPtr<FJsonValue>> ParametersJson = {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Count"), TEXT("input"), IntType,
            LiteralDefault(MakeShared<FJsonValueNumber>(3)))),
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Label"), TEXT("input"), ConstRefString)),
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Succeeded"), TEXT("output"), K2Type(TEXT("boolean"))))};
    TSharedRef<FJsonObject> AddFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddFunction->SetStringField(TEXT("name"), TEXT("Compute"));
    AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("protected"), false, true, ParametersJson));
    const TSharedRef<FJsonObject> FunctionMetadata = MakeShared<FJsonObject>();
    FunctionMetadata->SetStringField(TEXT("category"), TEXT("Unreal MCP"));
    FunctionMetadata->SetStringField(TEXT("tooltip"), TEXT("Computes one bounded result"));
    FunctionMetadata->SetStringField(TEXT("keywords"), TEXT("compute bounded"));
    AddFunction->SetObjectField(TEXT("metadata"), FunctionMetadata);
    if (!TestTrue(TEXT("function shell and complete signature add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString FunctionId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    TestEqual(TEXT("function gets stable graph identity"), FunctionId.Len(), 32);
    TestEqual(TEXT("function signature reads all parameter directions"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters")).Num(), 3);
    const TArray<TSharedPtr<FJsonValue>>& ReadParameters =
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters"));
    TestTrue(TEXT("ordinary input retains its tagged default"), ReadParameters[0]->AsObject()->HasField(TEXT("default")));
    TestFalse(TEXT("reference input does not invent a default"), ReadParameters[1]->AsObject()->HasField(TEXT("default")));
    TestFalse(TEXT("output does not invent a default"), ReadParameters[2]->AsObject()->HasField(TEXT("default")));
    TestTrue(TEXT("function preserves required entry and result nodes"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("required_nodes"))->GetBoolField(TEXT("valid")));
    TestEqual(TEXT("function metadata reads back exactly"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("category")), FString(TEXT("Unreal MCP")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const bool bDirtyBeforeInvalidSignature = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeInvalidSignature = Blueprint->Status;
    const int32 TransactionsBeforeInvalidSignature = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TSharedRef<FJsonObject> OutputReference = K2Type(TEXT("boolean"));
    OutputReference->SetBoolField(TEXT("reference"), true);
    TSharedRef<FJsonObject> InvalidOutputSignature = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("update"));
    InvalidOutputSignature->SetStringField(TEXT("function_id"), FunctionId);
    InvalidOutputSignature->SetStringField(TEXT("field"), TEXT("signature"));
    InvalidOutputSignature->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    InvalidOutputSignature->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Invalid"), TEXT("output"), OutputReference))}));
    TestFalse(TEXT("unsupported output-reference signature rejects before mutation"),
        Mutator.Execute(TEXT("blueprint_member_edit"), InvalidOutputSignature, Result, Error));
    TestEqual(TEXT("invalid signature preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestEqual(TEXT("invalid signature preserves package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeInvalidSignature);
    TestEqual(TEXT("invalid signature preserves compile state"), Blueprint->Status, StatusBeforeInvalidSignature);
    TestEqual(TEXT("invalid signature creates no transaction"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeInvalidSignature);

    TSharedRef<FJsonObject> RenameFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("rename"));
    RenameFunction->SetStringField(TEXT("function_id"), FunctionId);
    RenameFunction->SetStringField(TEXT("new_name"), TEXT("ComputeHealth"));
    if (!TestTrue(TEXT("function rename succeeds and preserves graph identity"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RenameFunction, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("function rename preserves stable identity"),
        ScopedIdByName(Inspector, AssetPath, TEXT("functions"), TEXT("ComputeHealth")), FunctionId);

    TSharedRef<FJsonObject> UpdateSignature = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("update"));
    UpdateSignature->SetStringField(TEXT("function_id"), FunctionId);
    UpdateSignature->SetStringField(TEXT("field"), TEXT("signature"));
    UpdateSignature->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateSignature->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), true, false, ParametersJson));
    if (!TestTrue(TEXT("unreferenced complete-signature update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateSignature, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const TSharedPtr<FJsonObject> UpdatedSignature = Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("signature"));
    TestEqual(TEXT("signature access updates exactly"), UpdatedSignature->GetStringField(TEXT("access")), FString(TEXT("public")));
    TestTrue(TEXT("signature pure flag updates exactly"), UpdatedSignature->GetBoolField(TEXT("pure")));
    TestFalse(TEXT("signature const flag updates exactly"), UpdatedSignature->GetBoolField(TEXT("const")));

    TSharedRef<FJsonObject> UpdateFunctionMetadata = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("update"));
    UpdateFunctionMetadata->SetStringField(TEXT("function_id"), FunctionId);
    UpdateFunctionMetadata->SetStringField(TEXT("field"), TEXT("metadata"));
    const TSharedRef<FJsonObject> UpdatedMetadata = MakeShared<FJsonObject>();
    UpdatedMetadata->SetStringField(TEXT("category"), TEXT("Utilities"));
    UpdatedMetadata->SetBoolField(TEXT("call_in_editor"), true);
    UpdateFunctionMetadata->SetObjectField(TEXT("metadata"), UpdatedMetadata);
    if (!TestTrue(TEXT("function metadata update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateFunctionMetadata, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("function metadata category updates exactly"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("category")), FString(TEXT("Utilities")));
    TestTrue(TEXT("function call-in-editor metadata updates exactly"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("metadata"))->GetBoolField(TEXT("call_in_editor")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const int32 TransactionsBeforeCollision = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TSharedRef<FJsonObject> CollidingLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("add"));
    CollidingLocal->SetStringField(TEXT("function_id"), FunctionId);
    CollidingLocal->SetStringField(TEXT("name"), TEXT("Count"));
    CollidingLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    TestFalse(TEXT("local collision with a function parameter rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), CollidingLocal, Result, Error));
    TestEqual(TEXT("local collision preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestEqual(TEXT("local collision creates no transaction"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeCollision);

    TSharedRef<FJsonObject> AddLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("add"));
    AddLocal->SetStringField(TEXT("function_id"), FunctionId);
    AddLocal->SetStringField(TEXT("name"), TEXT("Accumulator"));
    AddLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    AddLocal->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueNumber>(9)));
    if (!TestTrue(TEXT("typed function-local variable add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString LocalId = Result->GetObjectField(TEXT("local_variable"))->GetStringField(TEXT("id"));
    TestEqual(TEXT("local variable gets stable identity"), LocalId.Len(), 32);
    TestEqual(TEXT("local scope reports owning function"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("scope"))->GetStringField(TEXT("function_id")), FunctionId);
    TestEqual(TEXT("local default reads back exactly"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("default"))->GetNumberField(TEXT("value")), 9.0);

    const FString BeforeRename = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> RenameLocal = ScopedMemberEditArguments(AssetPath, BeforeRename, TEXT("local_variable"), TEXT("rename"));
    RenameLocal->SetStringField(TEXT("function_id"), FunctionId);
    RenameLocal->SetStringField(TEXT("local_id"), LocalId);
    RenameLocal->SetStringField(TEXT("new_name"), TEXT("RunningTotal"));
    if (!TestTrue(TEXT("local rename succeeds and preserves identity"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RenameLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString AfterRename = Result->GetStringField(TEXT("snapshot_id"));
    TestEqual(TEXT("local rename preserves stable identity"), ScopedIdByName(Inspector, AssetPath, TEXT("local_variables"), TEXT("RunningTotal")), LocalId);
    TestTrue(TEXT("local transaction undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("local undo restores snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeRename);
    TestTrue(TEXT("local transaction redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("local redo restores snapshot"), InspectSnapshot(Inspector, AssetPath), AfterRename);

    Snapshot = AfterRename;
    TSharedRef<FJsonObject> UpdateLocalType = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("update"));
    UpdateLocalType->SetStringField(TEXT("function_id"), FunctionId);
    UpdateLocalType->SetStringField(TEXT("local_id"), LocalId);
    UpdateLocalType->SetStringField(TEXT("field"), TEXT("type"));
    UpdateLocalType->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateLocalType->SetObjectField(TEXT("type"), K2Type(TEXT("string")));
    if (!TestTrue(TEXT("unreferenced local type update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateLocalType, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("local type reads back exactly"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("type"))->GetStringField(TEXT("category")), FString(TEXT("string")));

    TSharedRef<FJsonObject> UpdateLocalDefault = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("update"));
    UpdateLocalDefault->SetStringField(TEXT("function_id"), FunctionId);
    UpdateLocalDefault->SetStringField(TEXT("local_id"), LocalId);
    UpdateLocalDefault->SetStringField(TEXT("field"), TEXT("default"));
    UpdateLocalDefault->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueString>(TEXT("ready"))));
    if (!TestTrue(TEXT("local tagged-default update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateLocalDefault, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("local default reads back after update"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("default"))->GetStringField(TEXT("value")), FString(TEXT("ready")));

    TSharedRef<FJsonObject> AddTemporaryLocal = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("add"));
    AddTemporaryLocal->SetStringField(TEXT("function_id"), FunctionId);
    AddTemporaryLocal->SetStringField(TEXT("name"), TEXT("Scratch"));
    AddTemporaryLocal->SetObjectField(TEXT("type"), K2Type(TEXT("boolean")));
    if (!TestTrue(TEXT("second scoped local add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddTemporaryLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TemporaryLocalId = Result->GetObjectField(TEXT("local_variable"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> RemoveTemporaryLocal = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("remove"));
    RemoveTemporaryLocal->SetStringField(TEXT("function_id"), FunctionId);
    RemoveTemporaryLocal->SetStringField(TEXT("local_id"), TemporaryLocalId);
    RemoveTemporaryLocal->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(TEXT("unreferenced scoped local removal succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RemoveTemporaryLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddTemporary = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddTemporary->SetStringField(TEXT("name"), TEXT("Temporary"));
    AddTemporary->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), true, false, {}));
    if (!TestTrue(TEXT("temporary pure function add succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddTemporary, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TemporaryId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> RemoveTemporary = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("remove"));
    RemoveTemporary->SetStringField(TEXT("function_id"), TemporaryId);
    RemoveTemporary->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(TEXT("unreferenced function removal succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveTemporary, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddNotify = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddNotify->SetStringField(TEXT("name"), TEXT("OnRep_Health"));
    AddNotify->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), false, false, {}));
    if (!TestTrue(TEXT("RepNotify-compatible function add succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddNotify, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString NotifyId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));

    TSharedRef<FJsonObject> AddHealth = MemberEditArguments(AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("add"));
    AddHealth->SetStringField(TEXT("name"), TEXT("Health"));
    AddHealth->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    const TSharedRef<FJsonObject> RepNotifyMetadata = MakeShared<FJsonObject>();
    RepNotifyMetadata->SetStringField(TEXT("replication"), TEXT("rep_notify"));
    RepNotifyMetadata->SetStringField(TEXT("rep_notify_function"), TEXT("OnRep_Health"));
    RepNotifyMetadata->SetStringField(TEXT("replication_condition"), TEXT("COND_OwnerOnly"));
    AddHealth->SetObjectField(TEXT("metadata"), RepNotifyMetadata);
    if (!TestTrue(TEXT("RepNotify member coupling succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddHealth, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("RepNotify relationship reads as valid"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("replication"))->GetBoolField(TEXT("relationship_valid")));
    TestEqual(TEXT("RepNotify relationship carries stable function identity"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function_id")), NotifyId);
    const FString HealthId = Result->GetObjectField(TEXT("member"))->GetStringField(TEXT("id"));

    TSharedRef<FJsonObject> RenameNotify = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("rename"));
    RenameNotify->SetStringField(TEXT("function_id"), NotifyId);
    RenameNotify->SetStringField(TEXT("new_name"), TEXT("OnRep_CurrentHealth"));
    if (!TestTrue(TEXT("RepNotify function rename succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RenameNotify, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TSharedPtr<FJsonObject> RepNotifyInspection;
    const TSharedRef<FJsonObject> InspectHealth = InspectArguments(AssetPath);
    InspectHealth->SetStringField(TEXT("member_id"), HealthId);
    InspectHealth->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    if (!TestTrue(TEXT("renamed RepNotify relationship remains inspectable"),
        Inspector.Execute(InspectHealth, RepNotifyInspection, Error)) || !RepNotifyInspection.IsValid()) return false;
    const TSharedPtr<FJsonObject> RenamedHealth = RepNotifyInspection->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("RepNotify rename updates member relationship"),
        RenamedHealth->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function")), FString(TEXT("OnRep_CurrentHealth")));
    TestEqual(TEXT("RepNotify rename preserves function identity"),
        RenamedHealth->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function_id")), NotifyId);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> InvalidNotifySignature = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("update"));
    InvalidNotifySignature->SetStringField(TEXT("function_id"), NotifyId);
    InvalidNotifySignature->SetStringField(TEXT("field"), TEXT("signature"));
    InvalidNotifySignature->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    InvalidNotifySignature->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), false, false, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Invalid"), TEXT("input"), K2Type(TEXT("int"))))}));
    TestFalse(TEXT("invalid RepNotify signature change rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), InvalidNotifySignature, Result, Error));
    TestEqual(TEXT("RepNotify signature rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    if (!TestEqual(TEXT("Phase 6 Blueprint compiles without errors"), Log.NumErrors, 0)) return false;
    UEdGraph* FunctionGraph = nullptr;
    for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
        if (Candidate != nullptr && Candidate->GraphGuid.ToString(EGuidFormats::Digits).ToLower() == FunctionId) FunctionGraph = Candidate;
    UK2Node_FunctionEntry* FunctionEntry = FunctionGraph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(FunctionGraph)) : nullptr;
    FBPVariableDescription* Local = nullptr;
    if (FunctionEntry != nullptr)
    {
        for (FBPVariableDescription& Candidate : FunctionEntry->LocalVariables)
            if (Candidate.VarGuid.ToString(EGuidFormats::Digits).ToLower() == LocalId) Local = &Candidate;
    }
    if (!TestNotNull(TEXT("function graph survives compilation"), FunctionGraph)
        || !TestNotNull(TEXT("local variable survives compilation"), Local)) return false;
    UK2Node_VariableGet* LocalGetter = NewObject<UK2Node_VariableGet>(FunctionGraph);
    LocalGetter->VariableReference.SetLocalMember(Local->VarName, FunctionGraph->GetName(), Local->VarGuid);
    LocalGetter->CreateNewGuid();
    FunctionGraph->AddNode(LocalGetter, true, false);
    LocalGetter->PostPlacedNewNode();
    LocalGetter->AllocateDefaultPins();
    UFunction* GeneratedFunction = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName()) : nullptr;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("generated function exists for call reference"), GeneratedFunction)
        || !TestNotNull(TEXT("event graph exists for call reference"), EventGraph)) return false;
    UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(EventGraph);
    Call->SetFromFunction(GeneratedFunction);
    Call->CreateNewGuid();
    EventGraph->AddNode(Call, true, false);
    Call->PostPlacedNewNode();
    Call->AllocateDefaultPins();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    const bool bDirtyBeforeReferenceRejections = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeReferenceRejections = Blueprint->Status;
    const int32 TransactionsBeforeReferenceRejections = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;

    TSharedRef<FJsonObject> UpdateUsedFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("update"));
    UpdateUsedFunction->SetStringField(TEXT("function_id"), FunctionId);
    UpdateUsedFunction->SetStringField(TEXT("field"), TEXT("signature"));
    UpdateUsedFunction->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateUsedFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), false, false, {}));
    TestFalse(TEXT("referenced function signature update rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateUsedFunction, Result, Error));
    TestEqual(TEXT("referenced signature rejection uses stable error"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("referenced signature rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> UpdateUsedLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("update"));
    UpdateUsedLocal->SetStringField(TEXT("function_id"), FunctionId);
    UpdateUsedLocal->SetStringField(TEXT("local_id"), LocalId);
    UpdateUsedLocal->SetStringField(TEXT("field"), TEXT("type"));
    UpdateUsedLocal->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateUsedLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    TestFalse(TEXT("referenced local type update rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateUsedLocal, Result, Error));
    TestEqual(TEXT("referenced local type rejection uses stable error"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("referenced local type rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveUsedFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("remove"));
    RemoveUsedFunction->SetStringField(TEXT("function_id"), FunctionId);
    RemoveUsedFunction->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced function removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveUsedFunction, Result, Error));
    TestEqual(TEXT("referenced function rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveUsedLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("remove"));
    RemoveUsedLocal->SetStringField(TEXT("function_id"), FunctionId);
    RemoveUsedLocal->SetStringField(TEXT("local_id"), LocalId);
    RemoveUsedLocal->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced local removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveUsedLocal, Result, Error));
    TestEqual(TEXT("referenced local rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestEqual(TEXT("reference rejections preserve package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeReferenceRejections);
    TestEqual(TEXT("reference rejections preserve compile state"), Blueprint->Status, StatusBeforeReferenceRejections);
    TestEqual(TEXT("reference rejections create no transactions"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeReferenceRejections);

    TestTrue(TEXT("Phase 6 Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}


#endif
