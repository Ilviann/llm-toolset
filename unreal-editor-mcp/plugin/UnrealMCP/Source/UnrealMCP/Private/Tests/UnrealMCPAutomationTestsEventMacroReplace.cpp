#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "BlueprintActionDatabase.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Tunnel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPEventMacroReplaceTest,
    "UnrealMCP.EventMacroReplace.LogicUnitsAndExternalLinks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPEventMacroReplaceTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    namespace LogicUnit = UnrealMCP::BlueprintLogicUnitFingerprint;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_EventMacroReplace");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("logic-unit replacement Blueprint is created"), Blueprint)) return false;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("event graph exists"), EventGraph)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    const FString EventGraphId = EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddShared = MemberEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddShared->SetStringField(TEXT("name"), TEXT("SharedCondition"));
    AddShared->SetObjectField(TEXT("type"), K2Type(TEXT("boolean")));
    if (!TestTrue(TEXT("shared boundary variable is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddShared, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString SharedId = MemberIdByName(Inspector, AssetPath, TEXT("SharedCondition"));
    FBPVariableDescription* SharedVariable = nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
        if (Variable.VarGuid.ToString(EGuidFormats::Digits).ToLower() == SharedId) SharedVariable = &Variable;
    if (!TestNotNull(TEXT("shared boundary variable is live"), SharedVariable)) return false;

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMacro = ScopedMemberEditArguments(
        AssetPath, Snapshot, TEXT("macro"), TEXT("add"));
    AddMacro->SetStringField(TEXT("name"), TEXT("ReplaceableMacro"));
    AddMacro->SetObjectField(TEXT("signature"), MacroSignature(false, {}));
    if (!TestTrue(TEXT("replaceable macro is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString MacroId = Result->GetObjectField(TEXT("macro"))->GetStringField(TEXT("id"));
    UEdGraph* MacroGraph = nullptr;
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
        if (Graph != nullptr && Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower() == MacroId)
            MacroGraph = Graph;
    if (!TestNotNull(TEXT("replaceable macro graph is live"), MacroGraph)) return false;

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddCustomEvent = ScopedMemberEditArguments(
        AssetPath, Snapshot, TEXT("custom_event"), TEXT("add"));
    AddCustomEvent->SetStringField(TEXT("graph_id"), EventGraphId);
    AddCustomEvent->SetStringField(TEXT("name"), TEXT("ReplaceableCustomEvent"));
    AddCustomEvent->SetObjectField(TEXT("signature"), CustomEventSignature({}));
    if (!TestTrue(TEXT("replaceable custom event is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddCustomEvent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString CustomEventId =
        Result->GetObjectField(TEXT("custom_event"))->GetStringField(TEXT("id"));
    UK2Node_CustomEvent* CustomEvent = nullptr;
    for (UEdGraphNode* Node : EventGraph->Nodes)
        if (Node != nullptr && Node->NodeGuid.ToString(EGuidFormats::Digits).ToLower() == CustomEventId)
            CustomEvent = Cast<UK2Node_CustomEvent>(Node);
    if (!TestNotNull(TEXT("replaceable custom event is live"), CustomEvent)) return false;

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FBlueprintActionDatabase::Get().RefreshAll();
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"));
    auto CatalogAction = [&](UEdGraph* Graph, const FString& CurrentSnapshot,
        const FString& Family, const FString& Text) -> FString
    {
        const TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
        Query->SetStringField(TEXT("asset_path"), AssetPath);
        Query->SetStringField(TEXT("graph_id"), Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower());
        Query->SetStringField(TEXT("expected_snapshot"), CurrentSnapshot);
        Query->SetStringField(TEXT("node_family"), Family);
        if (!Text.IsEmpty()) Query->SetStringField(TEXT("text"), Text);
        Query->SetNumberField(TEXT("limit"), 50);
        TSharedPtr<FJsonObject> CatalogResult;
        FUnrealMCPError CatalogError;
        if (!Catalog.Execute(Query, CatalogResult, CatalogError)) return FString();
        for (const TSharedPtr<FJsonValue>& Value : CatalogResult->GetArrayField(TEXT("actions")))
        {
            const TSharedPtr<FJsonObject> Action = Value->AsObject();
            if (Action.IsValid() && (Text.IsEmpty()
                || Action->GetStringField(TEXT("title")).Contains(Text, ESearchCase::IgnoreCase)))
                return Action->GetStringField(TEXT("action_id"));
        }
        return FString();
    };

    const FString NativeEventAction = CatalogAction(EventGraph, Snapshot, TEXT("event"), FString());
    if (!TestFalse(TEXT("native event action is retained"), NativeEventAction.IsEmpty())) return false;
    FUnrealMCPBlueprintActionCatalog::FResolvedAction ResolvedEvent;
    if (!TestTrue(TEXT("native event action resolves"), Catalog.ResolveForInvocation(
        NativeEventAction, Blueprint, EventGraph, AssetPath, EventGraphId, Snapshot,
        ResolvedEvent, Error))) return false;
    UEdGraphNode* NativeEventNode = ResolvedEvent.Spawner != nullptr
        ? ResolvedEvent.Spawner->Invoke(EventGraph, ResolvedEvent.Bindings, FVector2D(-640, 400)) : nullptr;
    UK2Node_Event* NativeEvent = Cast<UK2Node_Event>(NativeEventNode);
    if (!TestNotNull(TEXT("native event root is created"), NativeEvent)
        || !TestFalse(TEXT("native event root is not a custom event"), NativeEvent->IsA<UK2Node_CustomEvent>()))
        return false;
    if (!NativeEvent->NodeGuid.IsValid()) NativeEvent->CreateNewGuid();

    auto AddBranch = [](UEdGraph* Graph, int32 X, int32 Y)
    {
        UK2Node_IfThenElse* Branch = NewObject<UK2Node_IfThenElse>(Graph);
        Graph->AddNode(Branch, true, false);
        Branch->CreateNewGuid();
        Branch->AllocateDefaultPins();
        Branch->NodePosX = X;
        Branch->NodePosY = Y;
        return Branch;
    };
    const UEdGraphSchema_K2* EventSchema = CastChecked<UEdGraphSchema_K2>(EventGraph->GetSchema());
    const UEdGraphSchema_K2* MacroSchema = CastChecked<UEdGraphSchema_K2>(MacroGraph->GetSchema());
    UK2Node_Tunnel* MacroEntry = nullptr;
    UK2Node_Tunnel* MacroExit = nullptr;
    bool bMacroPure = false;
    FKismetEditorUtilities::GetInformationOnMacro(MacroGraph, MacroEntry, MacroExit, bMacroPure);
    UK2Node_IfThenElse* OldMacroBranch = AddBranch(MacroGraph, 0, 0);
    UK2Node_IfThenElse* OldCustomBranch = AddBranch(EventGraph, 0, 200);
    UK2Node_IfThenElse* OldNativeBranch = AddBranch(EventGraph, 0, 400);
    UK2Node_IfThenElse* UnrelatedBranch = AddBranch(EventGraph, 600, 600);
    UK2Node_VariableGet* SharedGetter = NewObject<UK2Node_VariableGet>(EventGraph);
    SharedGetter->VariableReference.SetSelfMember(SharedVariable->VarName, SharedVariable->VarGuid);
    SharedGetter->CreateNewGuid();
    EventGraph->AddNode(SharedGetter, true, false);
    SharedGetter->PostPlacedNewNode();
    SharedGetter->AllocateDefaultPins();
    SharedGetter->NodePosX = -300;
    SharedGetter->NodePosY = 200;
    if (!TestTrue(TEXT("macro entry connects to old body"), MacroSchema->TryCreateConnection(
            MacroEntry->FindPin(UEdGraphSchema_K2::PN_Execute), OldMacroBranch->GetExecPin()))
        || !TestTrue(TEXT("old macro body connects to exit"), MacroSchema->TryCreateConnection(
            OldMacroBranch->GetThenPin(), MacroExit->FindPin(UEdGraphSchema_K2::PN_Then)))
        || !TestTrue(TEXT("custom event connects to old body"), EventSchema->TryCreateConnection(
            CustomEvent->FindPin(UEdGraphSchema_K2::PN_Then), OldCustomBranch->GetExecPin()))
        || !TestTrue(TEXT("native event connects to old body"), EventSchema->TryCreateConnection(
            NativeEvent->FindPin(UEdGraphSchema_K2::PN_Then), OldNativeBranch->GetExecPin()))
        || !TestTrue(TEXT("shared data enters custom body"), EventSchema->TryCreateConnection(
            SharedGetter->GetValuePin(), OldCustomBranch->GetConditionPin()))
        || !TestTrue(TEXT("shared data also remains unrelated"), EventSchema->TryCreateConnection(
            SharedGetter->GetValuePin(), UnrelatedBranch->GetConditionPin()))) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FBlueprintActionDatabase::Get().RefreshAll();

    auto Position = [](int32 X, int32 Y)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("x"), X);
        Value->SetNumberField(TEXT("y"), Y);
        return Value;
    };
    auto InternalEndpoint = [](const FString& Key, const FString& Pin)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("node_key"), Key);
        Value->SetStringField(TEXT("pin_name"), Pin);
        return Value;
    };
    auto InternalConnection = [&](const FString& FromKey, const FString& FromPin,
        const FString& ToKey, const FString& ToPin)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetObjectField(TEXT("from"), InternalEndpoint(FromKey, FromPin));
        Value->SetObjectField(TEXT("to"), InternalEndpoint(ToKey, ToPin));
        return Value;
    };
    auto IdArray = [](const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FString& Value : Values) Result.Add(MakeShared<FJsonValueString>(Value));
        return Result;
    };
    auto BuildArguments = [&](const FString& TargetKind, UEdGraph* Graph, UEdGraphNode* Root,
        const LogicUnit::FBoundary& Boundary, const FString& ActionId, bool bExternal)
    {
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("operation_id"),
            FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("expected_snapshot"), InspectSnapshot(Inspector, AssetPath));
        Arguments->SetStringField(TEXT("target_kind"), TargetKind);
        Arguments->SetStringField(TEXT("logic_unit_id"), TargetKind == TEXT("macro")
            ? Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower()
            : Root->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("graph_id"), Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("expected_logic_unit_fingerprint"), Boundary.Fingerprint);
        Arguments->SetStringField(TEXT("entry_node_id"),
            Boundary.Entry->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetArrayField(TEXT("owned_node_ids"), IdArray(Boundary.OwnedNodeIds));
        Arguments->SetArrayField(TEXT("local_variable_ids"), {});
        Arguments->SetObjectField(TEXT("entry_position"), Position(-320, Root->NodePosY));
        if (Boundary.Result != nullptr)
        {
            Arguments->SetStringField(TEXT("result_node_id"),
                Boundary.Result->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
            Arguments->SetObjectField(TEXT("result_position"), Position(640, Root->NodePosY));
        }
        const TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("key"), TEXT("branch"));
        Node->SetStringField(TEXT("action_id"), ActionId);
        Node->SetObjectField(TEXT("position"), Position(0, Root->NodePosY));
        Arguments->SetArrayField(TEXT("nodes"), {MakeShared<FJsonValueObject>(Node)});
        Arguments->SetArrayField(TEXT("pin_defaults"), {});
        const FString RootExecPin = TargetKind == TEXT("macro") ? TEXT("execute") : TEXT("then");
        TArray<TSharedPtr<FJsonValue>> Connections{
            MakeShared<FJsonValueObject>(InternalConnection(
                TEXT("$entry"), RootExecPin, TEXT("branch"), TEXT("execute")))};
        if (Boundary.Result != nullptr)
            Connections.Add(MakeShared<FJsonValueObject>(InternalConnection(
                TEXT("branch"), TEXT("then"), TEXT("$result"), TEXT("then"))));
        Arguments->SetArrayField(TEXT("connections"), Connections);
        TArray<TSharedPtr<FJsonValue>> ExternalConnections;
        if (bExternal)
        {
            const TSharedRef<FJsonObject> External = MakeShared<FJsonObject>();
            const TSharedRef<FJsonObject> From = MakeShared<FJsonObject>();
            From->SetStringField(TEXT("node_id"),
                SharedGetter->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
            From->SetStringField(TEXT("pin_id"),
                SharedGetter->GetValuePin()->PinId.ToString(EGuidFormats::Digits).ToLower());
            External->SetObjectField(TEXT("from"), From);
            External->SetObjectField(TEXT("to"), InternalEndpoint(TEXT("branch"), TEXT("Condition")));
            ExternalConnections.Add(MakeShared<FJsonValueObject>(External));
        }
        Arguments->SetArrayField(TEXT("external_connections"), ExternalConnections);
        return Arguments;
    };

    FUnrealMCPBlueprintBlockReplacementService Service(Inspector, Catalog);
    auto Replace = [&](const FString& TargetKind, UEdGraph* Graph, UEdGraphNode* Root, bool bExternal)
    {
        const FString CurrentSnapshot = InspectSnapshot(Inspector, AssetPath);
        FBlueprintActionDatabase::Get().RefreshAll();
        const FString ActionId = CatalogAction(Graph, CurrentSnapshot, TEXT("flow_control"), TEXT("Branch"));
        if (ActionId.IsEmpty()) return false;
        LogicUnit::FBoundary Boundary;
        const bool bDescribed = TargetKind == TEXT("macro")
            ? LogicUnit::DescribeMacro(Graph, Boundary)
            : LogicUnit::DescribeEventHandler(Graph, Root, Boundary);
        if (!bDescribed) return false;
        if (bExternal && Boundary.ExternalLinks.Num() != 1) return false;
        TSharedRef<FJsonObject> Arguments = BuildArguments(
            TargetKind, Graph, Root, Boundary, ActionId, bExternal);
        if (!Service.Execute(Arguments, Result, Error))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        return Result->GetStringField(TEXT("target_kind")) == TargetKind
            && Result->GetObjectField(TEXT("changed"))->GetIntegerField(TEXT("created_node_count")) == 1;
    };

    if (!TestTrue(TEXT("complete macro replacement succeeds"),
            Replace(TEXT("macro"), MacroGraph, MacroEntry, false))
        || !TestTrue(TEXT("custom-event replacement with declared external data succeeds"),
            Replace(TEXT("custom_event"), EventGraph, CustomEvent, true))
        || !TestTrue(TEXT("native event-rooted replacement succeeds"),
            Replace(TEXT("event"), EventGraph, NativeEvent, false))) return false;
    TestTrue(TEXT("unrelated shared-data consumer is preserved"),
        SharedGetter->GetValuePin()->LinkedTo.Contains(UnrelatedBranch->GetConditionPin()));
    TestNotNull(TEXT("shared getter is preserved"), SharedGetter->GetGraph());
    TestNotNull(TEXT("unrelated branch is preserved"), UnrelatedBranch->GetGraph());

    FCompilerResultsLog CompileLog;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);
    TestEqual(TEXT("all replaced logic units compile"), CompileLog.NumErrors, 0);
    return true;
}

#endif
