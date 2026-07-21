#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase2InspectionTest, "UnrealMCP.Phase2.InspectionContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase2InspectionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PluginContentDirectory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPPluginContent"));
    IFileManager::Get().MakeDirectory(*PluginContentDirectory, true);
    FPackageName::RegisterMountPoint(TEXT("/UnrealMCPTestPlugin/"), PluginContentDirectory + TEXT("/"));
    UBlueprint* ActorBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_Actor"), AActor::StaticClass(), true);
    UBlueprint* ObjectBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_Object"), UObject::StaticClass(), false);
    UBlueprint* ChildBlueprint = ActorBlueprint != nullptr
        ? CreateBlueprintFixture(Base + TEXT("/BP_Child"), ActorBlueprint->GeneratedClass, false) : nullptr;
    UPackage* PlainPackage = CreatePackage(*(Base + TEXT("/PlainAsset")));
    UObject* PlainAsset = NewObject<UTexture2D>(PlainPackage, TEXT("PlainAsset"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(PlainAsset);
    UBlueprint* PluginBlueprint = CreateBlueprintFixture(TEXT("/UnrealMCPTestPlugin/BP_PluginActor"), AActor::StaticClass(), true);
    TestNotNull(TEXT("Actor Blueprint fixture is created"), ActorBlueprint);
    TestNotNull(TEXT("non-Actor Blueprint fixture is created"), ObjectBlueprint);
    TestNotNull(TEXT("inherited Actor Blueprint fixture is created"), ChildBlueprint);
    TestNotNull(TEXT("mounted plugin Actor Blueprint fixture is created"), PluginBlueprint);
    if (ActorBlueprint == nullptr || ObjectBlueprint == nullptr || ChildBlueprint == nullptr || PluginBlueprint == nullptr)
    {
        FPackageName::UnRegisterMountPoint(TEXT("/UnrealMCPTestPlugin/"), PluginContentDirectory + TEXT("/"));
        return false;
    }

    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const FString ActorPath = ActorBlueprint->GetPathName();
    const bool bDirtyBefore = ActorBlueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = ActorBlueprint->Status;
    const int32 SelectionBefore = GEditor != nullptr ? GEditor->GetSelectedObjects()->Num() : 0;
    const int32 TransactionsBefore = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TestTrue(TEXT("complete structure inspection succeeds"), Inspector.Execute(AllSectionArguments(ActorPath), Result, Error));
    for (const TCHAR* Section : {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("component"),
        TEXT("variable"), TEXT("graph"), TEXT("node"), TEXT("pin")})
    {
        TestTrue(FString::Printf(TEXT("inspection includes %s"), Section), ResultHasSection(Result, Section));
    }
    TestTrue(TEXT("unsupported K2 types are explicit"), ResultHasUnsupportedType(Result));
    TestEqual(TEXT("inspection preserves package dirty state"), ActorBlueprint->GetOutermost()->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("inspection preserves compile state"), ActorBlueprint->Status, StatusBefore);
    TestEqual(TEXT("inspection preserves selection"), GEditor != nullptr ? GEditor->GetSelectedObjects()->Num() : 0, SelectionBefore);
    TestEqual(TEXT("inspection creates no transaction"), GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBefore);

    const TSharedRef<FJsonObject> Discover = MakeShared<FJsonObject>();
    Discover->SetStringField(TEXT("mode"), TEXT("discover"));
    Discover->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Base));
    Discover->SetStringField(TEXT("asset_name"), ActorBlueprint->GetName());
    TestTrue(TEXT("exact discovery succeeds"), Inspector.Execute(Discover, Result, Error));
    TestTrue(TEXT("exact discovery finds Actor Blueprint"), ResultHasSection(Result, TEXT("asset")));

    Discover->RemoveField(TEXT("package_path"));
    Discover->SetStringField(TEXT("asset_name"), PluginBlueprint->GetName());
    TestTrue(TEXT("all-mount discovery succeeds"), Inspector.Execute(Discover, Result, Error));
    TestTrue(TEXT("all-mount discovery finds plugin Actor Blueprint"), ResultHasSection(Result, TEXT("asset")));
    Discover->SetStringField(TEXT("package_path"), TEXT("/UnrealMCPTestPlugin"));
    TestTrue(TEXT("plugin-mount discovery succeeds"), Inspector.Execute(Discover, Result, Error));
    TestTrue(TEXT("plugin-mount discovery finds Actor Blueprint"), ResultHasSection(Result, TEXT("asset")));
    TestTrue(TEXT("plugin-mount deep inspection succeeds"), Inspector.Execute(AllSectionArguments(PluginBlueprint->GetPathName()), Result, Error));
    TestTrue(TEXT("plugin-mount deep inspection returns structure"), ResultHasSection(Result, TEXT("graph")));

    Discover->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Base));
    Discover->SetStringField(TEXT("asset_name"), ObjectBlueprint->GetName());
    TestTrue(TEXT("non-Actor discovery is a valid empty query"), Inspector.Execute(Discover, Result, Error));
    TestFalse(TEXT("non-Actor Blueprint is excluded from discovery"), ResultHasSection(Result, TEXT("asset")));
    TestFalse(TEXT("non-Actor deep inspection is rejected"), Inspector.Execute(InspectArguments(ObjectBlueprint->GetPathName()), Result, Error));
    TestEqual(TEXT("non-Actor error is stable"), Error.Code, FString(TEXT("wrong_type")));
    TestFalse(TEXT("missing asset is rejected"), Inspector.Execute(InspectArguments(Base + TEXT("/Missing.Missing")), Result, Error));
    TestEqual(TEXT("missing error is stable"), Error.Code, FString(TEXT("not_found")));
    TestFalse(TEXT("non-Blueprint asset is rejected"), Inspector.Execute(InspectArguments(PlainAsset->GetPathName()), Result, Error));
    TestEqual(TEXT("non-Blueprint error is stable"), Error.Code, FString(TEXT("wrong_type")));

    TSharedRef<FJsonObject> Inherited = InspectArguments(ChildBlueprint->GetPathName());
    Inherited->SetBoolField(TEXT("include_inherited"), true);
    Inherited->SetArrayField(TEXT("sections"), {
        MakeShared<FJsonValueString>(TEXT("components")), MakeShared<FJsonValueString>(TEXT("variables"))});
    TestTrue(TEXT("inherited inspection succeeds"), Inspector.Execute(Inherited, Result, Error));
    TestTrue(TEXT("inherited component is reported"), ResultHasSection(Result, TEXT("component")));
    TestTrue(TEXT("inherited variable is reported"), ResultHasSection(Result, TEXT("variable")));

    UBlueprint* EmptyGraphBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_EmptyGraphs"), AActor::StaticClass(), false);
    EmptyGraphBlueprint->UbergraphPages.Empty();
    EmptyGraphBlueprint->FunctionGraphs.Empty();
    EmptyGraphBlueprint->MacroGraphs.Empty();
    EmptyGraphBlueprint->DelegateSignatureGraphs.Empty();
    TestTrue(TEXT("empty graph inspection succeeds"), Inspector.Execute(AllSectionArguments(EmptyGraphBlueprint->GetPathName()), Result, Error));
    TestFalse(TEXT("empty graph inspection returns no graph record"), ResultHasSection(Result, TEXT("graph")));

    UBlueprint* LargeBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_Large"), AActor::StaticClass(), false);
    UEdGraph* LargeGraph = LargeBlueprint->UbergraphPages[0];
    for (int32 Index = 0; Index <= UnrealMCP::MaxInspectRecords; ++Index)
    {
        UEdGraphNode* Node = NewObject<UEdGraphNode>(LargeGraph);
        Node->CreateNewGuid();
        LargeGraph->AddNode(Node, false, false);
    }
    TestFalse(TEXT("oversized synthetic graph rejects"), Inspector.Execute(InspectArguments(LargeBlueprint->GetPathName()), Result, Error));
    TestEqual(TEXT("oversized graph error is stable"), Error.Code, FString(TEXT("response_too_large")));
    FPackageName::UnRegisterMountPoint(TEXT("/UnrealMCPTestPlugin/"), PluginContentDirectory + TEXT("/"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase2CursorTest, "UnrealMCP.Phase2.CursorGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase2CursorTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Cursor");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("cursor Blueprint fixture is created"), Blueprint)) return false;
    double CurrentTime = 10.0;
    FUnrealMCPBlueprintInspector Inspector([&CurrentTime] { return CurrentTime; });
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    TestTrue(TEXT("first small page succeeds"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName(), 1), Result, Error));
    FString Cursor;
    TestTrue(TEXT("first page returns opaque cursor"), Result->TryGetStringField(TEXT("next_cursor"), Cursor));
    const TSharedRef<FJsonObject> Continue = MakeShared<FJsonObject>();
    Continue->SetStringField(TEXT("cursor"), Cursor);
    TestTrue(TEXT("matching cursor continues"), Inspector.Execute(Continue, Result, Error));

    TestTrue(TEXT("fresh cursor page succeeds"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName(), 1), Result, Error));
    Result->TryGetStringField(TEXT("next_cursor"), Cursor);
    Blueprint->UbergraphPages[0]->Nodes[0]->NodePosX += 1;
    Continue->SetStringField(TEXT("cursor"), Cursor);
    TestFalse(TEXT("structurally stale cursor rejects"), Inspector.Execute(Continue, Result, Error));
    TestEqual(TEXT("stale cursor error is stable"), Error.Code, FString(TEXT("stale_precondition")));

    TestTrue(TEXT("another fresh cursor page succeeds"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName(), 1), Result, Error));
    Result->TryGetStringField(TEXT("next_cursor"), Cursor);
    CurrentTime += UnrealMCP::CursorLifetimeSeconds + 1.0;
    Continue->SetStringField(TEXT("cursor"), Cursor);
    TestFalse(TEXT("expired cursor rejects"), Inspector.Execute(Continue, Result, Error));
    TestEqual(TEXT("expired cursor error is stable"), Error.Code, FString(TEXT("cursor_expired")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase2LiveFixtureTest, "UnrealMCP.Phase2.LiveFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase2LiveFixtureTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    UBlueprint* Blueprint = CreateBlueprintFixture(TEXT("/Game/UnrealMCPPhase2/BP_InspectionFixture"), AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("live Actor Blueprint fixture is available"), Blueprint)) return false;
    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> BeforeSave;
    FUnrealMCPError Error;
    TestTrue(TEXT("fixture inspects before save"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), BeforeSave, Error));
    const FString SnapshotBefore = BeforeSave.IsValid() ? BeforeSave->GetStringField(TEXT("snapshot_id")) : FString();
    if (Blueprint->UbergraphPages.Num() > 0 && Blueprint->UbergraphPages[0]->Nodes.Num() > 0 && GEditor != nullptr)
    {
        UEdGraph* Graph = Blueprint->UbergraphPages[0];
        UEdGraphNode* Node = Graph->Nodes[0];
        {
            const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP identity undo test")));
            Graph->Modify();
            Node->Modify();
            Node->NodePosX += 64;
        }
        TestTrue(TEXT("test transaction undoes"), GEditor->UndoTransaction());
        TSharedPtr<FJsonObject> AfterUndo;
        TestTrue(TEXT("fixture inspects after undo"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), AfterUndo, Error));
        TestEqual(TEXT("undo restores structural snapshot"), AfterUndo->GetStringField(TEXT("snapshot_id")), SnapshotBefore);
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TSharedPtr<FJsonObject> AfterCompile;
    TestTrue(TEXT("fixture inspects after reconstruction compile"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), AfterCompile, Error));
    TestEqual(TEXT("compile preserves available identities"), AfterCompile->GetStringField(TEXT("snapshot_id")), SnapshotBefore);
    TestTrue(TEXT("fixture saves without UI"), SaveBlueprintFixture(Blueprint));
    TSharedPtr<FJsonObject> AfterSave;
    TestTrue(TEXT("fixture inspects after save"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), AfterSave, Error));
    const FString SnapshotAfter = AfterSave.IsValid() ? AfterSave->GetStringField(TEXT("snapshot_id")) : FString();
    TestEqual(TEXT("save preserves structural snapshot"), SnapshotAfter, SnapshotBefore);
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_PHASE2_SNAPSHOT=%s"), *SnapshotAfter);
    return true;
}


#endif
