#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase12PinDefaultsAndConnectionsTest,
    "UnrealMCP.Phase12.PinDefaultsAndDirectConnections", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase12PinDefaultsAndConnectionsTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase12");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Phase 12 Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* Graph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Phase 12 fixture has an event graph"), Graph)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    const FString GraphId = Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();

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
    UK2Node_CallFunction* LiteralOne = AddCall(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt)), 0, 0);
    UK2Node_CallFunction* LiteralTwo = AddCall(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt)), 0, 180);
    UK2Node_CallFunction* Add = AddCall(
        UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt)), 360, 80);
    UK2Node_CallFunction* PrintOne = AddCall(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)), 0, 420);
    UK2Node_CallFunction* PrintTwo = AddCall(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)), 420, 420);
    UK2Node_CallFunction* ObjectName = AddCall(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, GetObjectName)), 0, 700);
    UK2Node_CallFunction* ClassName = AddCall(
        UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, GetClassDisplayName)), 420, 700);
    if (!TestNotNull(TEXT("literal-one call node exists"), LiteralOne)
        || !TestNotNull(TEXT("literal-two call node exists"), LiteralTwo)
        || !TestNotNull(TEXT("integer-add call node exists"), Add)
        || !TestNotNull(TEXT("first print call node exists"), PrintOne)
        || !TestNotNull(TEXT("second print call node exists"), PrintTwo)
        || !TestNotNull(TEXT("object-reference call node exists"), ObjectName)
        || !TestNotNull(TEXT("class-reference call node exists"), ClassName)) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("77777777777777777777777777777777"));
    FUnrealMCPBlueprintGraphEditor Editor(Inspector, Catalog);
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    auto Id = [](const FGuid& Guid) { return Guid.ToString(EGuidFormats::Digits).ToLower(); };
    auto ReferenceDefault = [](const FString& Path)
    {
        const TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
        Default->SetStringField(TEXT("kind"), TEXT("reference"));
        Default->SetStringField(TEXT("path"), Path);
        return Default;
    };
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
    auto SetDefault = [&](UEdGraphNode* Node, UEdGraphPin* Pin, const TSharedRef<FJsonObject>& Default) -> bool
    {
        TSharedRef<FJsonObject> Arguments = Edit(TEXT("set_pin_default"));
        Arguments->SetStringField(TEXT("node_id"), Id(Node->NodeGuid));
        Arguments->SetStringField(TEXT("pin_id"), Id(Pin->PinId));
        Arguments->SetObjectField(TEXT("default"), Default);
        return Editor.Execute(Arguments, Result, Error);
    };
    auto Connect = [&](const FString& Operation, UEdGraphNode* FromNode, UEdGraphPin* FromPin,
        UEdGraphNode* ToNode, UEdGraphPin* ToPin) -> bool
    {
        TSharedRef<FJsonObject> Arguments = Edit(Operation);
        Arguments->SetStringField(TEXT("from_node_id"), Id(FromNode->NodeGuid));
        Arguments->SetStringField(TEXT("from_pin_id"), Id(FromPin->PinId));
        Arguments->SetStringField(TEXT("to_node_id"), Id(ToNode->NodeGuid));
        Arguments->SetStringField(TEXT("to_pin_id"), Id(ToPin->PinId));
        return Editor.Execute(Arguments, Result, Error);
    };

    UEdGraphPin* TextPin = PrintOne->FindPinChecked(TEXT("InString"), EGPD_Input);
    UEdGraphPin* BoolPin = PrintOne->FindPinChecked(TEXT("bPrintToScreen"), EGPD_Input);
    UEdGraphPin* ObjectPin = ObjectName->FindPinChecked(TEXT("Object"), EGPD_Input);
    UEdGraphPin* ClassPin = ClassName->FindPinChecked(TEXT("Class"), EGPD_Input);
    TestTrue(TEXT("string pin default is schema parsed and set"), SetDefault(PrintOne, TextPin,
        LiteralDefault(MakeShared<FJsonValueString>(TEXT("Phase 12 direct behavior")))));
    TestEqual(TEXT("string pin retains canonical default"), TextPin->DefaultValue, FString(TEXT("Phase 12 direct behavior")));
    const TSharedRef<FJsonObject> EngineDefault = MakeShared<FJsonObject>();
    EngineDefault->SetStringField(TEXT("kind"), TEXT("engine_default"));
    TestTrue(TEXT("engine_default resets through the live schema"), SetDefault(PrintOne, TextPin, EngineDefault));
    TestTrue(TEXT("reset pin matches its autogenerated default"),
        CastChecked<UEdGraphSchema_K2>(Graph->GetSchema())->DoesDefaultValueMatchAutogenerated(*TextPin));
    TestTrue(TEXT("string pin can be set again after reset"), SetDefault(PrintOne, TextPin,
        LiteralDefault(MakeShared<FJsonValueString>(TEXT("Phase 12 direct behavior")))));
    TestTrue(TEXT("Boolean pin default is schema parsed and set"), SetDefault(PrintOne, BoolPin,
        LiteralDefault(MakeShared<FJsonValueBoolean>(false))));
    TestEqual(TEXT("Boolean pin retains canonical default"), BoolPin->DefaultValue, FString(TEXT("false")));
    TestTrue(TEXT("hard object reference default is type checked and set"), SetDefault(ObjectName, ObjectPin,
        ReferenceDefault(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"))));
    TestNotNull(TEXT("hard object reference resolves through the schema"), ObjectPin->DefaultObject.Get());
    TestTrue(TEXT("hard class reference default is type checked and set"), SetDefault(ClassName, ClassPin,
        ReferenceDefault(TEXT("/Script/Engine.Actor"))));
    TestEqual(TEXT("hard class reference resolves exactly"), ClassPin->DefaultObject.Get(), static_cast<UObject*>(AActor::StaticClass()));
    TestTrue(TEXT("Undo restores class pin default"), GEditor->UndoTransaction());
    TestNull(TEXT("class default is absent after Undo"), ClassPin->DefaultObject.Get());
    TestTrue(TEXT("Redo restores class pin default"), GEditor->RedoTransaction());
    TestEqual(TEXT("class default survives Redo"), ClassPin->DefaultObject.Get(), static_cast<UObject*>(AActor::StaticClass()));

    UEdGraphPin* LiteralOneOut = LiteralOne->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
    UEdGraphPin* LiteralTwoOut = LiteralTwo->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
    UEdGraphPin* AddA = Add->FindPinChecked(TEXT("A"), EGPD_Input);
    TestTrue(TEXT("compatible data pins connect directly"), Connect(TEXT("connect_pins"), LiteralOne, LiteralOneOut, Add, AddA));
    TestTrue(TEXT("direct data connection exists"), LiteralOneOut->LinkedTo.Contains(AddA));
    TestTrue(TEXT("a second data connection replaces the exclusive input link"),
        Connect(TEXT("connect_pins"), LiteralTwo, LiteralTwoOut, Add, AddA));
    TestFalse(TEXT("replacement breaks only the prior input link"), LiteralOneOut->LinkedTo.Contains(AddA));
    TestTrue(TEXT("replacement direct link exists"), LiteralTwoOut->LinkedTo.Contains(AddA));
    const FString SnapshotBeforeDuplicate = InspectSnapshot(Inspector, AssetPath);
    const int32 TransactionsBeforeDuplicate = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("duplicate direct connection rejects"), Connect(TEXT("connect_pins"), LiteralTwo, LiteralTwoOut, Add, AddA));
    TestEqual(TEXT("duplicate connection has stable error"), Error.Code, FString(TEXT("invalid_connection")));
    TestEqual(TEXT("duplicate connection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), SnapshotBeforeDuplicate);
    TestEqual(TEXT("duplicate connection creates no transaction"), GEditor->Trans->GetQueueLength(), TransactionsBeforeDuplicate);
    TestTrue(TEXT("data pins disconnect directly"), Connect(TEXT("disconnect_pins"), LiteralTwo, LiteralTwoOut, Add, AddA));
    TestFalse(TEXT("disconnected data link is absent"), LiteralTwoOut->LinkedTo.Contains(AddA));
    TestTrue(TEXT("Undo restores disconnected data link"), GEditor->UndoTransaction());
    TestTrue(TEXT("data link returns after Undo"), LiteralTwoOut->LinkedTo.Contains(AddA));
    TestTrue(TEXT("Redo disconnects data link again"), GEditor->RedoTransaction());
    TestFalse(TEXT("data link is absent after Redo"), LiteralTwoOut->LinkedTo.Contains(AddA));

    UEdGraphPin* ThenPin = PrintOne->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output);
    UEdGraphPin* ExecutePin = PrintTwo->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    TestTrue(TEXT("execution pins connect directly"), Connect(TEXT("connect_pins"), PrintOne, ThenPin, PrintTwo, ExecutePin));
    TestTrue(TEXT("execution link exists"), ThenPin->LinkedTo.Contains(ExecutePin));
    TestTrue(TEXT("execution pins disconnect directly"), Connect(TEXT("disconnect_pins"), PrintOne, ThenPin, PrintTwo, ExecutePin));

    const FString SnapshotBeforeConversion = InspectSnapshot(Inspector, AssetPath);
    const int32 NodesBeforeConversion = Graph->Nodes.Num();
    const int32 TransactionsBeforeConversion = GEditor->Trans->GetQueueLength();
    TestFalse(TEXT("conversion-requiring connection rejects"), Connect(TEXT("connect_pins"), LiteralOne, LiteralOneOut, PrintOne, TextPin));
    TestEqual(TEXT("conversion rejection is explicit"), Error.Code, FString(TEXT("conversion_required")));
    TestEqual(TEXT("conversion rejection inserts no node"), Graph->Nodes.Num(), NodesBeforeConversion);
    TestEqual(TEXT("conversion rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), SnapshotBeforeConversion);
    TestEqual(TEXT("conversion rejection creates no transaction"), GEditor->Trans->GetQueueLength(), TransactionsBeforeConversion);

    const FString SnapshotBeforeIdentityRejections = InspectSnapshot(Inspector, AssetPath);
    const bool bDirtyBeforeIdentityRejections = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeIdentityRejections = Blueprint->Status;
    const int32 TransactionsBeforeIdentityRejections = GEditor->Trans->GetQueueLength();
    TSharedRef<FJsonObject> ProtectedDefault = Edit(TEXT("set_pin_default"));
    ProtectedDefault->SetStringField(TEXT("node_id"), Id(PrintOne->NodeGuid));
    ProtectedDefault->SetStringField(TEXT("pin_id"), Id(PrintOne->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->PinId));
    ProtectedDefault->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueNumber>(1)));
    TestFalse(TEXT("execution-pin default rejects as protected"), Editor.Execute(ProtectedDefault, Result, Error));
    TestEqual(TEXT("protected default uses stable error"), Error.Code, FString(TEXT("protected_pin")));

    TSharedRef<FJsonObject> MissingPin = Edit(TEXT("set_pin_default"));
    MissingPin->SetStringField(TEXT("node_id"), Id(PrintOne->NodeGuid));
    MissingPin->SetStringField(TEXT("pin_id"), TEXT("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));
    MissingPin->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueString>(TEXT("stale"))));
    TestFalse(TEXT("stale pin identity rejects"), Editor.Execute(MissingPin, Result, Error));
    TestEqual(TEXT("stale pin identity uses stable error"), Error.Code, FString(TEXT("invalid_pin")));
    TestEqual(TEXT("pin rejections preserve the graph snapshot"), InspectSnapshot(Inspector, AssetPath), SnapshotBeforeIdentityRejections);
    TestEqual(TEXT("pin rejections preserve package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeIdentityRejections);
    TestEqual(TEXT("pin rejections preserve compile state"), Blueprint->Status, StatusBeforeIdentityRejections);
    TestEqual(TEXT("pin rejections create no transactions"), GEditor->Trans->GetQueueLength(), TransactionsBeforeIdentityRejections);

    const FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
    TestTrue(TEXT("Phase 12 Blueprint compiles"), Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error));
    TestTrue(TEXT("Phase 12 compile succeeds"), Result->GetBoolField(TEXT("compile_succeeded")));
    TSharedRef<FJsonObject> Save = AssetArguments(AssetPath);
    Save->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    TestTrue(TEXT("Phase 12 Blueprint saves"), Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error));
    TestTrue(TEXT("Phase 12 save reports success"), Result->GetBoolField(TEXT("saved")));
    return true;
}


#endif
