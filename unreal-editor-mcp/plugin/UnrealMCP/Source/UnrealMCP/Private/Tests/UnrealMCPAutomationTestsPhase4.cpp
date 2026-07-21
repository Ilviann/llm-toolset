#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase4OperationLedgerTest, "UnrealMCP.Phase4.OperationLedger", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase4OperationLedgerTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    double CurrentTime = 10.0;
    const FString BridgeId = TEXT("0123456789abcdef0123456789abcdef");
    FUnrealMCPOperationLedger Ledger(BridgeId, TEXT("bounded-test-context"), [&CurrentTime] { return CurrentTime; });
    const FString OperationId = TEXT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), OperationId);
    Arguments->SetStringField(TEXT("asset_path"), TEXT("/Game/BP_Test.BP_Test"));
    FUnrealMCPOperationAdmission Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("new operation is accepted"), Admission.Kind, EUnrealMCPOperationAdmission::Accepted);
    FUnrealMCPError Error;
    TestTrue(TEXT("accepted operation starts executing"), Ledger.MarkExecuting(OperationId, Error));
    const TSharedRef<FJsonObject> Committed = MakeShared<FJsonObject>();
    Committed->SetStringField(TEXT("snapshot_id"), FString::ChrN(40, TEXT('b')));
    Ledger.Commit(OperationId, Committed);
    Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("same request replays"), Admission.Kind, EUnrealMCPOperationAdmission::ReplaySuccess);
    TestTrue(TEXT("replay returns retained result"), Admission.Result == Committed);

    Arguments->SetStringField(TEXT("asset_path"), TEXT("/Game/BP_Other.BP_Other"));
    Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("conflicting ID reuse rejects"), Admission.Kind, EUnrealMCPOperationAdmission::Conflict);
    TestEqual(TEXT("conflict code is stable"), Admission.Error->Code, FString(TEXT("operation_conflict")));

    const FString CancelId = TEXT("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const TSharedRef<FJsonObject> Queued = MakeShared<FJsonObject>();
    Queued->SetStringField(TEXT("operation_id"), CancelId);
    Ledger.Admit(TEXT("blueprint_compile"), Queued);
    const TSharedRef<FJsonObject> StatusArguments = MakeShared<FJsonObject>();
    StatusArguments->SetStringField(TEXT("operation_id"), CancelId);
    StatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    StatusArguments->SetBoolField(TEXT("cancel"), true);
    TSharedPtr<FJsonObject> Status;
    TestTrue(TEXT("queued cancellation resolves"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("queued operation becomes cancelled"), Status->GetStringField(TEXT("state")), FString(TEXT("cancelled")));
    TestFalse(TEXT("cancelled operation never executes"), Ledger.MarkExecuting(CancelId, Error));

    StatusArguments->SetStringField(TEXT("bridge_instance_id"), TEXT("cccccccccccccccccccccccccccccccc"));
    TestTrue(TEXT("another bridge instance resolves safely"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("bridge restart returns unknown outcome"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));
    CurrentTime += UnrealMCP::OperationLifetimeSeconds + 1.0;
    StatusArguments->SetStringField(TEXT("operation_id"), OperationId);
    StatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    StatusArguments->RemoveField(TEXT("cancel"));
    TestTrue(TEXT("expired result resolves safely"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("expired result becomes unknown"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));

    FUnrealMCPOperationLedger BoundedLedger(BridgeId, TEXT("bounded-capacity-context"), [] { return 20.0; });
    for (int32 Index = 0; Index < UnrealMCP::MaxRetainedOperations + 1; ++Index)
    {
        const FString Id = FString::Printf(TEXT("%032x"), Index + 1);
        const TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("operation_id"), Id);
        FUnrealMCPOperationAdmission CapacityAdmission = BoundedLedger.Admit(TEXT("blueprint_save"), Request);
        TestEqual(TEXT("capacity admits by evicting the oldest terminal result"), CapacityAdmission.Kind, EUnrealMCPOperationAdmission::Accepted);
        TestTrue(TEXT("capacity fixture executes"), BoundedLedger.MarkExecuting(Id, Error));
        BoundedLedger.Commit(Id, MakeShared<FJsonObject>());
    }
    TestEqual(TEXT("ledger remains at its published bound"),
        BoundedLedger.CurrentState()->GetIntegerField(TEXT("retained")), UnrealMCP::MaxRetainedOperations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase4PropertyCodecTest, "UnrealMCP.Phase4.PropertyCodec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase4PropertyCodecTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    TSharedPtr<FJsonObject> Changed;
    FUnrealMCPError Error;
    UBlueprint* ReferenceBlueprint = CreateBlueprintFixture(
        TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_CodecClass"), AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Blueprint class reference fixture exists"), ReferenceBlueprint)) return false;
    UTextRenderComponent* Text = NewObject<UTextRenderComponent>();
    TestTrue(TEXT("Boolean form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("bVisible"), MakeShared<FJsonValueBoolean>(false), Changed, Error));
    TestFalse(TEXT("Boolean form reads back exactly"), Changed->GetBoolField(TEXT("value")));
    TestTrue(TEXT("finite numeric form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("WorldSize"), MakeShared<FJsonValueNumber>(42.5), Changed, Error));
    TestEqual(TEXT("finite numeric form reads back exactly"), Changed->GetNumberField(TEXT("value")), 42.5);
    TestTrue(TEXT("text form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("Text"), MakeShared<FJsonValueString>(TEXT("Phase Four")), Changed, Error));
    TestEqual(TEXT("text form reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("Phase Four")));
    TestTrue(TEXT("enum form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("HorizontalAlignment"), MakeShared<FJsonValueString>(TEXT("EHTA_Center")), Changed, Error));
    TestTrue(TEXT("enum form is supported"), Changed->GetBoolField(TEXT("supported")));
    TestTrue(TEXT("safe struct form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("TextRenderColor"),
        MakeShared<FJsonValueString>(TEXT("(R=10,G=20,B=30,A=255)")), Changed, Error));
    TestEqual(TEXT("safe struct form reads back canonically"), Changed->GetStringField(TEXT("value")), FString(TEXT("(B=30,G=20,R=10,A=255)")));

    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>();
    TestTrue(TEXT("string form writes"), UnrealMCP::PropertyCodec::Set(Capture, TEXT("ProfilingEventName"),
        MakeShared<FJsonValueString>(TEXT("UnrealMCP")), Changed, Error));
    TestEqual(TEXT("string form reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("UnrealMCP")));
    TestTrue(TEXT("name form writes"), UnrealMCP::PropertyCodec::Set(Capture, TEXT("CollectionTransformWorldToLocal"),
        MakeShared<FJsonValueString>(TEXT("WorldToLocal")), Changed, Error));
    TestEqual(TEXT("name form reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("WorldToLocal")));

    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>();
    const TArray<TSharedPtr<FJsonValue>> Flags = {
        MakeShared<FJsonValueString>(TEXT("HLOD0")), MakeShared<FJsonValueString>(TEXT("HLOD2"))};
    const bool bFlagsWritten = UnrealMCP::PropertyCodec::Set(Mesh, TEXT("ExcludeFromHLODLevels"),
        MakeShared<FJsonValueArray>(Flags), Changed, Error);
    if (!bFlagsWritten) AddInfo(TEXT("flags form diagnostic: ") + Error.Code + TEXT(": ") + Error.Message);
    if (TestTrue(TEXT("flags form writes"), bFlagsWritten))
    {
        TestEqual(TEXT("flags form reads back both names"), Changed->GetArrayField(TEXT("value")).Num(), 2);
    }
    TestTrue(TEXT("visible engine asset reference writes"), UnrealMCP::PropertyCodec::Set(Mesh, TEXT("StaticMesh"),
        MakeShared<FJsonValueString>(TEXT("/Engine/BasicShapes/Cube.Cube")), Changed, Error));
    TestEqual(TEXT("hard asset reference reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("/Engine/BasicShapes/Cube.Cube")));

    UVolumetricCloudComponent* Cloud = NewObject<UVolumetricCloudComponent>();
    TestTrue(TEXT("soft asset reference writes"), UnrealMCP::PropertyCodec::Set(Cloud, TEXT("Material"),
        MakeShared<FJsonValueString>(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")), Changed, Error));
    TestEqual(TEXT("soft asset reference reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));

    UChildActorComponent* Child = NewObject<UChildActorComponent>();
    TestTrue(TEXT("Blueprint class reference writes"), UnrealMCP::PropertyCodec::Set(Child, TEXT("ChildActorClass"),
        MakeShared<FJsonValueString>(ReferenceBlueprint->GeneratedClass->GetPathName()), Changed, Error));
    TestEqual(TEXT("Blueprint class reference reads back exactly"), Changed->GetStringField(TEXT("value")), ReferenceBlueprint->GeneratedClass->GetPathName());

    UInputSettings* Input = NewObject<UInputSettings>();
    const bool bSoftClassWritten = UnrealMCP::PropertyCodec::Set(Input, TEXT("DefaultInputComponentClass"),
        MakeShared<FJsonValueString>(TEXT("/Script/Engine.InputComponent")), Changed, Error);
    if (!bSoftClassWritten) AddInfo(TEXT("soft class diagnostic: ") + Error.Code + TEXT(": ") + Error.Message);
    if (TestTrue(TEXT("soft native class reference writes"), bSoftClassWritten))
    {
        TestEqual(TEXT("soft class reference reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("/Script/Engine.InputComponent")));
    }

    TestFalse(TEXT("incompatible object reference rejects"), UnrealMCP::PropertyCodec::Set(Mesh, TEXT("StaticMesh"),
        MakeShared<FJsonValueString>(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")), Changed, Error));
    TestFalse(TEXT("unsupported container rejects"), UnrealMCP::PropertyCodec::Set(Text, TEXT("ComponentTags"),
        MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()), Changed, Error));
    TestEqual(TEXT("unsupported container error is stable"), Error.Code, FString(TEXT("unsupported_property")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase4ComponentAndDefaultTest, "UnrealMCP.Phase4.ComponentAndDefaultEdits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase4ComponentAndDefaultTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase4");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Phase 4 Blueprint fixture is created"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddRoot = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddRoot->SetStringField(TEXT("component_class"), USceneComponent::StaticClass()->GetPathName());
    AddRoot->SetStringField(TEXT("name"), TEXT("SceneRoot"));
    if (!TestTrue(TEXT("local scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddRoot, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString RootId = ComponentIdByName(Inspector, AssetPath, TEXT("SceneRoot"));
    TestEqual(TEXT("added component gets stable identity"), RootId.Len(), 32);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> SetRoot = ComponentEditArguments(AssetPath, Snapshot, TEXT("set_root"));
    SetRoot->SetStringField(TEXT("component_id"), RootId);
    if (!TestTrue(TEXT("scene root replacement succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), SetRoot, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMesh = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddMesh->SetStringField(TEXT("component_class"), UStaticMeshComponent::StaticClass()->GetPathName());
    AddMesh->SetStringField(TEXT("name"), TEXT("Mesh"));
    AddMesh->SetStringField(TEXT("parent_id"), RootId);
    if (!TestTrue(TEXT("attached scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddMesh, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    FString MeshId = ComponentIdByName(Inspector, AssetPath, TEXT("Mesh"));
    TestEqual(TEXT("attached component gets stable identity"), MeshId.Len(), 32);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMovement = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddMovement->SetStringField(TEXT("component_class"), URotatingMovementComponent::StaticClass()->GetPathName());
    AddMovement->SetStringField(TEXT("name"), TEXT("Movement"));
    if (!TestTrue(TEXT("non-scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddMovement, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Rename = ComponentEditArguments(AssetPath, Snapshot, TEXT("rename"));
    Rename->SetStringField(TEXT("component_id"), MeshId);
    Rename->SetStringField(TEXT("new_name"), TEXT("Visual"));
    if (!TestTrue(TEXT("component rename succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), Rename, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("rename preserves stable identity"), ComponentIdByName(Inspector, AssetPath, TEXT("Visual")), MeshId);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddPivot = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddPivot->SetStringField(TEXT("component_class"), USceneComponent::StaticClass()->GetPathName());
    AddPivot->SetStringField(TEXT("name"), TEXT("Pivot"));
    AddPivot->SetStringField(TEXT("parent_id"), RootId);
    if (!TestTrue(TEXT("second scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddPivot, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString PivotId = ComponentIdByName(Inspector, AssetPath, TEXT("Pivot"));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Reparent = ComponentEditArguments(AssetPath, Snapshot, TEXT("reparent"));
    Reparent->SetStringField(TEXT("component_id"), MeshId);
    Reparent->SetStringField(TEXT("new_parent_id"), PivotId);
    if (!TestTrue(TEXT("scene component reparent succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), Reparent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Cycle = ComponentEditArguments(AssetPath, Snapshot, TEXT("reparent"));
    Cycle->SetStringField(TEXT("component_id"), PivotId);
    Cycle->SetStringField(TEXT("new_parent_id"), MeshId);
    TestFalse(TEXT("attachment cycle rejects"), Mutator.Execute(TEXT("blueprint_component_edit"), Cycle, Result, Error));
    TestEqual(TEXT("cycle rejection is stable"), Error.Code, FString(TEXT("invalid_component")));
    TestEqual(TEXT("cycle rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> Duplicate = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    Duplicate->SetStringField(TEXT("component_class"), USceneComponent::StaticClass()->GetPathName());
    Duplicate->SetStringField(TEXT("name"), TEXT("Pivot"));
    TestFalse(TEXT("duplicate component name rejects"), Mutator.Execute(TEXT("blueprint_component_edit"), Duplicate, Result, Error));
    TestEqual(TEXT("duplicate-name rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> InvalidClass = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    InvalidClass->SetStringField(TEXT("component_class"), AActor::StaticClass()->GetPathName());
    InvalidClass->SetStringField(TEXT("name"), TEXT("Invalid"));
    TestFalse(TEXT("non-component class rejects"), Mutator.Execute(TEXT("blueprint_component_edit"), InvalidClass, Result, Error));
    TestEqual(TEXT("invalid class error is stable"), Error.Code, FString(TEXT("invalid_component")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> SetVisible = ComponentEditArguments(AssetPath, Snapshot, TEXT("set_property"));
    SetVisible->SetStringField(TEXT("component_id"), MeshId);
    SetVisible->SetStringField(TEXT("property_name"), TEXT("bVisible"));
    SetVisible->SetBoolField(TEXT("value"), false);
    if (!TestTrue(TEXT("component Boolean default edit succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), SetVisible, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestFalse(TEXT("component edit returns exact read-back"), Result->GetObjectField(TEXT("changed"))->GetBoolField(TEXT("value")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString BeforeClassDefault = Snapshot;
    const TSharedRef<FJsonObject> SetActorDefault = MakeShared<FJsonObject>();
    SetActorDefault->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    SetActorDefault->SetStringField(TEXT("asset_path"), AssetPath);
    SetActorDefault->SetStringField(TEXT("expected_snapshot"), Snapshot);
    SetActorDefault->SetStringField(TEXT("property_name"), TEXT("InitialLifeSpan"));
    SetActorDefault->SetNumberField(TEXT("value"), 12.5);
    if (!TestTrue(TEXT("Actor class default edit succeeds"), Mutator.Execute(TEXT("blueprint_default_edit"), SetActorDefault, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("class default round trips exactly"), Result->GetObjectField(TEXT("changed"))->GetNumberField(TEXT("value")), 12.5);
    const FString AfterClassDefault = Result->GetStringField(TEXT("snapshot_id"));
    TestTrue(TEXT("class-default transaction undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores prior class-default snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeClassDefault);
    TestTrue(TEXT("class-default transaction redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores edited class-default snapshot"), InspectSnapshot(Inspector, AssetPath), AfterClassDefault);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const TSharedRef<FJsonObject> TargetedInspect = InspectArguments(AssetPath);
    TargetedInspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components")), MakeShared<FJsonValueString>(TEXT("class_defaults"))});
    TargetedInspect->SetStringField(TEXT("component_id"), MeshId);
    TargetedInspect->SetArrayField(TEXT("property_names"), {MakeShared<FJsonValueString>(TEXT("bVisible")), MakeShared<FJsonValueString>(TEXT("InitialLifeSpan"))});
    if (!TestTrue(TEXT("targeted component and class-default inspection succeeds"), Inspector.Execute(TargetedInspect, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("targeted component record is present"), ResultHasSection(Result, TEXT("component")));
    TestTrue(TEXT("targeted class-default record is present"), ResultHasSection(Result, TEXT("class_default")));

    TSharedRef<FJsonObject> Stale = ComponentEditArguments(AssetPath, FString::ChrN(40, TEXT('0')), TEXT("remove"));
    Stale->SetStringField(TEXT("component_id"), MeshId);
    TestFalse(TEXT("stale snapshot rejects before mutation"), Mutator.Execute(TEXT("blueprint_component_edit"), Stale, Result, Error));
    TestEqual(TEXT("stale error is stable"), Error.Code, FString(TEXT("stale_precondition")));
    TestEqual(TEXT("rejection preserves structure"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveVisual = ComponentEditArguments(AssetPath, Snapshot, TEXT("remove"));
    RemoveVisual->SetStringField(TEXT("component_id"), MeshId);
    if (!TestTrue(TEXT("leaf component removal succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), RemoveVisual, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("removed component identity is unavailable"), ComponentIdByName(Inspector, AssetPath, TEXT("Visual")).IsEmpty());

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    TestEqual(TEXT("edited Blueprint compiles without errors"), Log.NumErrors, 0);
    TestTrue(TEXT("edited Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}


#endif
