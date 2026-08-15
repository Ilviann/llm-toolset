#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateGraphSchema.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimationTransitionSchema.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UnrealMCPAnimationInspectionAdapter.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UObject/SavePackage.h"

namespace
{
const TSharedPtr<FUnrealMCPValue>* Find(
    const FUnrealMCPAssetFamilyDocumentBuilder& Document,
    const FString& Path)
{
    const FUnrealMCPAssetFamilyValueRecord* Record = Document.GetRecords().FindByPredicate(
        [&Path](const FUnrealMCPAssetFamilyValueRecord& Candidate) { return Candidate.Path == Path; });
    return Record != nullptr ? &Record->Value : nullptr;
}

TSharedPtr<FUnrealMCPRecord> ObjectAt(
    const FUnrealMCPAssetFamilyDocumentBuilder& Document,
    const FString& Path)
{
    const TSharedPtr<FUnrealMCPValue>* Value = Find(Document, Path);
    return Value != nullptr && Value->IsValid() ? (*Value)->AsObject() : nullptr;
}

bool Inspect(
    const FUnrealMCPAssetFamilyDescriptor& Descriptor,
    UAnimBlueprint* Blueprint,
    const TArray<FString>& Selector,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPError& Error,
    bool bVerbose = false)
{
    FUnrealMCPAssetFamilyInspectionContext Context;
    Context.Asset = Blueprint;
    Context.Identity = {Blueprint->GetPathName(), Descriptor.SnapshotBuilder(Blueprint)};
    Context.Selector.Segments = Selector;
    Context.bVerbose = bVerbose;
    FUnrealMCPAssetFamilySelectorRouter Router(Descriptor.Bounds);
    FUnrealMCPAssetFamilySnapshotBuilder Snapshot(Descriptor.Bounds);
    return Descriptor.InspectionAdapter->Inspect(Context, Document, Router, Snapshot, Error);
}

UAnimationGraph* AddAnimGraph(UAnimBlueprint* Blueprint, const TCHAR* Name)
{
    UAnimationGraph* Graph = NewObject<UAnimationGraph>(Blueprint, Name, RF_Transactional);
    Graph->Schema = UAnimationGraphSchema::StaticClass();
    Blueprint->FunctionGraphs.Add(Graph);
    UAnimGraphNode_Root* Root = NewObject<UAnimGraphNode_Root>(Graph);
    Root->CreateNewGuid();
    Root->AllocateDefaultPins();
    Graph->AddNode(Root, false, false);
    return Graph;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetInspectAnimationTest,
    "UnrealMCP.AssetInspect.AnimationBlueprintGraphsStatesAndExclusions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetInspectAnimationTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(*(TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/ABP_Inspect")));
    UAnimBlueprint* Blueprint = NewObject<UAnimBlueprint>(
        Package, TEXT("ABP_Inspect"), RF_Public | RF_Standalone | RF_Transactional);
    Blueprint->ParentClass = UAnimInstance::StaticClass();
    Blueprint->bUseMultiThreadedAnimationUpdate = true;
    Blueprint->bWarnAboutBlueprintUsage = true;
    FAnimGroupInfo Group;
    Group.Name = TEXT("Locomotion");
    Blueprint->Groups.Add(Group);
    UAnimationGraph* Graph = nullptr;
    for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
        if (UAnimationGraph* Existing = Cast<UAnimationGraph>(Candidate))
            if (Existing->GetName() == TEXT("AnimGraph")) { Graph = Existing; break; }
    if (Graph == nullptr) Graph = AddAnimGraph(Blueprint, TEXT("AnimGraph"));

    UAnimGraphNode_StateMachine* StateMachine = NewObject<UAnimGraphNode_StateMachine>(Graph);
    StateMachine->CreateNewGuid();
    StateMachine->AllocateDefaultPins();
    StateMachine->EditorStateMachineGraph = NewObject<UAnimationStateMachineGraph>(StateMachine, TEXT("Locomotion"));
    StateMachine->EditorStateMachineGraph->Schema = UAnimationStateMachineSchema::StaticClass();
    Graph->AddNode(StateMachine, false, false);
    UAnimStateNode* Idle = NewObject<UAnimStateNode>(StateMachine->EditorStateMachineGraph);
    Idle->CreateNewGuid(); Idle->AllocateDefaultPins();
    Idle->BoundGraph = NewObject<UAnimationStateGraph>(Idle, TEXT("Idle"));
    Idle->BoundGraph->Schema = UAnimationStateGraphSchema::StaticClass();
    Idle->StateEntered.NotifyName = TEXT("EnterIdle");
    StateMachine->EditorStateMachineGraph->AddNode(Idle, false, false);
    UAnimStateEntryNode* Entry = NewObject<UAnimStateEntryNode>(StateMachine->EditorStateMachineGraph);
    Entry->CreateNewGuid(); Entry->AllocateDefaultPins();
    StateMachine->EditorStateMachineGraph->AddNode(Entry, false, false);
    StateMachine->EditorStateMachineGraph->EntryNode = Entry;
    StateMachine->EditorStateMachineGraph->GetSchema()->TryCreateConnection(
        Entry->GetOutputPin(), Idle->GetInputPin());
    UAnimStateNode* Run = NewObject<UAnimStateNode>(StateMachine->EditorStateMachineGraph);
    Run->CreateNewGuid(); Run->AllocateDefaultPins();
    Run->BoundGraph = NewObject<UAnimationStateGraph>(Run, TEXT("Run"));
    Run->BoundGraph->Schema = UAnimationStateGraphSchema::StaticClass();
    StateMachine->EditorStateMachineGraph->AddNode(Run, false, false);
    UAnimStateTransitionNode* Transition = NewObject<UAnimStateTransitionNode>(StateMachine->EditorStateMachineGraph);
    Transition->CreateNewGuid(); Transition->AllocateDefaultPins();
    Transition->BoundGraph = NewObject<UAnimationTransitionGraph>(Transition, TEXT("IdleToRunRule"));
    Transition->BoundGraph->Schema = UAnimationTransitionSchema::StaticClass();
    Transition->PriorityOrder = 2;
    Transition->CrossfadeDuration = 0.25f;
    Transition->MinTimeBeforeReentry = 0.5f;
    Transition->SyncGroupNameToRequireValidMarkersRule = TEXT("Locomotion");
    Transition->TransitionStart.NotifyName = TEXT("StartRun");
    StateMachine->EditorStateMachineGraph->AddNode(Transition, false, false);
    Transition->CreateConnections(Idle, Run);

    FUnrealMCPAssetFamilyRegistry Registry;
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("animation adapter registers"),
            UnrealMCP::AnimationInspection::RegisterAdapter(Registry, Error))
        || !TestTrue(TEXT("animation registry freezes"), Registry.Freeze(Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    const FUnrealMCPAssetFamilyDescriptor& Descriptor = Registry.GetDescriptors()[0];
    TestTrue(TEXT("animation family composes over core Blueprint"), Descriptor.bComposableInspectionOverlay);
    TestEqual(TEXT("animation family targets AnimInstance"), Descriptor.NativeClass, UAnimInstance::StaticClass());
    const bool bDirtyBefore = Package->IsDirty();

    FUnrealMCPAssetFamilyDocumentBuilder Root(Descriptor.Bounds);
    if (!TestTrue(TEXT("regular Animation Blueprint root inspects"),
        Inspect(Descriptor, Blueprint, {}, Root, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    TSharedPtr<FUnrealMCPRecord> Animation = ObjectAt(Root, TEXT("animation_blueprint"));
    TestEqual(TEXT("regular mode is explicit"), Animation->GetStringField(TEXT("mode")), FString(TEXT("regular")));
    TestTrue(TEXT("threaded update request is semantic"),
        Animation->GetObjectField(TEXT("threaded_update"))->GetBoolField(TEXT("requested")));
    TestEqual(TEXT("sync groups are bounded"),
        Animation->GetObjectField(TEXT("sync_groups"))->GetIntegerField(TEXT("count")), 1);
    TestEqual(TEXT("root indexes one pose graph"), (*Find(Root, TEXT("animation_graphs")))->AsArray().Num(), 1);
    TestEqual(TEXT("root indexes one state machine"), (*Find(Root, TEXT("state_machines")))->AsArray().Num(), 1);

    FUnrealMCPAssetFamilyDocumentBuilder Pose(Descriptor.Bounds);
    TestTrue(TEXT("pose graph uses shared atomic graph contract"),
        Inspect(Descriptor, Blueprint, {TEXT("animation_graphs"), TEXT("AnimGraph")}, Pose, Error, true));
    TestTrue(TEXT("pose graph is complete"), ObjectAt(Pose, TEXT("graph"))
        ->GetObjectField(TEXT("graph_status"))->GetBoolField(TEXT("complete")));
    TestFalse(TEXT("pose graph excludes live pose state"), Find(Pose, TEXT("active_state")) != nullptr);
    TestFalse(TEXT("pose graph excludes media"), Find(Pose, TEXT("thumbnail")) != nullptr);

    const TSharedPtr<FUnrealMCPRecord> MachineIndex = (*Find(Root, TEXT("state_machines")))
        ->AsArray()[0]->AsObject();
    const FString MachineSelector = MachineIndex->GetStringField(TEXT("selector"));
    TArray<FString> MachineSegments;
    MachineSelector.ParseIntoArray(MachineSegments, TEXT("/"), false);
    FUnrealMCPAssetFamilyDocumentBuilder Machine(Descriptor.Bounds);
    TestTrue(TEXT("state-machine topology inspects"),
        Inspect(Descriptor, Blueprint, MachineSegments, Machine, Error));
    TestEqual(TEXT("state-machine states are connected"),
        ObjectAt(Machine, TEXT("state_machine"))->GetArrayField(TEXT("states")).Num(), 2);
    TestEqual(TEXT("state-machine transition is connected"),
        ObjectAt(Machine, TEXT("state_machine"))->GetArrayField(TEXT("transitions")).Num(), 1);
    const TSharedPtr<FUnrealMCPRecord> StateRecord = ObjectAt(Machine, TEXT("state_machine"))
        ->GetArrayField(TEXT("states"))[0]->AsObject();
    TestEqual(TEXT("state notifications are semantic"), StateRecord
        ->GetObjectField(TEXT("notifications"))->GetObjectField(TEXT("entered"))
        ->GetStringField(TEXT("name")), FString(TEXT("EnterIdle")));
    const TSharedPtr<FUnrealMCPRecord> TransitionRecord = ObjectAt(Machine, TEXT("state_machine"))
        ->GetArrayField(TEXT("transitions"))[0]->AsObject();
    TestEqual(TEXT("transition reentry delay is semantic"),
        TransitionRecord->GetNumberField(TEXT("minimum_reentry_time_seconds")), 0.5);
    TestEqual(TEXT("transition marker policy is semantic"),
        TransitionRecord->GetStringField(TEXT("required_marker_sync_group")), FString(TEXT("Locomotion")));

    Blueprint->bIsTemplate = true;
    FUnrealMCPAssetFamilyDocumentBuilder Template(Descriptor.Bounds);
    TestTrue(TEXT("template root inspects"), Inspect(Descriptor, Blueprint, {}, Template, Error));
    TestEqual(TEXT("template mode is explicit"), ObjectAt(Template, TEXT("animation_blueprint"))
        ->GetStringField(TEXT("mode")), FString(TEXT("template")));
    Blueprint->bIsTemplate = false;
    Blueprint->BlueprintType = BPTYPE_Interface;
    FUnrealMCPAssetFamilyDocumentBuilder Interface(Descriptor.Bounds);
    TestTrue(TEXT("Animation Layer Interface root inspects"), Inspect(Descriptor, Blueprint, {}, Interface, Error));
    TestEqual(TEXT("interface mode is explicit"), ObjectAt(Interface, TEXT("animation_blueprint"))
        ->GetStringField(TEXT("mode")), FString(TEXT("interface")));
    TestEqual(TEXT("inspection preserves dirty state"), Package->IsDirty(), bDirtyBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetInspectAnimationLiveFixtureTest,
    "UnrealMCP.AssetInspect.AnimationBlueprintLiveFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetInspectAnimationLiveFixtureTest::RunTest(const FString& Parameters)
{
    const TCHAR* PackageName = TEXT("/Game/UnrealMCPAnimation/ABP_InspectionFixture");
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    TestTrue(TEXT("existing Animation Blueprint fixture is removed"),
        !IFileManager::Get().FileExists(*Filename) || IFileManager::Get().Delete(*Filename, false, true));
    UPackage* Package = CreatePackage(PackageName);
    UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UAnimInstance::StaticClass(), Package, TEXT("ABP_InspectionFixture"), BPTYPE_Normal,
        UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass()));
    TestNotNull(TEXT("saved Animation Blueprint fixture is created"), Blueprint);
    if (Blueprint == nullptr) return false;
    UAnimationGraph* Graph = nullptr;
    for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
        if (UAnimationGraph* Existing = Cast<UAnimationGraph>(Candidate))
            if (Existing->GetName() == TEXT("AnimGraph")) { Graph = Existing; break; }
    if (Graph == nullptr) Graph = AddAnimGraph(Blueprint, TEXT("AnimGraph"));
    UAnimGraphNode_StateMachine* StateMachine = NewObject<UAnimGraphNode_StateMachine>(Graph);
    StateMachine->CreateNewGuid();
    StateMachine->AllocateDefaultPins();
    StateMachine->EditorStateMachineGraph = NewObject<UAnimationStateMachineGraph>(
        StateMachine, TEXT("Locomotion"));
    StateMachine->EditorStateMachineGraph->Schema = UAnimationStateMachineSchema::StaticClass();
    Graph->AddNode(StateMachine, false, false);
    UAnimStateNode* Idle = NewObject<UAnimStateNode>(StateMachine->EditorStateMachineGraph);
    Idle->CreateNewGuid(); Idle->AllocateDefaultPins();
    Idle->BoundGraph = NewObject<UAnimationStateGraph>(Idle, TEXT("Idle"));
    Idle->BoundGraph->Schema = UAnimationStateGraphSchema::StaticClass();
    StateMachine->EditorStateMachineGraph->AddNode(Idle, false, false);
    UAnimStateEntryNode* Entry = NewObject<UAnimStateEntryNode>(StateMachine->EditorStateMachineGraph);
    Entry->CreateNewGuid(); Entry->AllocateDefaultPins();
    StateMachine->EditorStateMachineGraph->AddNode(Entry, false, false);
    StateMachine->EditorStateMachineGraph->EntryNode = Entry;
    StateMachine->EditorStateMachineGraph->GetSchema()->TryCreateConnection(
        Entry->GetOutputPin(), Idle->GetInputPin());
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FAssetRegistryModule::AssetCreated(Blueprint);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    TestTrue(TEXT("saved Animation Blueprint fixture persists"),
        UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs));
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_ANIMATION_FIXTURE=%s"), *Blueprint->GetPathName());
    return true;
}

#endif
