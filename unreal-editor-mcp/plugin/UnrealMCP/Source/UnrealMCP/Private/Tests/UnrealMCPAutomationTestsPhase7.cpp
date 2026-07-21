#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase7MacroAndCustomEventTest, "UnrealMCP.Phase7.MacrosAndCustomEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase7MacroAndCustomEventTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase7");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Phase 7 Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Phase 7 fixture has an event graph"), EventGraph)) return false;
    const FString EventGraphId = EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    const TArray<TSharedPtr<FJsonValue>> MacroParameters = {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Count"), TEXT("input"), K2Type(TEXT("int")),
            LiteralDefault(MakeShared<FJsonValueNumber>(2)))),
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Succeeded"), TEXT("output"), K2Type(TEXT("boolean"))))};
    TSharedRef<FJsonObject> AddMacro = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("macro"), TEXT("add"));
    AddMacro->SetStringField(TEXT("name"), TEXT("ComputeFlow"));
    AddMacro->SetObjectField(TEXT("signature"), MacroSignature(false, MacroParameters));
    const TSharedRef<FJsonObject> MacroMetadata = MakeShared<FJsonObject>();
    MacroMetadata->SetStringField(TEXT("category"), TEXT("Unreal MCP"));
    MacroMetadata->SetStringField(TEXT("tooltip"), TEXT("Runs one flow"));
    MacroMetadata->SetStringField(TEXT("keywords"), TEXT("flow compute"));
    AddMacro->SetObjectField(TEXT("metadata"), MacroMetadata);
    if (!TestTrue(TEXT("macro shell and complete signature add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString MacroId = Result->GetObjectField(TEXT("macro"))->GetStringField(TEXT("id"));
    TestEqual(TEXT("macro gets stable graph identity"), MacroId.Len(), 32);
    TestTrue(TEXT("macro required tunnels are present"),
        Result->GetObjectField(TEXT("macro"))->GetObjectField(TEXT("required_nodes"))->GetBoolField(TEXT("valid")));
    TestFalse(TEXT("impure macro reads back as impure"),
        Result->GetObjectField(TEXT("macro"))->GetObjectField(TEXT("signature"))->GetBoolField(TEXT("pure")));
    TestEqual(TEXT("macro signature reads both parameter directions"),
        Result->GetObjectField(TEXT("macro"))->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters")).Num(), 2);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> CollidingEvent = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("custom_event"), TEXT("add"));
    CollidingEvent->SetStringField(TEXT("graph_id"), EventGraphId);
    CollidingEvent->SetStringField(TEXT("name"), TEXT("ComputeFlow"));
    CollidingEvent->SetObjectField(TEXT("signature"), CustomEventSignature({}));
    TestFalse(TEXT("custom event cross-kind collision rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), CollidingEvent, Result, Error));
    TestEqual(TEXT("cross-kind rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> WrongGraphEvent = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("custom_event"), TEXT("add"));
    WrongGraphEvent->SetStringField(TEXT("graph_id"), MacroId);
    WrongGraphEvent->SetStringField(TEXT("name"), TEXT("WrongGraph"));
    WrongGraphEvent->SetObjectField(TEXT("signature"), CustomEventSignature({}));
    TestFalse(TEXT("custom event rejects a macro graph target"),
        Mutator.Execute(TEXT("blueprint_member_edit"), WrongGraphEvent, Result, Error));
    TestEqual(TEXT("wrong-graph rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> AddEvent = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("custom_event"), TEXT("add"));
    AddEvent->SetStringField(TEXT("graph_id"), EventGraphId);
    AddEvent->SetStringField(TEXT("name"), TEXT("OnThreshold"));
    AddEvent->SetObjectField(TEXT("signature"), CustomEventSignature({
        MakeShared<FJsonValueObject>(CustomEventParameter(TEXT("Threshold"), K2Type(TEXT("int")),
            LiteralDefault(MakeShared<FJsonValueNumber>(5))))}));
    const TSharedRef<FJsonObject> EventMetadata = MakeShared<FJsonObject>();
    EventMetadata->SetStringField(TEXT("category"), TEXT("Unreal MCP"));
    EventMetadata->SetStringField(TEXT("tooltip"), TEXT("Runs at a threshold"));
    EventMetadata->SetBoolField(TEXT("call_in_editor"), true);
    AddEvent->SetObjectField(TEXT("metadata"), EventMetadata);
    if (!TestTrue(TEXT("custom-event shell and signature add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddEvent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString EventId = Result->GetObjectField(TEXT("custom_event"))->GetStringField(TEXT("id"));
    TestEqual(TEXT("custom event gets stable node identity"), EventId.Len(), 32);
    TestEqual(TEXT("custom event reports its event graph"),
        Result->GetObjectField(TEXT("custom_event"))->GetObjectField(TEXT("graph_relationship"))->GetStringField(TEXT("graph_id")), EventGraphId);
    TestEqual(TEXT("custom event remains distinct local ownership"),
        Result->GetObjectField(TEXT("custom_event"))->GetStringField(TEXT("ownership")), FString(TEXT("local")));
    TestTrue(TEXT("custom-event call-in-editor metadata reads back"),
        Result->GetObjectField(TEXT("custom_event"))->GetObjectField(TEXT("metadata"))->GetBoolField(TEXT("call_in_editor")));

    const FString BeforeMacroRename = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> RenameMacro = ScopedMemberEditArguments(AssetPath, BeforeMacroRename, TEXT("macro"), TEXT("rename"));
    RenameMacro->SetStringField(TEXT("macro_id"), MacroId);
    RenameMacro->SetStringField(TEXT("new_name"), TEXT("ComputeThreshold"));
    if (!TestTrue(TEXT("macro rename succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), RenameMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString AfterMacroRename = Result->GetStringField(TEXT("snapshot_id"));
    TestEqual(TEXT("macro rename preserves stable identity"), ScopedIdByName(Inspector, AssetPath, TEXT("macros"), TEXT("ComputeThreshold")), MacroId);
    TestTrue(TEXT("macro rename transaction undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("macro undo restores snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeMacroRename);
    TestTrue(TEXT("macro rename transaction redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("macro redo restores snapshot"), InspectSnapshot(Inspector, AssetPath), AfterMacroRename);

    TSharedRef<FJsonObject> RenameEvent = ScopedMemberEditArguments(AssetPath, AfterMacroRename, TEXT("custom_event"), TEXT("rename"));
    RenameEvent->SetStringField(TEXT("custom_event_id"), EventId);
    RenameEvent->SetStringField(TEXT("new_name"), TEXT("OnThresholdReached"));
    if (!TestTrue(TEXT("custom-event rename succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), RenameEvent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("custom-event rename preserves stable identity"),
        ScopedIdByName(Inspector, AssetPath, TEXT("custom_events"), TEXT("OnThresholdReached")), EventId);

    TSharedRef<FJsonObject> UpdateMacro = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("macro"), TEXT("update"));
    UpdateMacro->SetStringField(TEXT("macro_id"), MacroId);
    UpdateMacro->SetStringField(TEXT("field"), TEXT("signature"));
    UpdateMacro->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateMacro->SetObjectField(TEXT("signature"), MacroSignature(true, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Value"), TEXT("input"), K2Type(TEXT("int"))))}));
    if (!TestTrue(TEXT("unreferenced macro signature update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("macro purity update reads back"),
        Result->GetObjectField(TEXT("macro"))->GetObjectField(TEXT("signature"))->GetBoolField(TEXT("pure")));

    TSharedRef<FJsonObject> UpdateEvent = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("custom_event"), TEXT("update"));
    UpdateEvent->SetStringField(TEXT("custom_event_id"), EventId);
    UpdateEvent->SetStringField(TEXT("field"), TEXT("signature"));
    UpdateEvent->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateEvent->SetObjectField(TEXT("signature"), CustomEventSignature({
        MakeShared<FJsonValueObject>(CustomEventParameter(TEXT("Value"), K2Type(TEXT("string")),
            LiteralDefault(MakeShared<FJsonValueString>(TEXT("ready")))))}));
    if (!TestTrue(TEXT("unreferenced custom-event signature update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateEvent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("custom-event signature update reads back"),
        Result->GetObjectField(TEXT("custom_event"))->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters"))[0]
            ->AsObject()->GetObjectField(TEXT("type"))->GetStringField(TEXT("category")), FString(TEXT("string")));

    TSharedRef<FJsonObject> AddTemporaryMacro = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("macro"), TEXT("add"));
    AddTemporaryMacro->SetStringField(TEXT("name"), TEXT("TemporaryMacro"));
    AddTemporaryMacro->SetObjectField(TEXT("signature"), MacroSignature(true, {}));
    if (!TestTrue(TEXT("temporary macro add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddTemporaryMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TemporaryMacroId = Result->GetObjectField(TEXT("macro"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> RemoveTemporaryMacro = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("macro"), TEXT("remove"));
    RemoveTemporaryMacro->SetStringField(TEXT("macro_id"), TemporaryMacroId);
    RemoveTemporaryMacro->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(TEXT("unreferenced macro removal succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RemoveTemporaryMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    TSharedRef<FJsonObject> AddTemporaryEvent = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("custom_event"), TEXT("add"));
    AddTemporaryEvent->SetStringField(TEXT("graph_id"), EventGraphId);
    AddTemporaryEvent->SetStringField(TEXT("name"), TEXT("TemporaryEvent"));
    AddTemporaryEvent->SetObjectField(TEXT("signature"), CustomEventSignature({}));
    if (!TestTrue(TEXT("temporary custom event add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddTemporaryEvent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TemporaryEventId = Result->GetObjectField(TEXT("custom_event"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> RemoveTemporaryEvent = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("custom_event"), TEXT("remove"));
    RemoveTemporaryEvent->SetStringField(TEXT("custom_event_id"), TemporaryEventId);
    RemoveTemporaryEvent->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(TEXT("unreferenced custom-event removal succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RemoveTemporaryEvent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    if (!TestEqual(TEXT("Phase 7 Blueprint compiles without errors"), Log.NumErrors, 0)) return false;
    UEdGraph* MacroGraph = nullptr;
    for (UEdGraph* Candidate : Blueprint->MacroGraphs)
        if (Candidate != nullptr && Candidate->GraphGuid.ToString(EGuidFormats::Digits).ToLower() == MacroId) MacroGraph = Candidate;
    UK2Node_CustomEvent* CustomEvent = nullptr;
    for (UEdGraphNode* Node : EventGraph->Nodes)
        if (Node != nullptr && Node->NodeGuid.ToString(EGuidFormats::Digits).ToLower() == EventId) CustomEvent = Cast<UK2Node_CustomEvent>(Node);
    if (!TestNotNull(TEXT("macro survives compilation"), MacroGraph)
        || !TestNotNull(TEXT("custom event survives compilation"), CustomEvent)) return false;
    UK2Node_MacroInstance* MacroCall = NewObject<UK2Node_MacroInstance>(EventGraph);
    MacroCall->SetMacroGraph(MacroGraph);
    MacroCall->CreateNewGuid();
    EventGraph->AddNode(MacroCall, true, false);
    MacroCall->PostPlacedNewNode();
    MacroCall->AllocateDefaultPins();
    UFunction* EventFunction = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(CustomEvent->CustomFunctionName) : nullptr;
    if (!TestNotNull(TEXT("compiled custom-event function exists"), EventFunction)) return false;
    UK2Node_CallFunction* EventCall = NewObject<UK2Node_CallFunction>(EventGraph);
    EventCall->SetFromFunction(EventFunction);
    EventCall->CreateNewGuid();
    EventGraph->AddNode(EventCall, true, false);
    EventCall->PostPlacedNewNode();
    EventCall->AllocateDefaultPins();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    TSharedRef<FJsonObject> RemoveUsedMacro = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("macro"), TEXT("remove"));
    RemoveUsedMacro->SetStringField(TEXT("macro_id"), MacroId);
    RemoveUsedMacro->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced macro removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveUsedMacro, Result, Error));
    TestEqual(TEXT("macro reference rejection is stable"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("macro reference rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveUsedEvent = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("custom_event"), TEXT("remove"));
    RemoveUsedEvent->SetStringField(TEXT("custom_event_id"), EventId);
    RemoveUsedEvent->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced custom-event removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveUsedEvent, Result, Error));
    TestEqual(TEXT("custom-event reference rejection is stable"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("custom-event reference rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TestTrue(TEXT("Phase 7 Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}


#endif
