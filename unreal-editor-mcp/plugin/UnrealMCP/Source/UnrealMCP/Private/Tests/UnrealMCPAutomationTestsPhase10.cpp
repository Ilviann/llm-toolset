#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "BlueprintEventNodeSpawner.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase10ExpandedActionCatalogTest, "UnrealMCP.Phase10.ExpandedActionCatalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase10ExpandedActionCatalogTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase10");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Phase 10 Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Phase 10 fixture has an event graph"), EventGraph)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    const FString EventGraphId = EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddFunction->SetStringField(TEXT("name"), TEXT("Phase10Function"));
    AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Value"), TEXT("input"), K2Type(TEXT("int"))))}));
    if (!TestTrue(TEXT("Phase 10 function graph is added"), Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString FunctionGraphId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMacro = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("macro"), TEXT("add"));
    AddMacro->SetStringField(TEXT("name"), TEXT("Phase10Macro"));
    AddMacro->SetObjectField(TEXT("signature"), MacroSignature(true, {}));
    if (!TestTrue(TEXT("Phase 10 macro graph is added"), Mutator.Execute(TEXT("blueprint_member_edit"), AddMacro, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString MacroGraphId = Result->GetObjectField(TEXT("macro"))->GetStringField(TEXT("id"));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    double Clock = 100.0;
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("44444444444444444444444444444444"), [&Clock] { return Clock; });
    auto CatalogArguments = [&](const FString& GraphId, const FString& Family, int32 Limit = 20)
    {
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("graph_id"), GraphId);
        Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
        Arguments->SetStringField(TEXT("node_family"), Family);
        Arguments->SetNumberField(TEXT("limit"), Limit);
        return Arguments;
    };
    auto Actions = [](const TSharedPtr<FJsonObject>& CatalogResult) -> const TArray<TSharedPtr<FJsonValue>>*
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        return CatalogResult.IsValid() && CatalogResult->TryGetArrayField(TEXT("actions"), Values) ? Values : nullptr;
    };
    auto FirstAction = [&](const TSharedPtr<FJsonObject>& CatalogResult) -> TSharedPtr<FJsonObject>
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = Actions(CatalogResult);
        return Values != nullptr && !Values->IsEmpty() ? (*Values)[0]->AsObject() : nullptr;
    };
    auto HasWildcard = [&](const TSharedPtr<FJsonObject>& CatalogResult)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = Actions(CatalogResult);
        if (Values == nullptr) return false;
        for (const TSharedPtr<FJsonValue>& Value : *Values)
        {
            const TSharedPtr<FJsonObject> Record = Value->AsObject();
            if (Record.IsValid() && Record->GetBoolField(TEXT("wildcard"))) return true;
        }
        return false;
    };

    TSharedRef<FJsonObject> EventQuery = CatalogArguments(EventGraphId, TEXT("event"));
    TestTrue(TEXT("unique inherited event catalogs in an event graph"), Catalog.Execute(EventQuery, Result, Error));
    TSharedPtr<FJsonObject> Action = FirstAction(Result);
    if (!TestNotNull(TEXT("event query returns an action"), Action.Get())) return false;
    TestEqual(TEXT("event family is exact"), Action->GetStringField(TEXT("node_family")), FString(TEXT("event")));
    TestEqual(TEXT("event member kind is explicit"), Action->GetStringField(TEXT("member_kind")), FString(TEXT("event")));
    const FString EventFunctionName = Action->GetStringField(TEXT("member_name"));
    const FString EventOwnerPath = Action->GetStringField(TEXT("owner_class"));
    TestFalse(TEXT("event function name is present"), EventFunctionName.IsEmpty());
    TestFalse(TEXT("event owner path is present"), EventOwnerPath.IsEmpty());
    EventQuery->SetStringField(TEXT("owner_class"), EventOwnerPath);
    EventQuery->SetStringField(TEXT("function"), EventFunctionName);
    TestTrue(TEXT("exact event filter resolves"), Catalog.Execute(EventQuery, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("exact event filter returns an action"), Action.Get())) return false;
    TestEqual(TEXT("event function is exact"), Action->GetStringField(TEXT("member_name")), EventFunctionName);
    const FString EventActionId = Action->GetStringField(TEXT("action_id"));
    TSharedPtr<FJsonObject> CachedEventResult;
    TestTrue(TEXT("event query is cacheable"), Catalog.Execute(EventQuery, CachedEventResult, Error));
    TestEqual(TEXT("event cache reuses opaque identity"), FirstAction(CachedEventResult)->GetStringField(TEXT("action_id")), EventActionId);

    for (const FString& RestrictedGraphId : {FunctionGraphId, MacroGraphId})
    {
        TSharedRef<FJsonObject> RestrictedEvent = CatalogArguments(RestrictedGraphId, TEXT("event"));
        RestrictedEvent->SetStringField(TEXT("function"), EventFunctionName);
        TestTrue(TEXT("event family restriction query executes"), Catalog.Execute(RestrictedEvent, Result, Error));
        TestEqual(TEXT("events are unavailable outside event graphs"), Result->GetIntegerField(TEXT("returned_count")), 0);
    }

    TSharedRef<FJsonObject> LatentEvent = CatalogArguments(EventGraphId, TEXT("function_call"));
    LatentEvent->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.KismetSystemLibrary"));
    LatentEvent->SetStringField(TEXT("function"), TEXT("Delay"));
    TestTrue(TEXT("latent function catalogs in event graph"), Catalog.Execute(LatentEvent, Result, Error));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("event graph returns latent action"), Action.Get())) return false;
    TestTrue(TEXT("latent metadata is explicit"), Action->GetBoolField(TEXT("latent")));
    TSharedRef<FJsonObject> LatentFunction = CatalogArguments(FunctionGraphId, TEXT("function_call"));
    LatentFunction->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.KismetSystemLibrary"));
    LatentFunction->SetStringField(TEXT("function"), TEXT("Delay"));
    TestTrue(TEXT("latent function restriction query executes"), Catalog.Execute(LatentFunction, Result, Error));
    TestEqual(TEXT("latent calls are unavailable in function graphs"), Result->GetIntegerField(TEXT("returned_count")), 0);

    for (const FString& Family : {TEXT("flow_control"), TEXT("cast"), TEXT("literal"), TEXT("operator")})
    {
        for (const FString& GraphId : {EventGraphId, FunctionGraphId, MacroGraphId})
        {
            TSharedRef<FJsonObject> Query = CatalogArguments(GraphId, Family, 50);
            if (Family == TEXT("cast")) Query->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.Actor"));
            if (Family == TEXT("literal"))
            {
                Query->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.KismetSystemLibrary"));
                Query->SetStringField(TEXT("function"), TEXT("MakeLiteralInt"));
            }
            if (!TestTrue(*FString::Printf(TEXT("%s catalogs in supported graph"), *Family), Catalog.Execute(Query, Result, Error)))
            { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
            if (!TestTrue(*FString::Printf(TEXT("%s returns a context-valid action"), *Family), Result->GetIntegerField(TEXT("returned_count")) > 0)) return false;
            Action = FirstAction(Result);
            TestEqual(*FString::Printf(TEXT("%s family is exact"), *Family), Action->GetStringField(TEXT("node_family")), Family);
        }
    }

    TSharedRef<FJsonObject> Operators = CatalogArguments(EventGraphId, TEXT("operator"), 50);
    Operators->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.KismetMathLibrary"));
    TestTrue(TEXT("operator catalog executes"), Catalog.Execute(Operators, Result, Error));
    TestTrue(TEXT("operator family exposes wildcard candidates"), HasWildcard(Result));
    Action = FirstAction(Result);
    if (!TestNotNull(TEXT("operator query returns an action"), Action.Get())) return false;
    TSharedRef<FJsonObject> ExactOperator = CatalogArguments(EventGraphId, TEXT("operator"), 5);
    ExactOperator->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.KismetMathLibrary"));
    ExactOperator->SetStringField(TEXT("function"), Action->GetStringField(TEXT("member_name")));
    TestTrue(TEXT("narrow exact operator filter executes"), Catalog.Execute(ExactOperator, Result, Error));
    TestTrue(TEXT("narrow exact operator filter resolves"), Result->GetIntegerField(TEXT("returned_count")) > 0);

    UEdGraphNode* IntegerContextNode = nullptr;
    UEdGraphPin* IntegerContextPin = nullptr;
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph == nullptr || Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower() != FunctionGraphId) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) continue;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin != nullptr && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
                { IntegerContextNode = Node; IntegerContextPin = Pin; break; }
            }
            if (IntegerContextPin != nullptr) break;
        }
    }
    if (!TestNotNull(TEXT("function fixture has an integer pin context"), IntegerContextPin)) return false;
    TSharedRef<FJsonObject> PinOperators = CatalogArguments(FunctionGraphId, TEXT("operator"), 20);
    PinOperators->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.KismetMathLibrary"));
    const TSharedRef<FJsonObject> PinContext = MakeShared<FJsonObject>();
    PinContext->SetStringField(TEXT("node_id"), IntegerContextNode->NodeGuid.ToString(EGuidFormats::Digits).ToLower());
    PinContext->SetStringField(TEXT("pin_id"), IntegerContextPin->PinId.ToString(EGuidFormats::Digits).ToLower());
    PinOperators->SetObjectField(TEXT("pin_context"), PinContext);
    TestTrue(TEXT("operator pin-context catalog executes"), Catalog.Execute(PinOperators, Result, Error));
    TestTrue(TEXT("operator pin context keeps compatible candidates"), Result->GetIntegerField(TEXT("returned_count")) > 0);

    for (const FString& Family : {TEXT("event"), TEXT("flow_control"), TEXT("cast"), TEXT("literal"), TEXT("operator")})
    {
        TSharedRef<FJsonObject> Forged = CatalogArguments(EventGraphId, Family);
        Forged->SetStringField(TEXT("text"), TEXT("DefinitelyNotARealUnrealAction"));
        TestTrue(*FString::Printf(TEXT("forged %s query executes"), *Family), Catalog.Execute(Forged, Result, Error));
        TestEqual(*FString::Printf(TEXT("forged %s action cannot resolve"), *Family), Result->GetIntegerField(TEXT("returned_count")), 0);
    }

    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    const int32 TransactionsBefore = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TestTrue(TEXT("final mutation-free catalog executes"), Catalog.Execute(Operators, Result, Error));
    TestEqual(TEXT("expanded catalog preserves package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("expanded catalog preserves compile state"), Blueprint->Status, StatusBefore);
    TestEqual(TEXT("expanded catalog creates no transactions"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBefore);

    UClass* EventOwnerClass = FindObject<UClass>(nullptr, *EventOwnerPath);
    if (!TestNotNull(TEXT("cataloged event owner class exists"), EventOwnerClass)) return false;
    UFunction* EventFunction = EventOwnerClass->FindFunctionByName(FName(*EventFunctionName));
    if (!TestNotNull(TEXT("cataloged unique event function exists"), EventFunction)) return false;
    UBlueprintEventNodeSpawner* EventSpawner = UBlueprintEventNodeSpawner::Create(EventFunction);
    if (!TestNotNull(TEXT("unique event fixture spawner exists"), EventSpawner)) return false;
    UEdGraphNode* ExistingEvent = EventSpawner->Invoke(EventGraph, IBlueprintNodeBinder::FBindingSet(), FVector2D::ZeroVector);
    if (!TestNotNull(TEXT("unique event fixture node is created"), ExistingEvent)) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    FUnrealMCPBlueprintActionCatalog UniqueCatalog(Inspector, TEXT("55555555555555555555555555555555"));
    EventQuery = CatalogArguments(EventGraphId, TEXT("event"));
    EventQuery->SetStringField(TEXT("owner_class"), EventOwnerPath);
    EventQuery->SetStringField(TEXT("function"), EventFunctionName);
    TestTrue(TEXT("existing unique-event query executes"), UniqueCatalog.Execute(EventQuery, Result, Error));
    TestEqual(TEXT("existing unique event is filtered from creation actions"), Result->GetIntegerField(TEXT("returned_count")), 0);

    FEdGraphPinType BooleanType;
    BooleanType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    const FString CurrentSnapshot = Snapshot;
    FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Phase10Changed"), BooleanType);
    TestFalse(TEXT("expanded family rejects stale snapshot"), UniqueCatalog.Execute(EventQuery, Result, Error));
    TestEqual(TEXT("expanded family stale error is stable"), Error.Code, FString(TEXT("stale_precondition")));
    TestNotEqual(TEXT("Phase 10 fixture structure actually changed"), InspectSnapshot(Inspector, AssetPath), CurrentSnapshot);
    return true;
}


#endif
