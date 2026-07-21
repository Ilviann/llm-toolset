#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase8ActionCatalogTest, "UnrealMCP.Phase8.ActionCatalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase8ActionCatalogTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase8");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Phase 8 Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Phase 8 fixture has an event graph"), EventGraph)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    const FString EventGraphId = EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddImpure = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddImpure->SetStringField(TEXT("name"), TEXT("RunCatalogWork"));
    AddImpure->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {}));
    if (!TestTrue(TEXT("impure catalog fixture function is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddImpure, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString FunctionGraphId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddPure = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddPure->SetStringField(TEXT("name"), TEXT("ReadCatalogValue"));
    AddPure->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), true, true, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Value"), TEXT("output"), K2Type(TEXT("int"))))}));
    if (!TestTrue(TEXT("pure catalog fixture function is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddPure, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMacro = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("macro"), TEXT("add"));
    AddMacro->SetStringField(TEXT("name"), TEXT("CatalogMacro"));
    AddMacro->SetObjectField(TEXT("signature"), MacroSignature(true, {}));
    if (!TestTrue(TEXT("catalog fixture macro is added"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString MacroGraphId = Result->GetObjectField(TEXT("macro"))->GetStringField(TEXT("id"));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    double Clock = 100.0;
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("11111111111111111111111111111111"), [&Clock] { return Clock; });
    auto CatalogArguments = [&](const FString& Family, const FString& Function, const FString& Member, int32 Limit = 20)
    {
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("graph_id"), EventGraphId);
        Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
        if (!Family.IsEmpty()) Arguments->SetStringField(TEXT("node_family"), Family);
        if (!Function.IsEmpty()) Arguments->SetStringField(TEXT("function"), Function);
        if (!Member.IsEmpty()) Arguments->SetStringField(TEXT("member"), Member);
        Arguments->SetNumberField(TEXT("limit"), Limit);
        return Arguments;
    };
    auto FirstAction = [](const TSharedPtr<FJsonObject>& CatalogResult) -> TSharedPtr<FJsonObject>
    {
        if (!CatalogResult.IsValid()) return nullptr;
        const TArray<TSharedPtr<FJsonValue>>& Actions = CatalogResult->GetArrayField(TEXT("actions"));
        return !Actions.IsEmpty() ? Actions[0]->AsObject() : nullptr;
    };

    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    const int32 TransactionsBefore = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    const int32 SelectedObjectsBefore = GEditor != nullptr && GEditor->GetSelectedObjects() != nullptr ? GEditor->GetSelectedObjects()->Num() : 0;
    const int32 SelectedActorsBefore = GEditor != nullptr && GEditor->GetSelectedActors() != nullptr ? GEditor->GetSelectedActors()->Num() : 0;
    TSharedRef<FJsonObject> VariableGet = CatalogArguments(TEXT("variable_get"), FString(), TEXT("Health"));
    if (!TestTrue(TEXT("local member variable getter catalogs"), Catalog.Execute(VariableGet, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TSharedPtr<FJsonObject> Action = FirstAction(Result);
    if (!TestNotNull(TEXT("variable getter returns an action"), Action.Get())) return false;
    TestEqual(TEXT("variable action family is exact"), Action->GetStringField(TEXT("node_family")), FString(TEXT("variable_get")));
    TestEqual(TEXT("variable action member is exact"), Action->GetStringField(TEXT("member_name")), FString(TEXT("Health")));
    TestEqual(TEXT("opaque action identity is bounded"), Action->GetStringField(TEXT("action_id")).Len(), 32);
    const FString FirstVariableId = Action->GetStringField(TEXT("action_id"));

    TSharedRef<FJsonObject> VariableSet = CatalogArguments(TEXT("variable_set"), FString(), TEXT("Health"));
    TestTrue(TEXT("local member variable setter catalogs"), Catalog.Execute(VariableSet, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("variable setter returns an action"), Action.Get())) return false;
    TestEqual(TEXT("variable setter family is exact"), Action->GetStringField(TEXT("node_family")), FString(TEXT("variable_set")));

    TSharedRef<FJsonObject> FunctionGraphCatalog = CatalogArguments(TEXT("variable_get"), FString(), TEXT("Health"));
    FunctionGraphCatalog->SetStringField(TEXT("graph_id"), FunctionGraphId);
    TestTrue(TEXT("function graph returns a core catalog"), Catalog.Execute(FunctionGraphCatalog, Result, Error));
    TestNotNull(TEXT("function graph has a variable action"), FirstAction(Result).Get());
    TSharedRef<FJsonObject> MacroGraphCatalog = CatalogArguments(TEXT("variable_get"), FString(), TEXT("Health"));
    MacroGraphCatalog->SetStringField(TEXT("graph_id"), MacroGraphId);
    TestTrue(TEXT("macro graph returns a core catalog"), Catalog.Execute(MacroGraphCatalog, Result, Error));
    TestNotNull(TEXT("macro graph has a variable action"), FirstAction(Result).Get());

    TSharedPtr<FJsonObject> CachedResult;
    TestTrue(TEXT("identical query uses retained catalog"), Catalog.Execute(VariableGet, CachedResult, Error));
    TestEqual(TEXT("cached action identity is stable"), FirstAction(CachedResult)->GetStringField(TEXT("action_id")), FirstVariableId);

    for (int32 QueryLimit = 1; QueryLimit <= 33; ++QueryLimit)
    {
        if (QueryLimit == 20) continue;
        Clock += 0.001;
        TSharedRef<FJsonObject> DistinctQuery = CatalogArguments(TEXT("variable_get"), FString(), TEXT("Health"), QueryLimit);
        if (!TestTrue(TEXT("distinct retained catalog executes"), Catalog.Execute(DistinctQuery, Result, Error))) return false;
    }
    TestTrue(TEXT("evicted catalog rebuilds"), Catalog.Execute(VariableGet, Result, Error));
    const FString AfterEvictionId = FirstAction(Result)->GetStringField(TEXT("action_id"));
    TestNotEqual(TEXT("catalog-capacity eviction invalidates action identity"), AfterEvictionId, FirstVariableId);

    TSharedRef<FJsonObject> PureFunction = CatalogArguments(TEXT("function_call"), TEXT("ReadCatalogValue"), FString());
    TestTrue(TEXT("pure local function catalogs"), Catalog.Execute(PureFunction, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("pure function returns an action"), Action.Get())) return false;
    TestTrue(TEXT("pure function metadata is reported"), Action->GetBoolField(TEXT("pure")));
    TestFalse(TEXT("local function is instance context"), Action->GetBoolField(TEXT("static")));

    TSharedRef<FJsonObject> ImpureFunction = CatalogArguments(TEXT("function_call"), TEXT("RunCatalogWork"), FString());
    TestTrue(TEXT("impure local function catalogs"), Catalog.Execute(ImpureFunction, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("impure function returns an action"), Action.Get())) return false;
    TestFalse(TEXT("impure function metadata is reported"), Action->GetBoolField(TEXT("pure")));

    TSharedRef<FJsonObject> InheritedFunction = CatalogArguments(TEXT("function_call"), TEXT("K2_GetActorLocation"), FString());
    InheritedFunction->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.Actor"));
    TestTrue(TEXT("inherited Actor function catalogs"), Catalog.Execute(InheritedFunction, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("inherited function returns an action"), Action.Get())) return false;
    TestEqual(TEXT("inherited owner is exact"), Action->GetStringField(TEXT("owner_class")), FString(TEXT("/Script/Engine.Actor")));

    TSharedRef<FJsonObject> StaticFunction = CatalogArguments(TEXT("function_call"), TEXT("PrintString"), FString());
    TestTrue(TEXT("static function catalogs"), Catalog.Execute(StaticFunction, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("static function returns an action"), Action.Get())) return false;
    TestTrue(TEXT("static function context is reported"), Action->GetBoolField(TEXT("static")));
    TestEqual(TEXT("static owner is exact"), Action->GetStringField(TEXT("owner_class")), FString(TEXT("/Script/Engine.KismetSystemLibrary")));

    TSharedRef<FJsonObject> Truncated = CatalogArguments(FString(), FString(), FString(), 1);
    TestTrue(TEXT("broad catalog remains bounded"), Catalog.Execute(Truncated, Result, Error));
    TestEqual(TEXT("result limit is applied"), Result->GetIntegerField(TEXT("returned_count")), 1);
    TestTrue(TEXT("bounded result reports truncation"), Result->GetBoolField(TEXT("truncated")));

    UEdGraphPin* ContextPin = nullptr;
    UEdGraphNode* ContextNode = nullptr;
    for (UEdGraphNode* Node : EventGraph->Nodes)
    {
        if (Node != nullptr && !Node->Pins.IsEmpty()) { ContextNode = Node; ContextPin = Node->Pins[0]; break; }
    }
    if (!TestNotNull(TEXT("fixture has a pin context"), ContextPin)) return false;
    TSharedRef<FJsonObject> PinFiltered = CatalogArguments(TEXT("function_call"), FString(), FString(), 5);
    const TSharedRef<FJsonObject> PinContext = MakeShared<FJsonObject>();
    PinContext->SetStringField(TEXT("node_id"), ContextNode->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
    PinContext->SetStringField(TEXT("pin_id"), ContextPin->PinId.ToString(EGuidFormats::Digits).ToLower());
    PinFiltered->SetObjectField(TEXT("pin_context"), PinContext);
    TestTrue(TEXT("live pin-context filter executes"), Catalog.Execute(PinFiltered, Result, Error));

    TSharedRef<FJsonObject> Forged = CatalogArguments(TEXT("function_call"), TEXT("DefinitelyNotAnUnrealFunction"), FString());
    TestTrue(TEXT("forged function name cannot resolve"), Catalog.Execute(Forged, Result, Error));
    TestEqual(TEXT("forged function returns no actions"), Result->GetIntegerField(TEXT("returned_count")), 0);

    double TimeoutScanClock = 0.0;
    FUnrealMCPBlueprintActionCatalog TimeoutCatalog(
        Inspector, TEXT("22222222222222222222222222222222"), [] { return 100.0; },
        [&TimeoutScanClock] { TimeoutScanClock += UnrealMCP::ActionScanSeconds + 0.1; return TimeoutScanClock; });
    TestTrue(TEXT("catalog timeout returns a bounded partial result"), TimeoutCatalog.Execute(Truncated, Result, Error));
    TestTrue(TEXT("catalog timeout is observable"), Result->GetBoolField(TEXT("timed_out")));
    TestTrue(TEXT("catalog timeout reports truncation"), Result->GetBoolField(TEXT("truncated")));

    Clock += UnrealMCP::ActionLifetimeSeconds + 1.0;
    TestTrue(TEXT("expired action query rebuilds"), Catalog.Execute(VariableGet, Result, Error));
    TestNotEqual(TEXT("expired action identity is invalidated"), FirstAction(Result)->GetStringField(TEXT("action_id")), AfterEvictionId);
    FUnrealMCPBlueprintActionCatalog RestartedCatalog(Inspector, TEXT("33333333333333333333333333333333"));
    TestTrue(TEXT("new bridge catalog executes"), RestartedCatalog.Execute(VariableGet, Result, Error));
    TestNotEqual(TEXT("bridge restart invalidates action identity"), FirstAction(Result)->GetStringField(TEXT("action_id")), FirstVariableId);

    TestEqual(TEXT("catalog preserves package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("catalog preserves compile state"), Blueprint->Status, StatusBefore);
    TestEqual(TEXT("catalog creates no transactions"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBefore);
    TestEqual(TEXT("catalog preserves selected object count"),
        GEditor != nullptr && GEditor->GetSelectedObjects() != nullptr ? GEditor->GetSelectedObjects()->Num() : 0, SelectedObjectsBefore);
    TestEqual(TEXT("catalog preserves selected actor count"),
        GEditor != nullptr && GEditor->GetSelectedActors() != nullptr ? GEditor->GetSelectedActors()->Num() : 0, SelectedActorsBefore);

    const FString StaleSnapshot = Snapshot;
    FEdGraphPinType BooleanType;
    BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("CatalogChanged"), BooleanType);
    TestFalse(TEXT("stale snapshot rejects"), RestartedCatalog.Execute(VariableGet, Result, Error));
    TestEqual(TEXT("stale snapshot uses stable error"), Error.Code, FString(TEXT("stale_precondition")));
    TestNotEqual(TEXT("fixture structure actually changed"), InspectSnapshot(Inspector, AssetPath), StaleSnapshot);
    return true;
}


#endif
