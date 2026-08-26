#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"
#include "Engine/InheritableComponentHandler.h"


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

    const FString PartialId = TEXT("dddddddddddddddddddddddddddddddd");
    const TSharedRef<FJsonObject> PartialArguments = MakeShared<FJsonObject>();
    PartialArguments->SetStringField(TEXT("operation_id"), PartialId);
    TestEqual(
        TEXT("partial operation is admitted"),
        Ledger.Admit(TEXT("asset_delete"), PartialArguments).Kind,
        EUnrealMCPOperationAdmission::Accepted);
    TestTrue(TEXT("partial operation starts executing"), Ledger.MarkExecuting(PartialId, Error));
    const TSharedRef<FJsonObject> Partial = MakeShared<FJsonObject>();
    Partial->SetStringField(TEXT("operation_state"), TEXT("partial"));
    Ledger.Complete(PartialId, TEXT("partial"), Partial);
    FUnrealMCPOperationAdmission PartialReplay =
        Ledger.Admit(TEXT("asset_delete"), PartialArguments);
    TestEqual(
        TEXT("partial outcome replays without executing"),
        PartialReplay.Kind,
        EUnrealMCPOperationAdmission::ReplaySuccess);
    const TSharedRef<FJsonObject> PartialStatusArguments = MakeShared<FJsonObject>();
    PartialStatusArguments->SetStringField(TEXT("operation_id"), PartialId);
    PartialStatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    TSharedPtr<FJsonObject> PartialStatus;
    TestTrue(
        TEXT("partial outcome remains reconcilable"),
        Ledger.Status(PartialStatusArguments, PartialStatus, Error));
    TestEqual(
        TEXT("partial outcome retains terminal state"),
        PartialStatus->GetStringField(TEXT("state")),
        FString(TEXT("partial")));
    TestFalse(
        TEXT("partial outcome is never retry-safe"),
        PartialStatus->GetBoolField(TEXT("retry_safe")));

    Arguments->SetStringField(TEXT("asset_path"), TEXT("/Game/BP_Other.BP_Other"));
    Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("conflicting ID reuse rejects"), Admission.Kind, EUnrealMCPOperationAdmission::Conflict);
    TestEqual(TEXT("conflict code is stable"), Admission.Error->Code, FString(TEXT("operation_conflict")));

    const TSharedRef<FJsonObject> CommittedIdentity = MakeShared<FJsonObject>();
    CommittedIdentity->SetStringField(TEXT("operation_id"), OperationId);
    CommittedIdentity->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    TSharedPtr<FJsonObject> Status;
    TestTrue(TEXT("status lookup resolves retained result"), Ledger.Status(CommittedIdentity, Status, Error));
    TestEqual(TEXT("status lookup preserves committed state"), Status->GetStringField(TEXT("state")), FString(TEXT("committed")));
    TestFalse(TEXT("status lookup has no cancellation field"), Status->HasField(TEXT("cancelled")));
    TestTrue(TEXT("terminal cancellation request resolves"), Ledger.Cancel(CommittedIdentity, Status, Error));
    TestEqual(TEXT("terminal cancellation preserves state"), Status->GetStringField(TEXT("state")), FString(TEXT("committed")));
    TestFalse(TEXT("terminal operation is not cancelled"), Status->GetBoolField(TEXT("cancelled")));

    const FString CancelId = TEXT("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const TSharedRef<FJsonObject> Queued = MakeShared<FJsonObject>();
    Queued->SetStringField(TEXT("operation_id"), CancelId);
    Ledger.Admit(TEXT("blueprint_compile"), Queued);
    const TSharedRef<FJsonObject> StatusArguments = MakeShared<FJsonObject>();
    StatusArguments->SetStringField(TEXT("operation_id"), CancelId);
    StatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    TestTrue(TEXT("queued status lookup resolves"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("status lookup cannot alter queued state"), Status->GetStringField(TEXT("state")), FString(TEXT("queued")));
    const TSharedRef<FJsonObject> StatusWithCancel = MakeShared<FJsonObject>(*StatusArguments);
    StatusWithCancel->SetBoolField(TEXT("cancel"), true);
    TestFalse(TEXT("operation_status rejects the removed cancel field"), Ledger.Status(StatusWithCancel, Status, Error));
    TestEqual(TEXT("status cancel rejection is stable"), Error.Code, FString(TEXT("invalid_argument")));
    TestTrue(TEXT("rejected status arguments leave operation queued"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("invalid status lookup cannot cancel"), Status->GetStringField(TEXT("state")), FString(TEXT("queued")));
    TestTrue(TEXT("queued cancellation resolves"), Ledger.Cancel(StatusArguments, Status, Error));
    TestEqual(TEXT("queued operation becomes cancelled"), Status->GetStringField(TEXT("state")), FString(TEXT("cancelled")));
    TestTrue(TEXT("queued cancellation reports cancellation"), Status->GetBoolField(TEXT("cancelled")));
    TestFalse(TEXT("cancelled operation never executes"), Ledger.MarkExecuting(CancelId, Error));

    const FString ExecutingId = TEXT("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    const TSharedRef<FJsonObject> ExecutingRequest = MakeShared<FJsonObject>();
    ExecutingRequest->SetStringField(TEXT("operation_id"), ExecutingId);
    TestEqual(TEXT("executing fixture is admitted"),
        Ledger.Admit(TEXT("blueprint_compile"), ExecutingRequest).Kind,
        EUnrealMCPOperationAdmission::Accepted);
    TestTrue(TEXT("executing fixture starts"), Ledger.MarkExecuting(ExecutingId, Error));
    const TSharedRef<FJsonObject> ExecutingIdentity = MakeShared<FJsonObject>();
    ExecutingIdentity->SetStringField(TEXT("operation_id"), ExecutingId);
    ExecutingIdentity->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    TestTrue(TEXT("executing cancellation request resolves"), Ledger.Cancel(ExecutingIdentity, Status, Error));
    TestEqual(TEXT("executing operation is not interrupted"), Status->GetStringField(TEXT("state")), FString(TEXT("executing")));
    TestFalse(TEXT("executing operation reports no cancellation"), Status->GetBoolField(TEXT("cancelled")));
    TestTrue(TEXT("executing status remains available"), Ledger.Status(ExecutingIdentity, Status, Error));
    TestEqual(TEXT("executing lookup remains non-mutating"), Status->GetStringField(TEXT("state")), FString(TEXT("executing")));

    const FString WrongInstanceId = TEXT("ffffffffffffffffffffffffffffffff");
    const TSharedRef<FJsonObject> WrongInstanceRequest = MakeShared<FJsonObject>();
    WrongInstanceRequest->SetStringField(TEXT("operation_id"), WrongInstanceId);
    TestEqual(TEXT("wrong-instance fixture is admitted"),
        Ledger.Admit(TEXT("blueprint_compile"), WrongInstanceRequest).Kind,
        EUnrealMCPOperationAdmission::Accepted);
    const TSharedRef<FJsonObject> WrongInstanceIdentity = MakeShared<FJsonObject>();
    WrongInstanceIdentity->SetStringField(TEXT("operation_id"), WrongInstanceId);
    WrongInstanceIdentity->SetStringField(TEXT("bridge_instance_id"), TEXT("cccccccccccccccccccccccccccccccc"));
    TestTrue(TEXT("wrong-instance cancellation resolves safely"), Ledger.Cancel(WrongInstanceIdentity, Status, Error));
    TestEqual(TEXT("wrong-instance cancellation reports unknown outcome"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));
    TestFalse(TEXT("wrong-instance cancellation reports no cancellation"), Status->GetBoolField(TEXT("cancelled")));
    WrongInstanceIdentity->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    TestTrue(TEXT("wrong-instance cancellation did not alter queued work"), Ledger.Status(WrongInstanceIdentity, Status, Error));
    TestEqual(TEXT("queued work survives wrong identity"), Status->GetStringField(TEXT("state")), FString(TEXT("queued")));
    const TSharedRef<FJsonObject> CancelWithExtra = MakeShared<FJsonObject>(*WrongInstanceIdentity);
    CancelWithExtra->SetBoolField(TEXT("cancel"), true);
    TestFalse(TEXT("operation_cancel rejects unknown fields"), Ledger.Cancel(CancelWithExtra, Status, Error));
    TestEqual(TEXT("cancel schema rejection is stable"), Error.Code, FString(TEXT("invalid_argument")));
    TestTrue(TEXT("exact cancellation still succeeds"), Ledger.Cancel(WrongInstanceIdentity, Status, Error));

    StatusArguments->SetStringField(TEXT("bridge_instance_id"), TEXT("cccccccccccccccccccccccccccccccc"));
    TestTrue(TEXT("another bridge instance resolves safely"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("bridge restart returns unknown outcome"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));
    CurrentTime += UnrealMCP::OperationLifetimeSeconds + 1.0;
    StatusArguments->SetStringField(TEXT("operation_id"), OperationId);
    StatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    TestTrue(TEXT("expired result resolves safely"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("expired result becomes unknown"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));
    TestTrue(TEXT("expired cancellation resolves safely"), Ledger.Cancel(StatusArguments, Status, Error));
    TestEqual(TEXT("expired cancellation remains unknown"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));
    TestFalse(TEXT("expired operation cannot be cancelled"), Status->GetBoolField(TEXT("cancelled")));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPInheritedComponentInspectionTest,
    "UnrealMCP.ReflectedInspection.InheritedComponentTemplates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPInheritedComponentInspectionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Prefix = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UBlueprint* Parent = CreateBlueprintFixture(Prefix + TEXT("/BP_ComponentParent"), AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("component parent Blueprint exists"), Parent)
        || !TestNotNull(TEXT("parent SCS exists"), Parent->SimpleConstructionScript.Get())) return false;
    USCS_Node* Node = Parent->SimpleConstructionScript->CreateNode(UTextRenderComponent::StaticClass(), TEXT("InheritedText"));
    Parent->SimpleConstructionScript->AddNode(Node);
    UTextRenderComponent* ParentTemplate = Cast<UTextRenderComponent>(Node->ComponentTemplate);
    if (!TestNotNull(TEXT("parent text template exists"), ParentTemplate)) return false;
    ParentTemplate->SetWorldSize(44.0f);
    ParentTemplate->SetText(FText::FromString(TEXT("Inherited Text")));
    FKismetEditorUtilities::CompileBlueprint(Parent);

    UBlueprint* Child = CreateBlueprintFixture(Prefix + TEXT("/BP_ComponentChild"), Parent->GeneratedClass, false);
    if (!TestNotNull(TEXT("component child Blueprint exists"), Child)) return false;
    UInheritableComponentHandler* Handler = Child->GetInheritableComponentHandler(true);
    UTextRenderComponent* ChildOverride = Handler != nullptr
        ? Cast<UTextRenderComponent>(Handler->CreateOverridenComponentTemplate(FComponentKey(Node))) : nullptr;
    if (!TestNotNull(TEXT("child component override template exists"), ChildOverride)) return false;
    ChildOverride->SetWorldSize(88.0f);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Child);
    FKismetEditorUtilities::CompileBlueprint(Child);

    const FString ComponentId = Node->VariableGuid.ToString(EGuidFormats::Digits).ToLower();
    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> ById = InspectArguments(Child->GetPathName());
    ById->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components"))});
    ById->SetStringField(TEXT("component_id"), ComponentId);
    ById->SetArrayField(TEXT("property_names"), {
        MakeShared<FJsonValueString>(TEXT("WorldSize")), MakeShared<FJsonValueString>(TEXT("Text"))});
    if (!TestTrue(TEXT("parent stable component ID targets the child effective template"),
        Inspector.Execute(ById, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const TSharedPtr<FJsonObject> Component = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("targeted inherited identity is preserved"), Component->GetStringField(TEXT("id")), ComponentId);
    TestEqual(TEXT("component remains declared by its parent"),
        Component->GetStringField(TEXT("owner_blueprint")), Parent->GetPathName());
    TestEqual(TEXT("effective template reports a child override"),
        Component->GetStringField(TEXT("template_value_origin")), FString(TEXT("local_override")));
    TestEqual(TEXT("effective template source is the child"),
        Component->GetStringField(TEXT("template_source_blueprint")), Child->GetPathName());
    TMap<FString, TSharedPtr<FJsonObject>> Properties;
    for (const TSharedPtr<FJsonValue>& Value : Component->GetArrayField(TEXT("editable_properties")))
    {
        const TSharedPtr<FJsonObject> Property = Value->AsObject();
        Properties.Add(Property->GetStringField(TEXT("name")), Property);
    }
    TestEqual(TEXT("child override value is effective"), Properties[TEXT("WorldSize")]->GetNumberField(TEXT("value")), 88.0);
    TestEqual(TEXT("child override is distinguished"),
        Properties[TEXT("WorldSize")]->GetStringField(TEXT("value_origin")), FString(TEXT("local_override")));
    TestEqual(TEXT("child override source is explicit"),
        Properties[TEXT("WorldSize")]->GetStringField(TEXT("source_blueprint")), Child->GetPathName());
    TestEqual(TEXT("unchanged inherited value remains effective"),
        Properties[TEXT("Text")]->GetStringField(TEXT("value")), FString(TEXT("Inherited Text")));
    TestEqual(TEXT("unchanged value is distinguished as inherited"),
        Properties[TEXT("Text")]->GetStringField(TEXT("value_origin")), FString(TEXT("inherited")));
    TestEqual(TEXT("inherited value source is explicit"),
        Properties[TEXT("Text")]->GetStringField(TEXT("source_blueprint")), Parent->GetPathName());

    const TSharedRef<FJsonObject> ByName = InspectArguments(Child->GetPathName());
    ByName->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components"))});
    ByName->SetStringField(TEXT("component_name"), TEXT("InheritedText"));
    TestTrue(TEXT("inherited component name targets the child effective template"), Inspector.Execute(ByName, Result, Error));
    TestEqual(TEXT("name lookup returns the inherited stable identity"),
        Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetStringField(TEXT("id")), ComponentId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPBlueprintReflectedValuesTest,
    "UnrealMCP.ReflectedInspection.BlueprintPropertyValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPBlueprintReflectedValuesTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    UClass* ComponentClass = LoadObject<UClass>(
        nullptr, TEXT("/Script/UnrealMCPTestCompanion.UnrealMCPInspectionComponent"));
    if (!TestNotNull(TEXT("extended component fixture class is available"), ComponentClass)) return false;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_ExtendedDefaults");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("extended-default Blueprint exists"), Blueprint)) return false;
    USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, TEXT("InspectionValues"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    UActorComponent* Template = Node->ComponentTemplate;
    if (!TestNotNull(TEXT("extended component template exists"), Template)) return false;

    auto Property = [ComponentClass](const TCHAR* Name) { return ComponentClass->FindPropertyByName(FName(Name)); };
    auto SetTag = [](FStructProperty* TagProperty, void* Container, const FString& Name)
    {
        void* Tag = TagProperty->ContainerPtrToValuePtr<void>(Container);
        FNameProperty* TagName = CastFieldChecked<FNameProperty>(TagProperty->Struct->FindPropertyByName(TEXT("TagName")));
        TagName->SetPropertyValue(TagName->ContainerPtrToValuePtr<void>(Tag), FName(*Name));
    };
    SetTag(CastFieldChecked<FStructProperty>(Property(TEXT("Tag"))), Template, TEXT("Test.Blueprint"));
    FStructProperty* ContainerProperty = CastFieldChecked<FStructProperty>(Property(TEXT("Tags")));
    void* TagContainer = ContainerProperty->ContainerPtrToValuePtr<void>(Template);
    FArrayProperty* TagsProperty = CastFieldChecked<FArrayProperty>(ContainerProperty->Struct->FindPropertyByName(TEXT("GameplayTags")));
    FScriptArrayHelper Tags(TagsProperty, TagsProperty->ContainerPtrToValuePtr<void>(TagContainer));
    FStructProperty* InnerTag = CastFieldChecked<FStructProperty>(TagsProperty->Inner);
    for (const TCHAR* Name : {TEXT("Test.Second"), TEXT("Test.First")})
    {
        const int32 Index = Tags.AddValue();
        FNameProperty* TagName = CastFieldChecked<FNameProperty>(InnerTag->Struct->FindPropertyByName(TEXT("TagName")));
        TagName->SetPropertyValue(TagName->ContainerPtrToValuePtr<void>(Tags.GetRawPtr(Index)), FName(Name));
    }
    *CastFieldChecked<FStructProperty>(Property(TEXT("Id")))->ContainerPtrToValuePtr<FGuid>(Template)
        = FGuid(5, 6, 7, 8);
    CastFieldChecked<FTextProperty>(Property(TEXT("Label")))->SetPropertyValue_InContainer(
        Template, FText::FromString(TEXT("Blueprint Text")));
    FEnumProperty* State = CastFieldChecked<FEnumProperty>(Property(TEXT("State")));
    State->GetUnderlyingProperty()->SetIntPropertyValue(
        State->ContainerPtrToValuePtr<void>(Template), static_cast<int64>(1));
    const FSoftObjectPath CubePath(TEXT("/Engine/BasicShapes/Cube.Cube"));
    CastFieldChecked<FSoftObjectProperty>(Property(TEXT("Asset")))->SetPropertyValue_InContainer(
        Template, FSoftObjectPtr(CubePath));
    CastFieldChecked<FSoftObjectProperty>(Property(TEXT("Class")))->SetPropertyValue_InContainer(
        Template, FSoftObjectPtr(FSoftObjectPath(TEXT("/Script/Engine.Actor"))));
    FArrayProperty* AssetsProperty = CastFieldChecked<FArrayProperty>(Property(TEXT("Assets")));
    FScriptArrayHelper Assets(AssetsProperty, AssetsProperty->ContainerPtrToValuePtr<void>(Template));
    const int32 AssetIndex = Assets.AddValue();
    CastFieldChecked<FSoftObjectProperty>(AssetsProperty->Inner)->SetPropertyValue(
        Assets.GetRawPtr(AssetIndex), FSoftObjectPtr(CubePath));
    FStructProperty* NestedProperty = CastFieldChecked<FStructProperty>(Property(TEXT("Nested")));
    void* Nested = NestedProperty->ContainerPtrToValuePtr<void>(Template);
    CastFieldChecked<FIntProperty>(NestedProperty->Struct->FindPropertyByName(TEXT("Count")))->SetPropertyValue_InContainer(Nested, 23);
    CastFieldChecked<FTextProperty>(NestedProperty->Struct->FindPropertyByName(TEXT("Label")))->SetPropertyValue_InContainer(
        Nested, FText::FromString(TEXT("Nested Blueprint Text")));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(Blueprint->GetPathName());
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components"))});
    Arguments->SetStringField(TEXT("component_name"), TEXT("InspectionValues"));
    TArray<TSharedPtr<FJsonValue>> Requested;
    for (const TCHAR* Name : {TEXT("Tag"), TEXT("Tags"), TEXT("Id"), TEXT("Label"), TEXT("State"),
        TEXT("Asset"), TEXT("Class"), TEXT("Assets"), TEXT("Nested")})
        Requested.Add(MakeShared<FJsonValueString>(Name));
    Arguments->SetArrayField(TEXT("property_names"), Requested);
    if (!TestTrue(TEXT("Blueprint reflected defaults inspect"), Inspector.Execute(Arguments, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TMap<FString, TSharedPtr<FJsonObject>> Values;
    for (const TSharedPtr<FJsonValue>& Value : Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetArrayField(TEXT("editable_properties")))
    {
        const TSharedPtr<FJsonObject> Encoded = Value->AsObject();
        Values.Add(Encoded->GetStringField(TEXT("name")), Encoded);
    }
    TestEqual(TEXT("Blueprint gameplay tag reads"), Values[TEXT("Tag")]->GetStringField(TEXT("value")), FString(TEXT("Test.Blueprint")));
    TestEqual(TEXT("Blueprint gameplay-tag container reads"), Values[TEXT("Tags")]->GetArrayField(TEXT("value")).Num(), 2);
    TestEqual(TEXT("Blueprint GUID reads"), Values[TEXT("Id")]->GetStringField(TEXT("value")), FString(TEXT("00000005000000060000000700000008")));
    TestEqual(TEXT("Blueprint FText reads"), Values[TEXT("Label")]->GetStringField(TEXT("value")), FString(TEXT("Blueprint Text")));
    TestEqual(TEXT("Blueprint enum reads"), Values[TEXT("State")]->GetStringField(TEXT("value")), FString(TEXT("Ready")));
    TestEqual(TEXT("Blueprint soft object reads"), Values[TEXT("Asset")]->GetStringField(TEXT("value")), CubePath.ToString());
    TestEqual(TEXT("Blueprint soft class reads"), Values[TEXT("Class")]->GetStringField(TEXT("value")), FString(TEXT("/Script/Engine.Actor")));
    TestEqual(TEXT("Blueprint soft-reference array reads"), Values[TEXT("Assets")]->GetArrayField(TEXT("value")).Num(), 1);
    TestEqual(TEXT("Blueprint nested reflected struct reads"),
        static_cast<int32>(Values[TEXT("Nested")]->GetObjectField(TEXT("value"))->GetObjectField(TEXT("fields"))->GetNumberField(TEXT("Count"))), 23);
    return true;
}


#endif
