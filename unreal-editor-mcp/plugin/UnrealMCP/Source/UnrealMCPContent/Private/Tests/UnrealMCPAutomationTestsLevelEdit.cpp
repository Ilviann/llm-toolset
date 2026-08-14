#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Components/TextRenderComponent.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/TextRenderActor.h"
#include "FileHelpers.h"
#include "Misc/SecureHash.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealMCPLevelActorEditingService.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPJsonCodec.h"

namespace UnrealMCPLevelEditTestPrivate
{
TSharedRef<FUnrealMCPRecord> Vector(double X, double Y, double Z)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("x"), X);
    Result->SetNumberField(TEXT("y"), Y);
    Result->SetNumberField(TEXT("z"), Z);
    return Result;
}

TSharedRef<FUnrealMCPRecord> Transform(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetObjectField(TEXT("location"), Vector(Location.X, Location.Y, Location.Z));
    const TSharedRef<FUnrealMCPRecord> Rotator = MakeShared<FUnrealMCPRecord>();
    Rotator->SetNumberField(TEXT("pitch"), Rotation.Pitch);
    Rotator->SetNumberField(TEXT("yaw"), Rotation.Yaw);
    Rotator->SetNumberField(TEXT("roll"), Rotation.Roll);
    Result->SetObjectField(TEXT("rotation"), Rotator);
    Result->SetObjectField(TEXT("scale"), Vector(1.0, 1.0, 1.0));
    return Result;
}

FString StableId(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower().Left(32);
}

FString ActorId(const FString& MapId, const AActor* Actor)
{
    return MapId + TEXT(":") + Actor->GetActorGuid().ToString(EGuidFormats::Digits).ToLower();
}

TSharedRef<FUnrealMCPRecord> Operation(const TCHAR* Name, const FString& ActorIdentity = FString())
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("operation"), Name);
    if (!ActorIdentity.IsEmpty()) Result->SetStringField(TEXT("actor_id"), ActorIdentity);
    return Result;
}
}

using namespace UnrealMCPLevelEditTestPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPLevelEditTest,
    "UnrealMCP.LevelEdit.TransactionalActorBatchAndPackageSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPLevelEditTest::RunTest(const FString& Parameters)
{
    const FString Directory = TEXT("/Game/UnrealMCPLevelEdit");
    const FString PackageName = Directory + TEXT("/EditMap");
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetMapPackageExtension());
    FAutomationEditorCommonUtils::CreateNewMap();
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(Directory), false, true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/__ExternalActors__/UnrealMCPLevelEdit")), false, true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/__ExternalObjects__/UnrealMCPLevelEdit")), false, true);
    UBlueprint* Blueprint = UnrealMCP::Tests::CreateBlueprintFixture(
        Directory + TEXT("/BP_LevelSpawn"), AActor::StaticClass(), false);
    TestNotNull(TEXT("Blueprint spawn fixture exists"), Blueprint);
    if (Blueprint == nullptr || !UnrealMCP::Tests::SaveBlueprintFixture(Blueprint)) return false;

    GEditor->CreateNewMapForEditing(false, true);
    UWorld* World = GEditor->GetEditorWorldContext().World();
    ATextRenderActor* Target = World != nullptr ? World->SpawnActor<ATextRenderActor>() : nullptr;
    ATextRenderActor* Parent = World != nullptr ? World->SpawnActor<ATextRenderActor>() : nullptr;
    ATextRenderActor* Disposable = World != nullptr ? World->SpawnActor<ATextRenderActor>() : nullptr;
    TestNotNull(TEXT("target actor exists"), Target);
    TestNotNull(TEXT("parent actor exists"), Parent);
    TestNotNull(TEXT("disposable actor exists"), Disposable);
    if (World == nullptr || Target == nullptr || Parent == nullptr || Disposable == nullptr
        || !FEditorFileUtils::SaveMap(World, Filename)) return false;

    FUnrealMCPLevelService Levels(TEXT("3333333333333333333333333333333333333333"));
    FUnrealMCPLevelActorEditingService Editing(Levels);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Current = MakeShared<FUnrealMCPRecord>();
    Current->SetStringField(TEXT("mode"), TEXT("current"));
    TestTrue(TEXT("current snapshot succeeds"), Levels.Inspect(Current, Result, Error));
    const TSharedPtr<FUnrealMCPRecord> CurrentRecord = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    const FString MapId = CurrentRecord->GetStringField(TEXT("map_id"));
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString TargetId = ActorId(MapId, Target);
    const FString ParentId = ActorId(MapId, Parent);
    const FString DisposableId = ActorId(MapId, Disposable);
    UTextRenderComponent* TextComponent = Target->GetTextRender();
    const FString ComponentId = StableId(
        TargetId + TEXT("|") + TextComponent->GetName() + TEXT("|")
        + TextComponent->GetClass()->GetPathName() + TEXT("|native_default"));

    const TSharedRef<FUnrealMCPRecord> CycleAttachA = Operation(TEXT("attach"), TargetId);
    CycleAttachA->SetStringField(TEXT("parent_actor_id"), ParentId);
    const TSharedRef<FUnrealMCPRecord> CycleAttachB = Operation(TEXT("attach"), ParentId);
    CycleAttachB->SetStringField(TEXT("parent_actor_id"), TargetId);
    const TSharedRef<FUnrealMCPRecord> Cycle = MakeShared<FUnrealMCPRecord>();
    Cycle->SetStringField(TEXT("operation_id"), TEXT("43434343434343434343434343434343"));
    Cycle->SetStringField(TEXT("map_id"), MapId);
    Cycle->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Cycle->SetArrayField(TEXT("operations"), {
        MakeShared<FUnrealMCPValueObject>(CycleAttachA), MakeShared<FUnrealMCPValueObject>(CycleAttachB)});
    TestFalse(TEXT("complete prevalidation rejects an attachment cycle"), Editing.Edit(Cycle, Result, Error));
    TestEqual(TEXT("cycle rejection is explicit"), Error.Code, FString(TEXT("attachment_cycle")));
    TestNull(TEXT("cycle rejection mutates no attachment"), Target->GetAttachParentActor());

    TArray<TSharedPtr<FUnrealMCPValue>> Operations;
    const TSharedRef<FUnrealMCPRecord> TransformOp = Operation(TEXT("transform"), TargetId);
    TransformOp->SetObjectField(TEXT("transform"), Transform(FVector(100.0, 200.0, 300.0), FRotator(0.0, 45.0, 0.0)));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(TransformOp));
    const TSharedRef<FUnrealMCPRecord> LabelOp = Operation(TEXT("label"), TargetId);
    LabelOp->SetStringField(TEXT("label"), TEXT("EditedTarget"));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(LabelOp));
    const TSharedRef<FUnrealMCPRecord> TagsOp = Operation(TEXT("tags"), TargetId);
    TagsOp->SetArrayField(TEXT("tags"), {MakeShared<FUnrealMCPValueString>(TEXT("Authored")), MakeShared<FUnrealMCPValueString>(TEXT("Verified"))});
    Operations.Add(MakeShared<FUnrealMCPValueObject>(TagsOp));
    const TSharedRef<FUnrealMCPRecord> DataLayersOp = Operation(TEXT("data_layers"), TargetId);
    DataLayersOp->SetArrayField(TEXT("data_layers"), {});
    Operations.Add(MakeShared<FUnrealMCPValueObject>(DataLayersOp));
    const TSharedRef<FUnrealMCPRecord> AttachOp = Operation(TEXT("attach"), TargetId);
    AttachOp->SetStringField(TEXT("parent_actor_id"), ParentId);
    Operations.Add(MakeShared<FUnrealMCPValueObject>(AttachOp));
    // Attachment adopts the parent's editor folder, so the explicit folder edit is
    // deliberately ordered afterward and defines the batch's final folder state.
    const TSharedRef<FUnrealMCPRecord> FolderOp = Operation(TEXT("folder"), TargetId);
    FolderOp->SetStringField(TEXT("folder"), TEXT("MCP/Edited"));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(FolderOp));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(Operation(TEXT("detach"), ParentId)));
    const TSharedRef<FUnrealMCPRecord> ActorPropertyOp = Operation(TEXT("actor_property"), TargetId);
    ActorPropertyOp->SetStringField(TEXT("property_name"), TEXT("InitialLifeSpan"));
    ActorPropertyOp->SetNumberField(TEXT("value"), 15.0);
    Operations.Add(MakeShared<FUnrealMCPValueObject>(ActorPropertyOp));
    const TSharedRef<FUnrealMCPRecord> ComponentOp = Operation(TEXT("component_property"), TargetId);
    ComponentOp->SetStringField(TEXT("component_id"), ComponentId);
    ComponentOp->SetStringField(TEXT("property_name"), TEXT("WorldSize"));
    ComponentOp->SetNumberField(TEXT("value"), 128.0);
    Operations.Add(MakeShared<FUnrealMCPValueObject>(ComponentOp));
    const TSharedRef<FUnrealMCPRecord> NativeSpawn = Operation(TEXT("spawn"));
    NativeSpawn->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.StaticMeshActor"));
    NativeSpawn->SetObjectField(TEXT("transform"), Transform(FVector(400.0, 0.0, 0.0)));
    NativeSpawn->SetStringField(TEXT("label"), TEXT("NativeSpawn"));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(NativeSpawn));
    const TSharedRef<FUnrealMCPRecord> BlueprintSpawn = Operation(TEXT("spawn"));
    BlueprintSpawn->SetStringField(TEXT("class_path"), Blueprint->GeneratedClass->GetPathName());
    BlueprintSpawn->SetObjectField(TEXT("transform"), Transform(FVector(500.0, 0.0, 0.0)));
    BlueprintSpawn->SetStringField(TEXT("label"), TEXT("BlueprintSpawn"));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(BlueprintSpawn));
    Operations.Add(MakeShared<FUnrealMCPValueObject>(Operation(TEXT("delete"), DisposableId)));

    const TSharedRef<FUnrealMCPRecord> Edit = MakeShared<FUnrealMCPRecord>();
    Edit->SetStringField(TEXT("operation_id"), TEXT("44444444444444444444444444444444"));
    Edit->SetStringField(TEXT("map_id"), MapId);
    Edit->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Edit->SetArrayField(TEXT("operations"), Operations);
    if (!TestTrue(TEXT("mixed actor batch commits"), Editing.Edit(Edit, Result, Error)))
    {
        AddError(FString::Printf(TEXT("level_actor_edit error: %s: %s"), *Error.Code, *Error.Message));
        if (Error.Details.IsValid())
            AddError(FString::Printf(TEXT("unsafe class: %s found=%d actor=%d abstract=%d deprecated=%d superseded=%d transient=%d editor=%d"),
                *Error.Details->GetStringField(TEXT("class_path")), Error.Details->GetBoolField(TEXT("found")),
                Error.Details->GetBoolField(TEXT("actor_class")), Error.Details->GetBoolField(TEXT("abstract")),
                Error.Details->GetBoolField(TEXT("deprecated")), Error.Details->GetBoolField(TEXT("superseded")),
                Error.Details->GetBoolField(TEXT("transient")), Error.Details->GetBoolField(TEXT("editor_only"))));
        return false;
    }
    TestEqual(TEXT("all operations report read-back"), Result->GetIntegerField(TEXT("operation_count")), Operations.Num());
    TestEqual(TEXT("label changed exactly"), Target->GetActorLabel(), FString(TEXT("EditedTarget")));
    TestEqual(TEXT("component property changed exactly"), TextComponent->WorldSize, 128.0f);
    TestEqual(TEXT("attachment changed exactly"), Target->GetAttachParentActor(), static_cast<AActor*>(Parent));
    TestTrue(TEXT("map snapshot advanced"), Result->GetStringField(TEXT("snapshot_id")) != Snapshot);
    TestTrue(TEXT("affected package set includes root"), Result->GetArrayField(TEXT("affected_packages")).ContainsByPredicate(
        [&PackageName](const TSharedPtr<FUnrealMCPValue>& Value) { return Value->AsString() == PackageName; }));
    FString EditedSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    const TArray<TSharedPtr<FUnrealMCPValue>> Affected = Result->GetArrayField(TEXT("affected_packages"));

    TSharedPtr<FUnrealMCPRecord> Ignored;
    TestFalse(TEXT("stale batch rejects before mutation"), Editing.Edit(Edit, Ignored, Error));
    TestEqual(TEXT("stale error is explicit"), Error.Code, FString(TEXT("stale_precondition")));
    TestTrue(TEXT("the complete actor batch is one Undo unit"), GEditor->UndoTransaction());
    TestNotEqual(TEXT("Undo restores the prior actor label"), Target->GetActorLabel(), FString(TEXT("EditedTarget")));
    TestTrue(TEXT("the complete actor batch is one Redo unit"), GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores the edited actor label"), Target->GetActorLabel(), FString(TEXT("EditedTarget")));
    TestTrue(TEXT("current snapshot refresh succeeds after Redo"), Levels.Inspect(Current, Result, Error));
    EditedSnapshot = Result->GetStringField(TEXT("snapshot_id"));

    const TSharedRef<FUnrealMCPRecord> ExpectedComponent = MakeShared<FUnrealMCPRecord>();
    ExpectedComponent->SetStringField(TEXT("component_id"), ComponentId);
    const TSharedRef<FUnrealMCPRecord> ExpectedWorldSize = MakeShared<FUnrealMCPRecord>();
    ExpectedWorldSize->SetStringField(TEXT("property_name"), TEXT("WorldSize"));
    ExpectedWorldSize->SetNumberField(TEXT("value"), 128.0);
    ExpectedComponent->SetArrayField(TEXT("properties"), {MakeShared<FUnrealMCPValueObject>(ExpectedWorldSize)});
    const TSharedRef<FUnrealMCPRecord> ExpectedActor = MakeShared<FUnrealMCPRecord>();
    ExpectedActor->SetStringField(TEXT("actor_id"), TargetId);
    ExpectedActor->SetStringField(TEXT("label"), TEXT("EditedTarget"));
    ExpectedActor->SetObjectField(TEXT("transform"), Transform(FVector(100.0, 200.0, 300.0), FRotator(0.0, 45.0, 0.0)));
    ExpectedActor->SetArrayField(TEXT("tags"), {MakeShared<FUnrealMCPValueString>(TEXT("Authored")), MakeShared<FUnrealMCPValueString>(TEXT("Verified"))});
    ExpectedActor->SetStringField(TEXT("folder"), TEXT("MCP/Edited"));
    const TSharedRef<FUnrealMCPRecord> ExpectedActorProperty = MakeShared<FUnrealMCPRecord>();
    ExpectedActorProperty->SetStringField(TEXT("property_name"), TEXT("InitialLifeSpan"));
    ExpectedActorProperty->SetNumberField(TEXT("value"), 15.0);
    ExpectedActor->SetArrayField(TEXT("actor_properties"), {MakeShared<FUnrealMCPValueObject>(ExpectedActorProperty)});
    ExpectedActor->SetArrayField(TEXT("components"), {MakeShared<FUnrealMCPValueObject>(ExpectedComponent)});
    const TSharedRef<FUnrealMCPRecord> Verification = MakeShared<FUnrealMCPRecord>();
    Verification->SetStringField(TEXT("mode"), TEXT("reload"));
    Verification->SetArrayField(TEXT("actors"), {MakeShared<FUnrealMCPValueObject>(ExpectedActor)});
    const TSharedRef<FUnrealMCPRecord> Save = MakeShared<FUnrealMCPRecord>();
    Save->SetStringField(TEXT("operation_id"), TEXT("55555555555555555555555555555555"));
    Save->SetStringField(TEXT("map_id"), MapId);
    Save->SetStringField(TEXT("expected_snapshot"), EditedSnapshot);
    Save->SetArrayField(TEXT("affected_packages"), Affected);
    Save->SetObjectField(TEXT("verification"), Verification);
    if (!TestTrue(TEXT("explicit package save returns a ledger outcome"), Editing.Save(Save, Result, Error)))
    {
        AddError(FString::Printf(TEXT("level_save error: %s: %s"), *Error.Code, *Error.Message));
        return false;
    }
    if (!Result->GetBoolField(TEXT("saved")) || !Result->GetBoolField(TEXT("verification_succeeded")))
    {
        FString Encoded;
        UnrealMCP::JsonCodec::Serialize(Result, Encoded);
        AddError(FString::Printf(TEXT("level_save partial result: %s"), *Encoded));
    }
    TestTrue(TEXT("every package saves"), Result->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("reload verification succeeds"), Result->GetBoolField(TEXT("verification_succeeded")));
    TestTrue(TEXT("reload was performed"), Result->GetBoolField(TEXT("reload_performed")));

    const FString MoveDirectory = TEXT("/Game/UnrealMCPLevelMove");
    const FString SubPackage = MoveDirectory + TEXT("/SubLevel");
    const FString RootPackage = MoveDirectory + TEXT("/RootLevel");
    FAutomationEditorCommonUtils::CreateNewMap();
    UWorld* SubWorld = GEditor->GetEditorWorldContext().World();
    const FString SubFilename = FPackageName::LongPackageNameToFilename(
        SubPackage, FPackageName::GetMapPackageExtension());
    TestTrue(TEXT("move target level fixture saves"),
        SubWorld != nullptr && FEditorFileUtils::SaveMap(SubWorld, SubFilename));
    FAutomationEditorCommonUtils::CreateNewMap();
    UWorld* MoveWorld = GEditor->GetEditorWorldContext().World();
    const FString RootFilename = FPackageName::LongPackageNameToFilename(
        RootPackage, FPackageName::GetMapPackageExtension());
    ULevelStreaming* Streaming = MoveWorld != nullptr
        ? UEditorLevelUtils::AddLevelToWorld(
            MoveWorld, *SubPackage, ULevelStreamingAlwaysLoaded::StaticClass())
        : nullptr;
    if (MoveWorld != nullptr) MoveWorld->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    FActorSpawnParameters MoveSpawnParameters;
    MoveSpawnParameters.OverrideLevel = MoveWorld != nullptr ? MoveWorld->PersistentLevel : nullptr;
    ATextRenderActor* MovingActor = MoveWorld != nullptr
        ? MoveWorld->SpawnActor<ATextRenderActor>(ATextRenderActor::StaticClass(), FTransform::Identity, MoveSpawnParameters)
        : nullptr;
    TestNotNull(TEXT("move source actor exists"), MovingActor);
    TestNotNull(TEXT("move target level is loaded"), Streaming != nullptr ? Streaming->GetLoadedLevel() : nullptr);
    TestTrue(TEXT("move root fixture saves"),
        MoveWorld != nullptr && MovingActor != nullptr && Streaming != nullptr
        && Streaming->GetLoadedLevel() != nullptr && FEditorFileUtils::SaveMap(MoveWorld, RootFilename));
    TestTrue(TEXT("move fixture snapshot succeeds"), Levels.Inspect(Current, Result, Error));
    const FString MoveMapId = Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetStringField(TEXT("map_id"));
    const FString MovingActorId = ActorId(MoveMapId, MovingActor);
    const FGuid MovingGuid = MovingActor->GetActorGuid();
    const TSharedRef<FUnrealMCPRecord> MoveOp = Operation(TEXT("move"), MovingActorId);
    MoveOp->SetStringField(TEXT("target_level"), SubPackage);
    const TSharedRef<FUnrealMCPRecord> MoveRequest = MakeShared<FUnrealMCPRecord>();
    MoveRequest->SetStringField(TEXT("operation_id"), TEXT("56565656565656565656565656565656"));
    MoveRequest->SetStringField(TEXT("map_id"), MoveMapId);
    MoveRequest->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    MoveRequest->SetArrayField(TEXT("operations"), {MakeShared<FUnrealMCPValueObject>(MoveOp)});
    if (!TestTrue(TEXT("non-partitioned loaded-level move commits"), Editing.Edit(MoveRequest, Result, Error)))
        AddError(FString::Printf(TEXT("move error: %s: %s"), *Error.Code, *Error.Message));
    AActor* MovedActor = nullptr;
    for (AActor* Candidate : Streaming->GetLoadedLevel()->Actors)
        if (Candidate != nullptr && Candidate->GetActorGuid() == MovingGuid) MovedActor = Candidate;
    TestNotNull(TEXT("move preserves Actor GUID"), MovedActor);
    TestEqual(TEXT("move reaches the exact loaded target level"),
        MovedActor != nullptr ? MovedActor->GetLevel() : nullptr, Streaming->GetLoadedLevel());

    FAutomationEditorCommonUtils::CreateNewMap();
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(Directory), false, true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/__ExternalActors__/UnrealMCPLevelEdit")), false, true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/__ExternalObjects__/UnrealMCPLevelEdit")), false, true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(MoveDirectory), false, true);
    return true;
}

#endif
