#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Factories/AnimBlueprintFactory.h"
#include "UnrealMCPBlueprintInspectionSupport.h"

namespace UnrealMCP::Tests::Animation
{
template <typename T> T* AddNode(UEdGraph* Graph)
{
    T* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    Node->AllocateDefaultPins();
    return Node;
}

UAnimBlueprint* CreateFixture(const FString& PackageName)
{
    UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
    Factory->ParentClass = UAnimInstance::StaticClass();
    Factory->bTemplate = true;
    UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
        UAnimBlueprint::StaticClass(), CreatePackage(*PackageName),
        FName(*FPackageName::GetLongPackageAssetName(PackageName)),
        RF_Public | RF_Standalone, nullptr, GWarn));
    if (Blueprint == nullptr) return nullptr;
    FAssetRegistryModule::AssetCreated(Blueprint);
    UEdGraph* AnimGraph = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (Graph != nullptr && Graph->IsA<UAnimationGraph>()) { AnimGraph = Graph; break; }
    if (AnimGraph == nullptr) return nullptr;
    UAnimGraphNode_StateMachine* Machine = AddNode<UAnimGraphNode_StateMachine>(AnimGraph);
    UAnimationStateMachineGraph* States = Machine->EditorStateMachineGraph;
    UAnimStateNode* Idle = AddNode<UAnimStateNode>(States);
    UAnimStateNode* Run = AddNode<UAnimStateNode>(States);
    Idle->OnRenameNode(TEXT("Idle"));
    Run->OnRenameNode(TEXT("Run"));
    States->EntryNode->GetOutputPin()->MakeLinkTo(Idle->GetInputPin());
    UAnimStateTransitionNode* Transition = AddNode<UAnimStateTransitionNode>(States);
    Transition->CreateConnections(Idle, Run);
    TArray<UAnimGraphNode_Root*> Roots;
    AnimGraph->GetNodesOfClass(Roots);
    if (!Roots.IsEmpty())
    {
        for (UEdGraphPin* Pin : Machine->Pins)
            if (Pin != nullptr && Pin->Direction == EGPD_Output)
                for (UEdGraphPin* Input : Roots[0]->Pins)
                    if (Input != nullptr && Input->Direction == EGPD_Input)
                        Pin->MakeLinkTo(Input);
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint;
}

TSharedPtr<FJsonObject> Find(const TSharedPtr<FJsonObject>& Result, const FString& Section,
    const FString& Field = FString(), const FString& Value = FString())
{
    if (!Result.IsValid()) return nullptr;
    for (const TSharedPtr<FJsonValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FJsonObject> Record = Item->AsObject();
        FString Actual;
        if (Record->GetStringField(TEXT("section")) == Section
            && (Field.IsEmpty() || (Record->TryGetStringField(Field, Actual) && Actual == Value))) return Record;
    }
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPAnimationInspectionTest,
    "UnrealMCP.Animation.GraphInspection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPAnimationInspectionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::Tests::Animation;
    using namespace UnrealMCP::BlueprintFamilyPolicy;
    UAnimBlueprint* Blueprint = CreateFixture(TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/ABP_Inspection"));
    if (!TestNotNull(TEXT("template animation Blueprint creates"), Blueprint)) return false;
    TestTrue(TEXT("fixture compiles"), Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
    TestTrue(TEXT("fixture saves cleanly"), SaveBlueprintFixture(Blueprint));
    const bool bDirty = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus Status = Blueprint->Status;
    TestEqual(TEXT("animation family classifies"), ClassifyForInspection(Blueprint).Name, FString(TEXT("animation")));
    TestTrue(TEXT("animation inspection is admitted"), Supports(Blueprint, EOperation::Inspect));
    for (EOperation Operation : {EOperation::Create, EOperation::Compile, EOperation::Save,
        EOperation::Members, EOperation::GraphEdit, EOperation::ActionCatalog,
        EOperation::ClassDefaults, EOperation::Components, EOperation::WidgetTree})
        TestFalse(TEXT("all animation authoring stays excluded"), Supports(Blueprint, Operation));
    TestFalse(TEXT("class creation does not admit animation"), Supports(UAnimInstance::StaticClass(), EOperation::Create));

    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPError Error;
    TSharedPtr<FJsonObject> Result;
    auto Execute = [&](const TSharedRef<FJsonObject>& Args)
    {
        const bool bSuccess = Inspector.Execute(Args, Result, Error);
        if (!bSuccess) AddError(Error.Code + TEXT(": ") + Error.Message);
        return bSuccess;
    };
    TSharedRef<FJsonObject> Discover = MakeShared<FJsonObject>();
    Discover->SetStringField(TEXT("mode"), TEXT("discover"));
    Discover->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName()));
    Discover->SetStringField(TEXT("asset_name"), Blueprint->GetName());
    if (!Execute(Discover)) return false;
    TestTrue(TEXT("discovery reports animation"), Find(Result, TEXT("asset"), TEXT("blueprint_family"), TEXT("animation")).IsValid());
    TSharedRef<FJsonObject> Args = AllSectionArguments(Blueprint->GetPathName());
    Args->SetNumberField(TEXT("page_size"), 100);
    if (!Execute(Args)) return false;
    TestFalse(TEXT("bounded fixture fits one page"), Result->GetBoolField(TEXT("has_more")));
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const TSharedPtr<FJsonObject> Types = Result->GetObjectField(TEXT("family_capabilities"))->GetObjectField(TEXT("graph_types"));
    for (const TCHAR* Kind : {TEXT("event"), TEXT("animation"), TEXT("state_machine"), TEXT("animation_state"), TEXT("transition")})
    {
        TestTrue(TEXT("animation graph kind is published"), Types->GetBoolField(Kind));
        TestTrue(*FString::Printf(TEXT("%s graph is inspected"), Kind), Find(Result, TEXT("graph"), TEXT("kind"), Kind).IsValid());
    }
    TestFalse(TEXT("listing excludes graph contents"), Find(Result, TEXT("node")).IsValid());
    const TArray<TSharedPtr<FJsonValue>> Listing = Result->GetArrayField(TEXT("records"));
    TArray<TSharedPtr<FJsonValue>> DetailedRecords;
    for (const TSharedPtr<FJsonValue>& Item : Listing)
    {
        const TSharedPtr<FJsonObject> Graph = Item->AsObject();
        if (Graph->GetStringField(TEXT("section")) != TEXT("graph")) continue;
        TestTrue(TEXT("graph listing includes parameters"), Graph->HasField(TEXT("parameters")));
        TestFalse(TEXT("graph listing excludes detailed ownership"), Graph->HasField(TEXT("parent_graph_id")));
        TestFalse(TEXT("graph listing excludes node counts"), Graph->HasField(TEXT("node_count")));
        TSharedRef<FJsonObject> Detail = InspectArguments(Blueprint->GetPathName());
        Detail->SetStringField(TEXT("graph_id"), Graph->GetStringField(TEXT("id")));
        if (!Execute(Detail)) return false;
        TestEqual(TEXT("selected graph shares asset snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
        for (const TSharedPtr<FJsonValue>& Record : Result->GetArrayField(TEXT("records")))
        {
            const TSharedPtr<FJsonObject> Value = Record->AsObject();
            const FString Id = Value->GetStringField(Value->GetStringField(TEXT("section")) == TEXT("graph")
                ? TEXT("id") : TEXT("graph_id"));
            TestEqual(TEXT("details belong only to selected graph"), Id, Graph->GetStringField(TEXT("id")));
            DetailedRecords.Add(Record);
        }
    }
    Result->SetArrayField(TEXT("records"), DetailedRecords);
    TestTrue(TEXT("pose and state links are inspected"), Find(Result, TEXT("connection")).IsValid());
    const TSharedPtr<FJsonObject> State = Find(Result, TEXT("graph"), TEXT("name"), TEXT("Idle"));
    if (!TestTrue(TEXT("nested Idle graph exists"), State.IsValid())) return false;
    const FString StateId = State->GetStringField(TEXT("id"));
    TestFalse(TEXT("state has parent graph identity"), State->GetStringField(TEXT("parent_graph_id")).IsEmpty());
    const TSharedPtr<FJsonObject> StateNode = Find(Result, TEXT("node"), TEXT("bound_graph_id"), StateId);
    if (!TestTrue(TEXT("state node links to bound graph"), StateNode.IsValid())) return false;
    TestTrue(TEXT("relationship identity has canonical case"),
        StateNode->GetStringField(TEXT("bound_graph_id")).Equals(StateId, ESearchCase::CaseSensitive));
    TSharedRef<FJsonObject> Named = InspectArguments(Blueprint->GetPathName(), 1);
    Named->SetStringField(TEXT("graph_name"), TEXT("Idle"));
    if (!Execute(Named)) return false;
    TestEqual(TEXT("name resolves exact nested identity"), Find(Result, TEXT("graph"))->GetStringField(TEXT("id")), StateId);
    TSharedRef<FJsonObject> NamedCursor = MakeShared<FJsonObject>();
    NamedCursor->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    if (!Execute(NamedCursor)) return false;
    TestTrue(TEXT("named cursor continues details"), Find(Result, TEXT("node")).IsValid());
    Named->SetStringField(TEXT("graph_name"), TEXT("idle"));
    TestFalse(TEXT("graph names are exact case"), Inspector.Execute(Named, Result, Error));
    TestEqual(TEXT("missing name error"), Error.Code, FString(TEXT("not_found")));
    Named->SetStringField(TEXT("graph_name"), TEXT("Idle"));
    Named->SetStringField(TEXT("graph_id"), StateId);
    TestFalse(TEXT("mixed graph selectors reject"), Inspector.Execute(Named, Result, Error));
    TestEqual(TEXT("mixed selector error"), Error.Code, FString(TEXT("invalid_argument")));
    Named->RemoveField(TEXT("graph_id"));
    for (const FString& InvalidName : {FString(), FString::ChrN(129, TCHAR('g'))})
    {
        Named->SetStringField(TEXT("graph_name"), InvalidName);
        TestFalse(TEXT("invalid graph name rejects"), Inspector.Execute(Named, Result, Error));
        TestEqual(TEXT("invalid name error"), Error.Code, FString(TEXT("invalid_argument")));
    }
    for (const TCHAR* Section : {TEXT("nodes"), TEXT("pins"), TEXT("connections")})
    {
        TSharedRef<FJsonObject> Unscoped = InspectArguments(Blueprint->GetPathName());
        Unscoped->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(Section)});
        TestFalse(TEXT("unscoped details reject"), Inspector.Execute(Unscoped, Result, Error));
        TestEqual(TEXT("unscoped details error"), Error.Code, FString(TEXT("invalid_argument")));
    }
    Args->SetStringField(TEXT("graph_id"), StateId);
    if (!Execute(Args)) return false;
    for (const TSharedPtr<FJsonValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        FString Id;
        if (Item->AsObject()->TryGetStringField(TEXT("graph_id"), Id))
            TestEqual(TEXT("exact filter scopes graph records"), Id, StateId);
    }
    Args->SetStringField(TEXT("graph_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TestFalse(TEXT("missing graph rejects"), Inspector.Execute(Args, Result, Error));
    TestEqual(TEXT("missing graph error"), Error.Code, FString(TEXT("not_found")));
    Args->RemoveField(TEXT("graph_id"));
    Args->SetNumberField(TEXT("page_size"), 1);
    if (!Execute(Args)) return false;
    TSharedRef<FJsonObject> Cursor = MakeShared<FJsonObject>();
    Cursor->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    if (!Execute(Cursor)) return false;
    TestEqual(TEXT("cursor retains family"), Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("animation")));
    TestEqual(TEXT("cursor retains snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
    TestFalse(TEXT("cursor cannot be reused"), Inspector.Execute(Cursor, Result, Error));
    TestEqual(TEXT("single-use error"), Error.Code, FString(TEXT("cursor_expired")));
    if (!Execute(Args)) return false;
    Cursor->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    TArray<TPair<UEdGraph*, FString>> Graphs;
    TestTrue(TEXT("bounded traversal succeeds"), UnrealMCP::BlueprintInspectionPrivate::AddBlueprintGraphs(Blueprint, Blueprint->GetPathName(), Graphs));
    UEdGraph* Nested = nullptr;
    for (const auto& Graph : Graphs) if (Graph.Key->GraphGuid.ToString(EGuidFormats::Digits) == StateId) Nested = Graph.Key;
    if (!TestNotNull(TEXT("nested graph resolves"), Nested) || Nested->Nodes.IsEmpty()) return false;
    ++Nested->Nodes[0]->NodePosX;
    TestFalse(TEXT("nested node change invalidates cursor"), Inspector.Execute(Cursor, Result, Error));
    TestEqual(TEXT("nested stale error"), Error.Code, FString(TEXT("stale_precondition")));
    --Nested->Nodes[0]->NodePosX;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TestFalse(TEXT("compile entry point rejects animation"), Mutator.Execute(TEXT("blueprint_compile"), AssetArguments(Blueprint->GetPathName()), Result, Error));
    TestEqual(TEXT("mutation rejection is stable"), Error.Code, FString(TEXT("wrong_type")));
    TestEqual(TEXT("inspection and rejection preserve snapshot"), InspectSnapshot(Inspector, Blueprint->GetPathName()), Snapshot);
    TestEqual(TEXT("inspection preserves dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirty);
    TestEqual(TEXT("inspection preserves compile status"), Blueprint->Status, Status);
    Named->SetStringField(TEXT("graph_name"), TEXT("Idle"));
    if (!Execute(Named)) return false;
    NamedCursor->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    Nested->Rename(TEXT("RenamedIdle"), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty);
    TestFalse(TEXT("renaming named graph invalidates cursor"), Inspector.Execute(NamedCursor, Result, Error));
    TestEqual(TEXT("renamed graph cursor error"), Error.Code, FString(TEXT("stale_precondition")));
    Nested->Rename(TEXT("Idle"), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty);
    UEdGraph* DuplicateName = NewObject<UEdGraph>(Blueprint, TEXT("Idle"));
    DuplicateName->GraphGuid = FGuid::NewGuid();
    Blueprint->FunctionGraphs.Add(DuplicateName);
    Named->SetStringField(TEXT("graph_name"), TEXT("Idle"));
    TestFalse(TEXT("duplicate nested name rejects"), Inspector.Execute(Named, Result, Error));
    TestEqual(TEXT("ambiguous name error"), Error.Code, FString(TEXT("invalid_argument")));
    Named->RemoveField(TEXT("graph_name"));
    Named->SetStringField(TEXT("graph_id"), StateId);
    TestTrue(TEXT("identity disambiguates nested names"), Inspector.Execute(Named, Result, Error));
    Blueprint->FunctionGraphs.Remove(DuplicateName);
    // Malformed repeated/cyclic child edges must remain bounded and deduplicated.
    Nested->SubGraphs.Add(Nested);
    TArray<TPair<UEdGraph*, FString>> CyclicGraphs;
    TestTrue(TEXT("cycle terminates"), UnrealMCP::BlueprintInspectionPrivate::AddBlueprintGraphs(Blueprint, Blueprint->GetPathName(), CyclicGraphs));
    TestEqual(TEXT("cycle does not duplicate graphs"), CyclicGraphs.Num(), Graphs.Num());
    Nested->SubGraphs.Pop();
    TArray<TPair<UEdGraph*, FString>> FullGraphs;
    FullGraphs.SetNum(UnrealMCP::MaxInspectRecords);
    TestFalse(TEXT("graph traversal enforces structural bound"), UnrealMCP::BlueprintInspectionPrivate::AddBlueprintGraphs(Blueprint, Blueprint->GetPathName(), FullGraphs));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPAnimationLiveFixtureTest,
    "UnrealMCP.Animation.AnimationLiveFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPAnimationLiveFixtureTest::RunTest(const FString& Parameters)
{
    UAnimBlueprint* Blueprint = UnrealMCP::Tests::Animation::CreateFixture(TEXT("/Game/UnrealMCPTests/ABP_AnimationFixture"));
    return TestNotNull(TEXT("live animation fixture creates"), Blueprint)
        && TestTrue(TEXT("live animation fixture saves"), UnrealMCP::Tests::SaveBlueprintFixture(Blueprint));
}

#endif
