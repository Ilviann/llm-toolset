#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "BlueprintActionDatabase.h"
#include "Editor/Transactor.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionResult.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPFunctionReplaceTest,
    "UnrealMCP.FunctionReplace.PreflightTransactionPreservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPFunctionReplaceTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_FunctionReplace");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("function replacement Blueprint is created"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddFunction =
        ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddFunction->SetStringField(TEXT("name"), TEXT("ReplaceableWork"));
    AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(
        TEXT("public"), false, false, {
            MakeShared<FJsonValueObject>(FunctionParameter(
                TEXT("Enabled"), TEXT("input"), K2Type(TEXT("boolean")))),
            MakeShared<FJsonValueObject>(FunctionParameter(
                TEXT("Value"), TEXT("output"), K2Type(TEXT("int")))),
        }));
    if (!TestTrue(TEXT("replaceable function is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString FunctionId =
        Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    UEdGraph* FunctionGraph = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (Graph != nullptr
            && Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower() == FunctionId)
            FunctionGraph = Graph;
    if (!TestNotNull(TEXT("replaceable function graph exists"), FunctionGraph)) return false;

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddLocal =
        ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("add"));
    AddLocal->SetStringField(TEXT("function_id"), FunctionId);
    AddLocal->SetStringField(TEXT("name"), TEXT("Accumulator"));
    AddLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    AddLocal->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueNumber>(1)));
    if (!TestTrue(TEXT("replaceable function local is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddLocal, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }

    UK2Node_IfThenElse* OldBranch = NewObject<UK2Node_IfThenElse>(FunctionGraph);
    FunctionGraph->AddNode(OldBranch, false, false);
    OldBranch->CreateNewGuid();
    OldBranch->AllocateDefaultPins();
    OldBranch->NodePosX = 120;
    OldBranch->NodePosY = 240;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FBlueprintActionDatabase::Get().RefreshAll();
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    FUnrealMCPBlueprintActionCatalog Catalog(
        Inspector, TEXT("abababababababababababababababab"));
    auto BranchAction = [&](const FString& CurrentSnapshot) -> FString
    {
        const TSharedRef<FJsonObject> Query = MakeShared<FJsonObject>();
        Query->SetStringField(TEXT("asset_path"), AssetPath);
        Query->SetStringField(TEXT("graph_id"), FunctionId);
        Query->SetStringField(TEXT("expected_snapshot"), CurrentSnapshot);
        Query->SetStringField(TEXT("node_family"), TEXT("flow_control"));
        Query->SetStringField(TEXT("text"), TEXT("Branch"));
        Query->SetNumberField(TEXT("limit"), 50);
        TSharedPtr<FJsonObject> CatalogResult;
        FUnrealMCPError CatalogError;
        if (!Catalog.Execute(Query, CatalogResult, CatalogError)) return FString();
        for (const TSharedPtr<FJsonValue>& Value : CatalogResult->GetArrayField(TEXT("actions")))
        {
            const TSharedPtr<FJsonObject> Action = Value->AsObject();
            if (Action.IsValid() && Action->GetStringField(TEXT("title")).Equals(
                TEXT("Branch"), ESearchCase::IgnoreCase))
                return Action->GetStringField(TEXT("action_id"));
        }
        return FString();
    };
    const FString ActionId = BranchAction(Snapshot);
    if (!TestFalse(TEXT("context-valid Branch action is retained"), ActionId.IsEmpty())) return false;

    auto Position = [](int32 X, int32 Y)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetNumberField(TEXT("x"), X);
        Value->SetNumberField(TEXT("y"), Y);
        return Value;
    };
    auto Endpoint = [](const FString& NodeKey, const FString& PinName)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("node_key"), NodeKey);
        Value->SetStringField(TEXT("pin_name"), PinName);
        return Value;
    };
    auto Connection = [&](const FString& FromKey, const FString& FromPin,
        const FString& ToKey, const FString& ToPin)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetObjectField(TEXT("from"), Endpoint(FromKey, FromPin));
        Value->SetObjectField(TEXT("to"), Endpoint(ToKey, ToPin));
        return Value;
    };
    auto ReplacementArguments = [&](const FString& CurrentSnapshot, const FString& CurrentAction)
    {
        UnrealMCP::BlueprintLogicUnitFingerprint::FBoundary Boundary;
        check(UnrealMCP::BlueprintLogicUnitFingerprint::DescribeFunction(FunctionGraph, Boundary));
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("operation_id"),
            FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("expected_snapshot"), CurrentSnapshot);
        Arguments->SetStringField(TEXT("function_id"), FunctionId);
        Arguments->SetStringField(TEXT("expected_function_fingerprint"), Boundary.Fingerprint);
        Arguments->SetStringField(TEXT("entry_node_id"),
            Boundary.Entry->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("result_node_id"),
            Boundary.Result->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
        TArray<TSharedPtr<FJsonValue>> Owned;
        for (const FString& Id : Boundary.OwnedNodeIds)
            Owned.Add(MakeShared<FJsonValueString>(Id));
        Arguments->SetArrayField(TEXT("owned_node_ids"), Owned);
        TArray<TSharedPtr<FJsonValue>> Locals;
        for (const FString& Id : Boundary.LocalVariableIds)
            Locals.Add(MakeShared<FJsonValueString>(Id));
        Arguments->SetArrayField(TEXT("local_variable_ids"), Locals);
        Arguments->SetObjectField(TEXT("entry_position"), Position(-320, 0));
        Arguments->SetObjectField(TEXT("result_position"), Position(640, 0));
        TArray<TSharedPtr<FJsonValue>> Nodes;
        if (!CurrentAction.IsEmpty())
        {
            const TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
            Node->SetStringField(TEXT("key"), TEXT("branch"));
            Node->SetStringField(TEXT("action_id"), CurrentAction);
            Node->SetObjectField(TEXT("position"), Position(0, 0));
            Nodes.Add(MakeShared<FJsonValueObject>(Node));
        }
        Arguments->SetArrayField(TEXT("nodes"), Nodes);
        TArray<TSharedPtr<FJsonValue>> Defaults;
        const TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
        Default->SetObjectField(TEXT("endpoint"), Endpoint(TEXT("$result"), TEXT("Value")));
        Default->SetObjectField(TEXT("value"), LiteralDefault(MakeShared<FJsonValueNumber>(7)));
        Defaults.Add(MakeShared<FJsonValueObject>(Default));
        Arguments->SetArrayField(TEXT("pin_defaults"), Defaults);
        TArray<TSharedPtr<FJsonValue>> Connections;
        if (!CurrentAction.IsEmpty())
        {
            Connections.Add(MakeShared<FJsonValueObject>(
                Connection(TEXT("$entry"), TEXT("then"), TEXT("branch"), TEXT("execute"))));
            Connections.Add(MakeShared<FJsonValueObject>(
                Connection(TEXT("$entry"), TEXT("Enabled"), TEXT("branch"), TEXT("Condition"))));
            Connections.Add(MakeShared<FJsonValueObject>(
                Connection(TEXT("branch"), TEXT("then"), TEXT("$result"), TEXT("execute"))));
        }
        Arguments->SetArrayField(TEXT("connections"), Connections);
        return Arguments;
    };

    TSharedRef<FJsonObject> Replace = ReplacementArguments(Snapshot, ActionId);
    const FString OldNodeId = OldBranch->NodeGuid.ToString(EGuidFormats::Digits).ToLower();
    const int32 TransactionsBefore = GEditor->Trans->GetQueueLength();
    FUnrealMCPBlueprintBlockReplacementService Service(Inspector, Catalog);
    if (!TestTrue(TEXT("compiled function replacement succeeds"),
        Service.Execute(Replace, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(TEXT("replacement uses one live transaction"),
        GEditor->Trans->GetQueueLength(), TransactionsBefore + 1);
    TestTrue(TEXT("scratch preflight is reported"),
        Result->GetObjectField(TEXT("changed"))->GetBoolField(TEXT("scratch_preflight")));
    TestTrue(TEXT("candidate compile is reported"),
        Result->GetObjectField(TEXT("changed"))->GetBoolField(TEXT("compile_succeeded")));
    TestEqual(TEXT("one old body node is removed"),
        Result->GetObjectField(TEXT("changed"))->GetIntegerField(TEXT("removed_node_count")), 1);
    TestEqual(TEXT("one planned body node is created"),
        Result->GetObjectField(TEXT("changed"))->GetIntegerField(TEXT("created_node_count")), 1);
    bool bOldNodeFound = false;
    for (UEdGraphNode* Node : FunctionGraph->Nodes)
        bOldNodeFound |= Node != nullptr
            && Node->NodeGuid.ToString(EGuidFormats::Digits).ToLower() == OldNodeId;
    TestFalse(TEXT("old body identity is absent"), bOldNodeFound);

    const FString ReplacedSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestTrue(TEXT("Undo restores the exact prior function"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores the prior snapshot"),
        InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestTrue(TEXT("Redo reapplies the complete function replacement"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores the replaced snapshot"),
        InspectSnapshot(Inspector, AssetPath), ReplacedSnapshot);

    const int32 TransactionsBeforeStale = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("stale complete boundary rejects"),
        Service.Execute(Replace, Result, Error));
    TestEqual(TEXT("stale boundary uses stable error"),
        Error.Code, FString(TEXT("stale_precondition")));
    TestEqual(TEXT("stale boundary creates no transaction"),
        GEditor->Trans->GetQueueLength(), TransactionsBeforeStale);

    const FString BeforeCompileFailure = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> CompileFailureArguments =
        ReplacementArguments(BeforeCompileFailure, FString());
    FUnrealMCPBlueprintBlockReplacementService CompileFailingService(
        Inspector, Catalog,
        [](UBlueprint*, FCompilerResultsLog& Log)
        {
            Log.Error(TEXT("Injected candidate compile failure"));
        });
    const int32 TransactionsBeforeCompileFailure = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("candidate compile failure rejects before live mutation"),
        CompileFailingService.Execute(CompileFailureArguments, Result, Error));
    TestEqual(TEXT("candidate compile failure is explicit"),
        Error.Code, FString(TEXT("compile_failed")));
    TestEqual(TEXT("candidate compile failure creates no transaction"),
        GEditor->Trans->GetQueueLength(), TransactionsBeforeCompileFailure);
    TestEqual(TEXT("candidate compile failure preserves live snapshot"),
        InspectSnapshot(Inspector, AssetPath), BeforeCompileFailure);

    const FString FailureAction = BranchAction(BeforeCompileFailure);
    if (!TestFalse(TEXT("fresh failure-path action is retained"), FailureAction.IsEmpty()))
        return false;
    TSharedRef<FJsonObject> LiveFailureArguments =
        ReplacementArguments(BeforeCompileFailure, FailureAction);
    int32 InvocationCount = 0;
    FUnrealMCPBlueprintBlockReplacementService LiveFailingService(
        Inspector, Catalog, {},
        [&InvocationCount](
            const FUnrealMCPBlueprintActionCatalog::FResolvedAction& Action,
            UEdGraph* Graph,
            const FVector2D& Position) -> UEdGraphNode*
        {
            ++InvocationCount;
            return InvocationCount == 1 && Action.Spawner != nullptr
                ? Action.Spawner->Invoke(Graph, Action.Bindings, Position) : nullptr;
        });
    const bool bDirtyBeforeFailure = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeFailure = Blueprint->Status;
    TestFalse(TEXT("unexpected live spawn failure rejects"),
        LiveFailingService.Execute(LiveFailureArguments, Result, Error));
    TestEqual(TEXT("live failure restores exact snapshot"),
        InspectSnapshot(Inspector, AssetPath), BeforeCompileFailure);
    TestEqual(TEXT("live failure restores prior dirty state"),
        Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeFailure);
    TestEqual(TEXT("live failure restores prior compile state"),
        Blueprint->Status, StatusBeforeFailure);

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("restored replacement remains compilable"),
        Blueprint->Status != BS_Error);
    TestTrue(TEXT("replaced Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}

#endif
