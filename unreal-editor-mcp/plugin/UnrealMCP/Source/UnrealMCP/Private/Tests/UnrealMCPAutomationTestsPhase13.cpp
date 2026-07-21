#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "BlueprintActionDatabase.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintTypePromotion.h"
#include "K2Node_PromotableOperator.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase13AtomicGraphEditingTest,
    "UnrealMCP.Phase13.WildcardsConversionsAndAtomicGraphEditing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase13AtomicGraphEditingTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase13");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Phase 13 Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* Graph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Phase 13 fixture has an event graph"), Graph)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    const FString GraphId = Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    auto Id = [](const FGuid& Guid) { return Guid.ToString(EGuidFormats::Digits).ToLower(); };

    auto AddCall = [&](UFunction* Function, int32 X, int32 Y) -> UK2Node_CallFunction*
    {
        if (Function == nullptr) return nullptr;
        UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
        Node->SetFlags(RF_Transactional);
        Graph->AddNode(Node, true, false);
        Node->CreateNewGuid();
        Node->SetFromFunction(Function);
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        for (UEdGraphPin* Pin : Node->Pins) if (Pin != nullptr && !Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
        return Node;
    };
    UFunction* LiteralFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    UK2Node_CallFunction* Literal = AddCall(LiteralFunction, 0, 0);
    UK2Node_CallFunction* LiteralLimit = AddCall(LiteralFunction, 0, 180);
    UK2Node_CallFunction* PrintOne = AddCall(PrintFunction, 640, 0);
    UK2Node_CallFunction* PrintTwo = AddCall(PrintFunction, 960, 0);
    UK2Node_CallFunction* PrintLimit = AddCall(PrintFunction, 640, 180);
    if (!TestNotNull(TEXT("integer literal exists"), Literal)
        || !TestNotNull(TEXT("limit literal exists"), LiteralLimit)
        || !TestNotNull(TEXT("first print exists"), PrintOne)
        || !TestNotNull(TEXT("second print exists"), PrintTwo)
        || !TestNotNull(TEXT("limit print exists"), PrintLimit)) return false;

    FTypePromotion::ClearNodeSpawners();
    FBlueprintActionDatabase::Get().RefreshAll();
    UBlueprintFunctionNodeSpawner* OperatorSpawner = FTypePromotion::GetOperatorSpawner(TEXT("Add"));
    UK2Node_PromotableOperator* AddOperator = nullptr;
    if (OperatorSpawner != nullptr)
    {
        IBlueprintNodeBinder::FBindingSet Bindings;
        AddOperator = Cast<UK2Node_PromotableOperator>(OperatorSpawner->Invoke(Graph, Bindings, FVector2D(320.0, 240.0)));
    }
    if (!TestNotNull(TEXT("wildcard add operator is available"), AddOperator)) return false;
    if (!AddOperator->NodeGuid.IsValid()) AddOperator->CreateNewGuid();
    for (UEdGraphPin* Pin : AddOperator->Pins) if (Pin != nullptr && !Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("13131313131313131313131313131313"));
    FUnrealMCPBlueprintGraphEditor Editor(Inspector, Catalog);
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    auto Edit = [&](const FString& Operation)
    {
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("expected_snapshot"), InspectSnapshot(Inspector, AssetPath));
        Arguments->SetStringField(TEXT("operation"), Operation);
        Arguments->SetStringField(TEXT("graph_id"), GraphId);
        return Arguments;
    };
    auto Connect = [&](FUnrealMCPBlueprintGraphEditor& TargetEditor, UEdGraphNode* FromNode, UEdGraphPin* FromPin,
        UEdGraphNode* ToNode, UEdGraphPin* ToPin, bool bAutomaticConversion, const FString& ExpectedSnapshot = FString())
    {
        TSharedRef<FJsonObject> Arguments = Edit(TEXT("connect_pins"));
        if (!ExpectedSnapshot.IsEmpty()) Arguments->SetStringField(TEXT("expected_snapshot"), ExpectedSnapshot);
        Arguments->SetStringField(TEXT("from_node_id"), Id(FromNode->NodeGuid));
        Arguments->SetStringField(TEXT("from_pin_id"), Id(FromPin->PinId));
        Arguments->SetStringField(TEXT("to_node_id"), Id(ToNode->NodeGuid));
        Arguments->SetStringField(TEXT("to_pin_id"), Id(ToPin->PinId));
        if (bAutomaticConversion) Arguments->SetBoolField(TEXT("automatic_conversion"), true);
        return TargetEditor.Execute(Arguments, Result, Error);
    };

    UEdGraphPin* LiteralOut = Literal->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
    UEdGraphPin* TextIn = PrintOne->FindPinChecked(TEXT("InString"), EGPD_Input);
    const FString BeforeDisabled = InspectSnapshot(Inspector, AssetPath);
    const int32 NodesBeforeDisabled = Graph->Nodes.Num();
    const int32 TransactionsBeforeDisabled = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("conversion remains disabled by default"), Connect(Editor, Literal, LiteralOut, PrintOne, TextIn, false));
    TestEqual(TEXT("disabled conversion is explicit"), Error.Code, FString(TEXT("conversion_required")));
    TestEqual(TEXT("disabled conversion inserts nothing"), Graph->Nodes.Num(), NodesBeforeDisabled);
    TestEqual(TEXT("disabled conversion preserves snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeDisabled);
    TestEqual(TEXT("disabled conversion creates no transaction"), GEditor->Trans->GetQueueLength(), TransactionsBeforeDisabled);

    FUnrealMCPBlueprintGraphEditor RollbackEditor(Inspector, Catalog, {}, {},
        [](const UEdGraphSchema_K2* Schema, UEdGraphPin* FromPin, UEdGraphPin* ToPin)
        {
            if (Schema == nullptr || !Schema->TryCreateConnection(FromPin, ToPin)) return false;
            UEdGraph* LocalGraph = FromPin->GetOwningNodeUnchecked()->GetGraph();
            UEdGraphNode* Unexpected = NewObject<UEdGraphNode>(LocalGraph);
            Unexpected->SetFlags(RF_Transactional);
            LocalGraph->AddNode(Unexpected, true, false);
            Unexpected->CreateNewGuid();
            return true;
        });
    const FString BeforeRollback = InspectSnapshot(Inspector, AssetPath);
    const int32 NodesBeforeRollback = Graph->Nodes.Num();
    TestFalse(TEXT("unexpected second insertion rolls back"), Connect(RollbackEditor, Literal, LiteralOut, PrintOne, TextIn, true));
    TestEqual(TEXT("rollback uses stable internal error"), Error.Code, FString(TEXT("internal_error")));
    TestEqual(TEXT("rollback restores inserted nodes"), Graph->Nodes.Num(), NodesBeforeRollback);
    TestEqual(TEXT("rollback restores exact snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeRollback);

    TestTrue(TEXT("explicit conversion inserts one bounded node"), Connect(Editor, Literal, LiteralOut, PrintOne, TextIn, true));
    TSharedPtr<FJsonObject> Connection = Result->GetObjectField(TEXT("changed"))->GetObjectField(TEXT("connection"));
    TestTrue(TEXT("conversion is reported as non-direct"), !Connection->GetBoolField(TEXT("direct")));
    TestTrue(TEXT("conversion opt-in is reported"), Connection->GetBoolField(TEXT("automatic_conversion")));
    TestEqual(TEXT("one conversion node is reported"), static_cast<int32>(Connection->GetNumberField(TEXT("conversion_node_count"))), 1);
    TestTrue(TEXT("conversion returns inserted node and pin identities"), Result->GetArrayField(TEXT("created_identities")).Num() >= 3);
    TestEqual(TEXT("conversion adds one graph node"), Graph->Nodes.Num(), NodesBeforeDisabled + 1);
    TestTrue(TEXT("Undo removes the conversion node and links"), GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores the node count"), Graph->Nodes.Num(), NodesBeforeDisabled);
    TestTrue(TEXT("Redo restores the conversion node and links"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores the conversion node count"), Graph->Nodes.Num(), NodesBeforeDisabled + 1);

    UEdGraphPin* OperatorA = AddOperator->FindPinChecked(TEXT("A"), EGPD_Input);
    TestTrue(TEXT("wildcard operator specializes without conversion opt-in"), Connect(Editor, LiteralLimit,
        LiteralLimit->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), AddOperator, OperatorA, false));
    Connection = Result->GetObjectField(TEXT("changed"))->GetObjectField(TEXT("connection"));
    TestTrue(TEXT("wildcard specialization is reported"), Connection->GetBoolField(TEXT("wildcard_specialized")));
    TestTrue(TEXT("specialization remains direct"), Connection->GetBoolField(TEXT("direct")));
    TestTrue(TEXT("specialization returns reconstructed identities"), Result->GetArrayField(TEXT("reconstructed_identities")).Num() > 1);
    TestTrue(TEXT("specialized operator input is integer"), AddOperator->FindPinChecked(TEXT("A"), EGPD_Input)->PinType.PinCategory == UEdGraphSchema_K2::PC_Int);

    TestTrue(TEXT("first execution edge connects"), Connect(Editor, PrintOne,
        PrintOne->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output), PrintTwo,
        PrintTwo->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input), false));
    const FString BeforeCycle = InspectSnapshot(Inspector, AssetPath);
    const int32 TransactionsBeforeCycle = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("directed execution cycle rejects"), Connect(Editor, PrintTwo,
        PrintTwo->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output), PrintOne,
        PrintOne->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input), false));
    TestEqual(TEXT("cycle uses stable invalid connection"), Error.Code, FString(TEXT("invalid_connection")));
    TestEqual(TEXT("cycle rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeCycle);
    TestEqual(TEXT("cycle rejection creates no transaction"), GEditor->Trans->GetQueueLength(), TransactionsBeforeCycle);

    TestFalse(TEXT("incompatible exec-to-text remains rejected with opt-in"), Connect(Editor, PrintTwo,
        PrintTwo->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output), PrintLimit,
        PrintLimit->FindPinChecked(TEXT("InString"), EGPD_Input), true));
    TestEqual(TEXT("incompatible connection has stable error"), Error.Code, FString(TEXT("incompatible_pins")));
    TestFalse(TEXT("stale snapshot rejects before connection"), Connect(Editor, PrintTwo,
        PrintTwo->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output), PrintLimit,
        PrintLimit->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input), false, TEXT("0000000000000000000000000000000000000000")));
    TestEqual(TEXT("stale snapshot has stable error"), Error.Code, FString(TEXT("stale_precondition")));

    TSharedRef<FJsonObject> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), InspectSnapshot(Inspector, AssetPath));
    TestTrue(TEXT("Phase 13 Blueprint compiles"), Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error));
    TestTrue(TEXT("Phase 13 compile succeeds"), Result->GetBoolField(TEXT("compile_succeeded")));
    TSharedRef<FJsonObject> Save = AssetArguments(AssetPath);
    Save->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    TestTrue(TEXT("Phase 13 Blueprint saves"), Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error));

    while (Graph->Nodes.Num() < UnrealMCP::MaxGraphNodes)
    {
        UEdGraphNode* Filler = NewObject<UEdGraphNode>(Graph);
        Graph->AddNode(Filler, true, false);
        Filler->CreateNewGuid();
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    const FString BeforeLimit = InspectSnapshot(Inspector, AssetPath);
    const int32 TransactionsBeforeLimit = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("conversion capacity limit rejects before transaction"), Connect(Editor, LiteralLimit,
        LiteralLimit->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output), PrintLimit,
        PrintLimit->FindPinChecked(TEXT("InString"), EGPD_Input), true));
    TestEqual(TEXT("conversion capacity uses graph limit error"), Error.Code, FString(TEXT("graph_limit_exceeded")));
    TestEqual(TEXT("limit rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeLimit);
    TestEqual(TEXT("limit rejection creates no transaction"), GEditor->Trans->GetQueueLength(), TransactionsBeforeLimit);
    return true;
}


#endif
