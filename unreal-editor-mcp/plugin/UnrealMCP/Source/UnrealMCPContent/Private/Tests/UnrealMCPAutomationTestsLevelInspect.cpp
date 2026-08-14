#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextRenderActor.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealMCPLevelService.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPLevelInspectTest,
    "UnrealMCP.LevelInspect.ActorsComponentsPropertiesAndSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPLevelInspectTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UnrealMCPLevelInspect/InspectMap");
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetMapPackageExtension());
    GEditor->CreateNewMapForEditing(false, true);
    UWorld* World = GEditor->GetEditorWorldContext().World();
    TestNotNull(TEXT("partitioned editor world exists"), World);
    if (World == nullptr)
    {
        return false;
    }

    ATextRenderActor* Actor = World->SpawnActor<ATextRenderActor>(
        ATextRenderActor::StaticClass(),
        FTransform(FRotator(0.0, 45.0, 0.0), FVector(100.0, 200.0, 300.0)));
    TestNotNull(TEXT("inspection actor exists"), Actor);
    if (Actor == nullptr)
    {
        return false;
    }
    Actor->SetActorLabel(TEXT("InspectableActor"));
    Actor->SetFolderPath(TEXT("MCP/Inspection"));
    Actor->Tags.Add(TEXT("Inspectable"));
    Actor->GetTextRender()->SetText(FText::FromString(TEXT("Level inspection")));
    USceneComponent* InstanceComponent = NewObject<USceneComponent>(
        Actor, TEXT("InspectionInstance"), RF_Transactional);
    Actor->AddInstanceComponent(InstanceComponent);
    InstanceComponent->RegisterComponent();
    InstanceComponent->AttachToComponent(
        Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    const FGuid ActorGuid = Actor->GetActorGuid();
    const FString TextComponentName = Actor->GetTextRender()->GetName();
    const FString InstanceComponentName = InstanceComponent->GetName();
    if (!FEditorFileUtils::SaveMap(World, Filename))
    {
        AddError(TEXT("Could not save the level-inspect fixture"));
        return false;
    }

    FUnrealMCPLevelService Service(TEXT("2222222222222222222222222222222222222222"));
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Current = MakeShared<FUnrealMCPRecord>();
    Current->SetStringField(TEXT("mode"), TEXT("current"));
    TestTrue(TEXT("current map snapshot succeeds"), Service.Inspect(Current, Result, Error));
    const TSharedPtr<FUnrealMCPRecord> CurrentRecord = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    FString MapId = CurrentRecord->GetStringField(TEXT("map_id"));
    FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    const TSharedRef<FUnrealMCPRecord> Filters = MakeShared<FUnrealMCPRecord>();
    Filters->SetStringField(TEXT("label"), TEXT("InspectableActor"));
    Filters->SetStringField(TEXT("tag"), TEXT("Inspectable"));
    Filters->SetStringField(TEXT("folder"), TEXT("MCP/Inspection"));
    Filters->SetBoolField(TEXT("loaded"), true);
    const TSharedRef<FUnrealMCPRecord> Region = MakeShared<FUnrealMCPRecord>();
    const auto Vector = [](double X, double Y, double Z)
    {
        const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        Value->SetNumberField(TEXT("x"), X);
        Value->SetNumberField(TEXT("y"), Y);
        Value->SetNumberField(TEXT("z"), Z);
        return Value;
    };
    Region->SetObjectField(TEXT("min"), Vector(0.0, 100.0, 200.0));
    Region->SetObjectField(TEXT("max"), Vector(200.0, 300.0, 400.0));
    Filters->SetObjectField(TEXT("region"), Region);
    const TSharedRef<FUnrealMCPRecord> Actors = MakeShared<FUnrealMCPRecord>();
    Actors->SetStringField(TEXT("mode"), TEXT("actors"));
    Actors->SetStringField(TEXT("map_id"), MapId);
    Actors->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Actors->SetObjectField(TEXT("filters"), Filters);

    TestTrue(TEXT("exact actor filters succeed"), Service.Inspect(Actors, Result, Error));
    TestEqual(TEXT("filters return one actor"), Result->GetIntegerField(TEXT("record_count")), 1);
    const TSharedPtr<FUnrealMCPRecord> ActorRecord = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    const FString ActorId = ActorRecord->GetStringField(TEXT("actor_id"));
    TestEqual(TEXT("actor identity is map-qualified"), ActorId.Left(40), MapId);
    TestTrue(TEXT("World Partition descriptor is reported"), ActorRecord->GetBoolField(TEXT("descriptor_available")));
    TestTrue(TEXT("freshly authored actor is initially loaded"), ActorRecord->GetBoolField(TEXT("loaded")));
    TestEqual(TEXT("external actor package is exact"), ActorRecord->GetStringField(TEXT("external_package")), Actor->GetPackage()->GetName());
    TestEqual(TEXT("actor tag is reported"), ActorRecord->GetArrayField(TEXT("tags"))[0]->AsString(), FString(TEXT("Inspectable")));

    FAutomationEditorCommonUtils::CreateNewMap();
    TestTrue(TEXT("saved partitioned fixture reloads"),
        FEditorFileUtils::LoadMap(Filename, false, true));
    World = GEditor->GetEditorWorldContext().World();
    TestNotNull(TEXT("reloaded fixture world exists"), World);
    TestTrue(TEXT("reloaded current map snapshot succeeds"), Service.Inspect(Current, Result, Error));
    const TSharedPtr<FUnrealMCPRecord> ReloadedCurrent =
        Result->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("map identity survives reload"),
        ReloadedCurrent->GetStringField(TEXT("map_id")), MapId);
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    Actors->SetStringField(TEXT("expected_snapshot"), Snapshot);

    UWorldPartition* WorldPartition = World->GetWorldPartition();
    TestNotNull(TEXT("fixture World Partition exists"), WorldPartition);
    FWorldPartitionActorDescInstance* FixtureDescriptor = WorldPartition != nullptr
        ? WorldPartition->GetActorDescInstance(ActorGuid) : nullptr;
    TestNotNull(TEXT("fixture descriptor survives unloading"), FixtureDescriptor);
    const auto IsFixtureActorRegistered = [&World, &FixtureDescriptor]()
    {
        AActor* LoadedActor = FixtureDescriptor != nullptr
            ? FixtureDescriptor->GetActor(false, false) : nullptr;
        return LoadedActor != nullptr
            && World != nullptr
            && World->PersistentLevel != nullptr
            && World->PersistentLevel->Actors.Contains(LoadedActor);
    };
    TestFalse(TEXT("fixture actor is explicitly unloaded"),
        IsFixtureActorRegistered());
    const int32 SelectionBefore = GEditor->GetSelectedActors() != nullptr
        ? GEditor->GetSelectedActors()->Num() : 0;
    const bool bDirtyBefore = World->GetPackage()->IsDirty();
    const TArray<FBox> LoadedRegionsBefore = WorldPartition != nullptr
        ? WorldPartition->GetUserLoadedEditorRegions() : TArray<FBox>();
    Filters->SetBoolField(TEXT("loaded"), false);
    TestTrue(TEXT("unloaded exact actor filter succeeds"), Service.Inspect(Actors, Result, Error));
    TestEqual(TEXT("unloaded filter returns one descriptor"),
        Result->GetIntegerField(TEXT("record_count")), 1);
    TestFalse(TEXT("broad descriptor query does not load its match"),
        IsFixtureActorRegistered());

    const TSharedRef<FUnrealMCPRecord> ActorInspect = MakeShared<FUnrealMCPRecord>();
    ActorInspect->SetStringField(TEXT("mode"), TEXT("actor"));
    ActorInspect->SetStringField(TEXT("map_id"), MapId);
    ActorInspect->SetStringField(TEXT("expected_snapshot"), Snapshot);
    ActorInspect->SetStringField(TEXT("actor_id"), ActorId);
    ActorInspect->SetArrayField(TEXT("property_names"), {MakeShared<FUnrealMCPValueString>(TEXT("Tags"))});
    TestTrue(TEXT("exact live actor inspection succeeds"), Service.Inspect(ActorInspect, Result, Error));
    FString TextComponentId;
    FString InstanceComponentId;
    int32 ComponentCount = 0;
    int32 PropertyCount = 0;
    for (const TSharedPtr<FUnrealMCPValue>& Value : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Value->AsObject();
        const FString Section = Record->GetStringField(TEXT("section"));
        if (Section == TEXT("component"))
        {
            ++ComponentCount;
            if (Record->GetStringField(TEXT("name")) == TextComponentName)
                TextComponentId = Record->GetStringField(TEXT("component_id"));
            if (Record->GetStringField(TEXT("name")) == InstanceComponentName)
            {
                InstanceComponentId = Record->GetStringField(TEXT("component_id"));
                TestEqual(TEXT("instance component origin is explicit"),
                    Record->GetStringField(TEXT("creation_method")), FString(TEXT("instance")));
            }
        }
        else if (Section == TEXT("property"))
        {
            ++PropertyCount;
            TestEqual(TEXT("actor property owner is explicit"),
                Record->GetStringField(TEXT("owner_kind")), FString(TEXT("actor")));
        }
    }
    TestTrue(TEXT("actor components are bounded and returned"), ComponentCount >= 2);
    TestEqual(TEXT("requested actor property is returned once"), PropertyCount, 1);
    TestFalse(TEXT("text component identity is available"), TextComponentId.IsEmpty());
    TestFalse(TEXT("instance component identity is available"), InstanceComponentId.IsEmpty());
    TestFalse(TEXT("exact actor load is released after inspection"),
        IsFixtureActorRegistered());

    const TSharedRef<FUnrealMCPRecord> ComponentInspect = MakeShared<FUnrealMCPRecord>();
    ComponentInspect->SetStringField(TEXT("mode"), TEXT("component"));
    ComponentInspect->SetStringField(TEXT("map_id"), MapId);
    ComponentInspect->SetStringField(TEXT("expected_snapshot"), Snapshot);
    ComponentInspect->SetStringField(TEXT("actor_id"), ActorId);
    ComponentInspect->SetStringField(TEXT("component_id"), TextComponentId);
    ComponentInspect->SetArrayField(TEXT("property_names"), {MakeShared<FUnrealMCPValueString>(TEXT("Text"))});
    TestTrue(TEXT("exact component property inspection succeeds"), Service.Inspect(ComponentInspect, Result, Error));
    TestEqual(TEXT("component query returns actor, component, and property"),
        Result->GetIntegerField(TEXT("record_count")), 3);
    TestEqual(TEXT("component property is typed text"),
        Result->GetArrayField(TEXT("records"))[2]->AsObject()
            ->GetObjectField(TEXT("type"))->GetStringField(TEXT("category")),
        FString(TEXT("text")));
    TestFalse(TEXT("exact component load is released after inspection"),
        IsFixtureActorRegistered());

    const TSharedRef<FUnrealMCPRecord> Unsupported = MakeShared<FUnrealMCPRecord>(*ActorInspect);
    Unsupported->SetArrayField(
        TEXT("property_names"), {MakeShared<FUnrealMCPValueString>(TEXT("DefinitelyMissing"))});
    TestFalse(TEXT("unsupported reflected property rejects"), Service.Inspect(Unsupported, Result, Error));
    TestEqual(TEXT("unsupported property error is stable"),
        Error.Code, FString(TEXT("unsupported_property")));

    const TSharedRef<FUnrealMCPRecord> Broad = MakeShared<FUnrealMCPRecord>();
    Broad->SetStringField(TEXT("mode"), TEXT("actors"));
    Broad->SetStringField(TEXT("map_id"), MapId);
    Broad->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Broad->SetNumberField(TEXT("page_size"), 1);
    TestTrue(TEXT("bounded actor page succeeds"), Service.Inspect(Broad, Result, Error));
    TestTrue(TEXT("actor page has a continuation"), Result->GetBoolField(TEXT("has_more")));
    const TSharedRef<FUnrealMCPRecord> Continue = MakeShared<FUnrealMCPRecord>();
    Continue->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    Continue->SetNumberField(TEXT("page_size"), 1);
    TestTrue(TEXT("actor cursor continues"), Service.Inspect(Continue, Result, Error));

    TestEqual(TEXT("selection is preserved"),
        GEditor->GetSelectedActors() != nullptr ? GEditor->GetSelectedActors()->Num() : 0,
        SelectionBefore);
    TestEqual(TEXT("dirty state is preserved"), World->GetPackage()->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("loaded regions are preserved"),
        World->GetWorldPartition() != nullptr
            ? World->GetWorldPartition()->GetUserLoadedEditorRegions().Num() : 0,
        LoadedRegionsBefore.Num());

    World->GetPackage()->SetDirtyFlag(true);
    TestFalse(TEXT("stale actor query rejects"), Service.Inspect(Actors, Result, Error));
    TestEqual(TEXT("stale actor query error is explicit"),
        Error.Code, FString(TEXT("stale_precondition")));
    World->GetPackage()->SetDirtyFlag(false);

    FAutomationEditorCommonUtils::CreateNewMap();
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/UnrealMCPLevelInspect")),
        false,
        true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/__ExternalActors__/UnrealMCPLevelInspect")),
        false,
        true);
    IFileManager::Get().DeleteDirectory(
        *FPackageName::LongPackageNameToFilename(TEXT("/Game/__ExternalObjects__/UnrealMCPLevelInspect")),
        false,
        true);
    return true;
}

#endif
