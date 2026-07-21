#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Editor/Transactor.h"
#include "K2Node_FunctionEntry.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase11GraphNodeLifecycleTest, "UnrealMCP.Phase11.GraphNodeLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase11GraphNodeLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase11");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Phase 11 Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Phase 11 fixture has an event graph"), EventGraph)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    const FString EventGraphId = EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddFunction->SetStringField(TEXT("name"), TEXT("Phase11Function"));
    AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {}));
    if (!TestTrue(TEXT("Phase 11 function graph is added"), Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString FunctionGraphId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMacro = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("macro"), TEXT("add"));
    AddMacro->SetStringField(TEXT("name"), TEXT("Phase11Macro"));
    AddMacro->SetObjectField(TEXT("signature"), MacroSignature(false, {}));
    if (!TestTrue(TEXT("Phase 11 macro graph is added"), Mutator.Execute(TEXT("blueprint_member_edit"), AddMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString MacroGraphId = Result->GetObjectField(TEXT("macro"))->GetStringField(TEXT("id"));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("66666666666666666666666666666666"));
    FUnrealMCPBlueprintGraphEditor GraphEditor(Inspector, Catalog);
    auto GraphById = [&](const FString& Id) -> UEdGraph*
    {
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
            if (Graph != nullptr && Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower() == Id) return Graph;
        return nullptr;
    };
    auto NodeById = [&](UEdGraph* Graph, const FString& Id) -> UEdGraphNode*
    {
        if (Graph == nullptr) return nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Node != nullptr && Node->NodeGuid.ToString(EGuidFormats::Digits).ToLower() == Id) return Node;
        return nullptr;
    };
    auto CatalogAction = [&](const FString& GraphId, const FString& Family, const FString& Function, const FString& Member, bool* Pure = nullptr) -> FString
    {
        const TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
        Query->SetStringField(TEXT("asset_path"), AssetPath);
        Query->SetStringField(TEXT("graph_id"), GraphId);
        Query->SetStringField(TEXT("expected_snapshot"), InspectSnapshot(Inspector, AssetPath));
        Query->SetStringField(TEXT("node_family"), Family);
        Query->SetNumberField(TEXT("limit"), 50);
        if (!Function.IsEmpty()) Query->SetStringField(TEXT("function"), Function);
        if (!Member.IsEmpty()) Query->SetStringField(TEXT("member"), Member);
        TSharedPtr<FJsonObject> CatalogResult;
        FUnrealMCPError CatalogError;
        if (!Catalog.Execute(Query, CatalogResult, CatalogError)) return FString();
        for (const TSharedPtr<FJsonValue>& Value : CatalogResult->GetArrayField(TEXT("actions")))
        {
            const TSharedPtr<FJsonObject> Action = Value->AsObject();
            if (!Action.IsValid()) continue;
            if (Pure != nullptr && Action->HasField(TEXT("pure"))) *Pure = Action->GetBoolField(TEXT("pure"));
            return Action->GetStringField(TEXT("action_id"));
        }
        return FString();
    };
    auto EditArguments = [&](const FString& Operation, const FString& GraphId) -> TSharedRef<FJsonObject>
    {
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("expected_snapshot"), InspectSnapshot(Inspector, AssetPath));
        Arguments->SetStringField(TEXT("operation"), Operation);
        Arguments->SetStringField(TEXT("graph_id"), GraphId);
        return Arguments;
    };
    auto Position = [](int32 X, int32 Y)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("x"), X);
        Value->SetNumberField(TEXT("y"), Y);
        return Value;
    };
    auto AddAction = [&](const FString& GraphId, const FString& ActionId, int32 X, int32 Y, FString& OutNodeId) -> bool
    {
        TSharedRef<FJsonObject> Arguments = EditArguments(TEXT("add_node"), GraphId);
        Arguments->SetStringField(TEXT("action_id"), ActionId);
        Arguments->SetObjectField(TEXT("position"), Position(X, Y));
        TSharedPtr<FJsonObject> EditResult;
        FUnrealMCPError EditError;
        if (!GraphEditor.Execute(Arguments, EditResult, EditError))
        {
            AddError(EditError.Code + TEXT(": ") + EditError.Message);
            return false;
        }
        const TSharedPtr<FJsonObject> Node = EditResult->GetObjectField(TEXT("changed"))->GetObjectField(TEXT("node"));
        OutNodeId = Node->GetStringField(TEXT("id"));
        if (!Node->GetBoolField(TEXT("identity_stable")) || Node->GetIntegerField(TEXT("pin_count")) <= 0) return false;
        for (const TSharedPtr<FJsonValue>& PinValue : Node->GetArrayField(TEXT("pins")))
            if (!PinValue->AsObject()->GetBoolField(TEXT("identity_stable"))) return false;
        return true;
    };
    auto RemoveNode = [&](const FString& GraphId, const FString& NodeId) -> bool
    {
        TSharedRef<FJsonObject> Arguments = EditArguments(TEXT("remove_node"), GraphId);
        Arguments->SetStringField(TEXT("node_id"), NodeId);
        TSharedPtr<FJsonObject> EditResult;
        FUnrealMCPError EditError;
        return GraphEditor.Execute(Arguments, EditResult, EditError);
    };

    const FString SnapshotBeforeInvalid = InspectSnapshot(Inspector, AssetPath);
    const bool bDirtyBeforeInvalid = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeInvalid = Blueprint->Status;
    const int32 TransactionsBeforeInvalid = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TSharedRef<FJsonObject> InvalidAction = EditArguments(TEXT("add_node"), EventGraphId);
    InvalidAction->SetStringField(TEXT("action_id"), TEXT("ffffffffffffffffffffffffffffffff"));
    InvalidAction->SetObjectField(TEXT("position"), Position(0, 0));
    TestFalse(TEXT("unknown action identity rejects"), GraphEditor.Execute(InvalidAction, Result, Error));
    TestEqual(TEXT("unknown action uses stable error"), Error.Code, FString(TEXT("invalid_action")));
    TestEqual(TEXT("unknown action preserves snapshot"), InspectSnapshot(Inspector, AssetPath), SnapshotBeforeInvalid);
    TestEqual(TEXT("unknown action preserves dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeInvalid);
    TestEqual(TEXT("unknown action preserves compile state"), Blueprint->Status, StatusBeforeInvalid);
    TestEqual(TEXT("unknown action creates no transaction"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeInvalid);

    bool bPure = true;
    const FString VariableGetAction = CatalogAction(EventGraphId, TEXT("variable_get"), FString(), TEXT("Health"), &bPure);
    if (!TestFalse(TEXT("variable-get action exists"), VariableGetAction.IsEmpty())) return false;
    FString VariableNodeId;
    if (!TestTrue(TEXT("pure variable node is created with stable node and pin identities"),
        AddAction(EventGraphId, VariableGetAction, 100, 200, VariableNodeId))) return false;
    TestNotNull(TEXT("created variable node is live"), NodeById(EventGraph, VariableNodeId));
    TestTrue(TEXT("Undo removes the created node"), GEditor != nullptr && GEditor->UndoTransaction());
    TestNull(TEXT("created node is absent after Undo"), NodeById(EventGraph, VariableNodeId));
    TestTrue(TEXT("Redo restores the created node"), GEditor != nullptr && GEditor->RedoTransaction());
    UEdGraphNode* VariableNode = NodeById(EventGraph, VariableNodeId);
    if (!TestNotNull(TEXT("created node identity survives Redo"), VariableNode)) return false;

    const FString BeforeMoveSnapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> Move = EditArguments(TEXT("move_node"), EventGraphId);
    Move->SetStringField(TEXT("node_id"), VariableNodeId);
    Move->SetObjectField(TEXT("position"), Position(320, -160));
    TestTrue(TEXT("node movement executes"), GraphEditor.Execute(Move, Result, Error));
    TestEqual(TEXT("node x position moves exactly"), NodeById(EventGraph, VariableNodeId)->NodePosX, 320);
    TestEqual(TEXT("node y position moves exactly"), NodeById(EventGraph, VariableNodeId)->NodePosY, -160);
    TestNotEqual(TEXT("node movement changes the structural snapshot"), Result->GetStringField(TEXT("snapshot_id")), BeforeMoveSnapshot);
    TestTrue(TEXT("Undo restores node movement"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores old x position"), NodeById(EventGraph, VariableNodeId)->NodePosX, 100);
    TestTrue(TEXT("Redo restores node movement"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores moved x position"), NodeById(EventGraph, VariableNodeId)->NodePosX, 320);

    TSharedRef<FJsonObject> OutOfBounds = EditArguments(TEXT("move_node"), EventGraphId);
    OutOfBounds->SetStringField(TEXT("node_id"), VariableNodeId);
    OutOfBounds->SetObjectField(TEXT("position"), Position(UnrealMCP::MaxGraphCoordinate + 1, 0));
    TestFalse(TEXT("out-of-bounds move rejects"), GraphEditor.Execute(OutOfBounds, Result, Error));
    TestEqual(TEXT("out-of-bounds move uses invalid argument"), Error.Code, FString(TEXT("invalid_argument")));

    TSharedRef<FJsonObject> StaleMove = EditArguments(TEXT("move_node"), EventGraphId);
    StaleMove->SetStringField(TEXT("expected_snapshot"), BeforeMoveSnapshot);
    StaleMove->SetStringField(TEXT("node_id"), VariableNodeId);
    StaleMove->SetObjectField(TEXT("position"), Position(0, 0));
    TestFalse(TEXT("stale move snapshot rejects"), GraphEditor.Execute(StaleMove, Result, Error));
    TestEqual(TEXT("stale move uses stable error"), Error.Code, FString(TEXT("stale_precondition")));

    UEdGraph* FunctionGraph = GraphById(FunctionGraphId);
    if (!TestNotNull(TEXT("function graph remains available"), FunctionGraph)) return false;
    UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(FunctionGraph));
    if (!TestNotNull(TEXT("function graph has a protected entry"), Entry)) return false;
    TSharedRef<FJsonObject> ProtectedRemove = EditArguments(TEXT("remove_node"), FunctionGraphId);
    ProtectedRemove->SetStringField(TEXT("node_id"), Entry->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
    TestFalse(TEXT("protected signature-node deletion rejects"), GraphEditor.Execute(ProtectedRemove, Result, Error));
    TestEqual(TEXT("protected deletion uses stable error"), Error.Code, FString(TEXT("protected_node")));

    TSharedRef<FJsonObject> MissingNode = EditArguments(TEXT("remove_node"), EventGraphId);
    MissingNode->SetStringField(TEXT("node_id"), TEXT("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));
    TestFalse(TEXT("stale node identity rejects"), GraphEditor.Execute(MissingNode, Result, Error));
    TestEqual(TEXT("stale node uses stable error"), Error.Code, FString(TEXT("invalid_node")));

    FUnrealMCPBlueprintGraphEditor FailingEditor(
        Inspector, Catalog,
        [](const FString&, UBlueprint*, UEdGraph*, const FString&, const FString&, const FString&,
            FUnrealMCPBlueprintActionCatalog::FResolvedAction&, FUnrealMCPError&) { return true; },
        [](const FUnrealMCPBlueprintActionCatalog::FResolvedAction&, UEdGraph*, const FVector2D&) -> UEdGraphNode* { return nullptr; });
    const FString BeforeFailureSnapshot = InspectSnapshot(Inspector, AssetPath);
    const bool bDirtyBeforeFailure = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeFailure = Blueprint->Status;
    const int32 TransactionsBeforeFailure = GEditor->Trans->GetQueueLength();
    TSharedRef<FJsonObject> SpawnerFailure = EditArguments(TEXT("add_node"), EventGraphId);
    SpawnerFailure->SetStringField(TEXT("action_id"), TEXT("dddddddddddddddddddddddddddddddd"));
    SpawnerFailure->SetObjectField(TEXT("position"), Position(0, 0));
    TestFalse(TEXT("spawner failure rejects"), FailingEditor.Execute(SpawnerFailure, Result, Error));
    TestEqual(TEXT("spawner failure uses stable error"), Error.Code, FString(TEXT("invalid_action")));
    TestEqual(TEXT("spawner failure preserves snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeFailureSnapshot);
    TestEqual(TEXT("spawner failure preserves dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeFailure);
    TestEqual(TEXT("spawner failure preserves compile state"), Blueprint->Status, StatusBeforeFailure);
    TestEqual(TEXT("spawner failure preserves transaction history"), GEditor->Trans->GetQueueLength(), TransactionsBeforeFailure);

    FUnrealMCPBlueprintGraphEditor ExistingEditor(
        Inspector, Catalog,
        [](const FString&, UBlueprint*, UEdGraph*, const FString&, const FString&, const FString&,
            FUnrealMCPBlueprintActionCatalog::FResolvedAction&, FUnrealMCPError&) { return true; },
        [VariableNode](const FUnrealMCPBlueprintActionCatalog::FResolvedAction&, UEdGraph*, const FVector2D&) { return VariableNode; });
    TSharedRef<FJsonObject> ExistingAction = EditArguments(TEXT("add_node"), EventGraphId);
    ExistingAction->SetStringField(TEXT("action_id"), TEXT("cccccccccccccccccccccccccccccccc"));
    ExistingAction->SetObjectField(TEXT("position"), Position(900, 900));
    TestTrue(TEXT("returned-existing action is detected"), ExistingEditor.Execute(ExistingAction, Result, Error));
    TestTrue(TEXT("returned-existing result is explicit"), Result->GetObjectField(TEXT("changed"))->GetBoolField(TEXT("returned_existing")));
    TestFalse(TEXT("returned-existing result is not reported as created"), Result->GetObjectField(TEXT("changed"))->GetBoolField(TEXT("created")));
    TestEqual(TEXT("returned-existing node is not repositioned"), NodeById(EventGraph, VariableNodeId)->NodePosX, 320);

    for (const TPair<FString, FString>& Target : {
        TPair<FString, FString>(FunctionGraphId, TEXT("function")),
        TPair<FString, FString>(MacroGraphId, TEXT("macro"))})
    {
        const FString LiteralAction = CatalogAction(Target.Key, TEXT("literal"), TEXT("MakeLiteralInt"), FString());
        if (!TestFalse(*FString::Printf(TEXT("literal catalogs in %s graph"), *Target.Value), LiteralAction.IsEmpty())) return false;
        FString NodeId;
        if (!TestTrue(*FString::Printf(TEXT("node creates in %s graph"), *Target.Value), AddAction(Target.Key, LiteralAction, 40, 80, NodeId))) return false;
        TestTrue(*FString::Printf(TEXT("node removes from %s graph"), *Target.Value), RemoveNode(Target.Key, NodeId));
    }

    const FString ImpureAction = CatalogAction(EventGraphId, TEXT("function_call"), TEXT("PrintString"), FString(), &bPure);
    if (!TestFalse(TEXT("impure function action catalogs"), ImpureAction.IsEmpty())) return false;
    TestFalse(TEXT("impure function metadata is preserved"), bPure);
    FString ImpureNodeId;
    if (!TestTrue(TEXT("impure function node creates"), AddAction(EventGraphId, ImpureAction, 500, 200, ImpureNodeId))) return false;
    TestTrue(TEXT("impure function node removes"), RemoveNode(EventGraphId, ImpureNodeId));

    const FString VariableSetAction = CatalogAction(EventGraphId, TEXT("variable_set"), FString(), TEXT("Health"));
    if (!TestFalse(TEXT("variable-set action catalogs"), VariableSetAction.IsEmpty())) return false;
    FString VariableSetNodeId;
    if (!TestTrue(TEXT("variable-set node creates"), AddAction(EventGraphId, VariableSetAction, 600, 200, VariableSetNodeId))) return false;
    TestTrue(TEXT("variable-set node removes"), RemoveNode(EventGraphId, VariableSetNodeId));

    const FString EventAction = CatalogAction(EventGraphId, TEXT("event"), FString(), FString());
    if (!TestFalse(TEXT("unique-event action catalogs"), EventAction.IsEmpty())) return false;
    FString EventNodeId;
    if (!TestTrue(TEXT("unique-event node creates"), AddAction(EventGraphId, EventAction, 700, 200, EventNodeId))) return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestNotNull(TEXT("unique-event identity survives compilation"), NodeById(EventGraph, EventNodeId));
    TestTrue(TEXT("unique-event node removes safely"), RemoveNode(EventGraphId, EventNodeId));

    TestTrue(TEXT("primary variable node removes"), RemoveNode(EventGraphId, VariableNodeId));
    TestNull(TEXT("removed node is absent"), NodeById(EventGraph, VariableNodeId));
    TestTrue(TEXT("Undo restores removed node"), GEditor->UndoTransaction());
    TestNotNull(TEXT("removed node identity survives Undo"), NodeById(EventGraph, VariableNodeId));
    TestTrue(TEXT("Redo removes node again"), GEditor->RedoTransaction());
    TestNull(TEXT("removed node is absent after Redo"), NodeById(EventGraph, VariableNodeId));

    Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
    TestTrue(TEXT("Phase 11 Blueprint compiles"), Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error));
    TestTrue(TEXT("Phase 11 compile succeeds"), Result->GetBoolField(TEXT("compile_succeeded")));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Save = AssetArguments(AssetPath);
    Save->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Snapshot);
    TestTrue(TEXT("Phase 11 Blueprint saves"), Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error));
    TestTrue(TEXT("Phase 11 save reports success"), Result->GetBoolField(TEXT("saved")));
    return true;
}


#endif
