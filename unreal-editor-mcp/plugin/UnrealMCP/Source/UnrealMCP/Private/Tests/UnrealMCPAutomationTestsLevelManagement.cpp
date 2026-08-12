#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "AssetCompilingManager.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealMCPAssetDeletionService.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPLevelManagementService.h"
#include "UnrealMCPLevelService.h"

namespace
{
bool CurrentSnapshot(
    FUnrealMCPLevelService& Levels,
    FString& OutPath,
    FString& OutSnapshot,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("mode"), TEXT("current"));
    TSharedPtr<FUnrealMCPRecord> Result;
    if (!Levels.Inspect(Arguments, Result, OutError)) return false;
    OutSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    OutPath = Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetStringField(TEXT("map_path"));
    return true;
}

TSharedRef<FUnrealMCPRecord> Setting(const FString& Name, TSharedPtr<FUnrealMCPValue> Value)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("property_name"), Name);
    Result->SetField(TEXT("value"), MoveTemp(Value));
    return Result;
}

TSharedRef<FUnrealMCPRecord> BlankCreate(
    const FString& Path,
    const FString& Snapshot,
    bool bOpen,
    bool bWorldPartition = false,
    bool bWorldPartitionStreaming = false,
    bool bExternalActors = false)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("operation"), TEXT("create"));
    Arguments->SetStringField(TEXT("destination_path"), Path);
    Arguments->SetStringField(TEXT("expected_current_snapshot"), Snapshot);
    Arguments->SetBoolField(TEXT("open_after_create"), bOpen);
    const TSharedRef<FUnrealMCPRecord> Source = MakeShared<FUnrealMCPRecord>();
    Source->SetStringField(TEXT("kind"), TEXT("blank"));
    Arguments->SetObjectField(TEXT("source"), Source);
    const TSharedRef<FUnrealMCPRecord> Options = MakeShared<FUnrealMCPRecord>();
    Options->SetBoolField(TEXT("world_partition"), bWorldPartition);
    Options->SetBoolField(TEXT("world_partition_streaming"), bWorldPartitionStreaming);
    Options->SetBoolField(TEXT("external_actors"), bExternalActors);
    Arguments->SetObjectField(TEXT("creation_options"), Options);
    Arguments->SetArrayField(TEXT("settings"), {
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("KillZ"), MakeShared<FUnrealMCPValueNumber>(-12345.0)))});
    return Arguments;
}

TSharedRef<FUnrealMCPRecord> LevelManagementDeleteArguments(const FString& Path, const FString& Snapshot)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), Path);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    return Arguments;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPLevelManagementTest,
    "UnrealMCP.LevelManagement.CreateConfigurePersistAndDelete",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPLevelManagementTest::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/Game/UnrealMCPLevelManagement/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString BasePackage = Root + TEXT("/Base");
    const FString BasePath = BasePackage + TEXT(".Base");
    const FString CreatedPackage = Root + TEXT("/Created");
    const FString CreatedPath = CreatedPackage + TEXT(".Created");
    const FString PartitionedPackage = Root + TEXT("/Partitioned");
    const FString PartitionedPath = PartitionedPackage + TEXT(".Partitioned");
    const FString BaseFilename = FPackageName::LongPackageNameToFilename(
        BasePackage, FPackageName::GetMapPackageExtension());
    UWorld* Base = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("base map exists"), Base)
        || !TestTrue(TEXT("base map saves"), FEditorFileUtils::SaveMap(Base, BaseFilename))) return false;

    FUnrealMCPLevelService Levels(TEXT("1111111111111111111111111111111111111111"));
    FUnrealMCPLevelManagementService Management(
        TEXT("1111111111111111111111111111111111111111"), Levels);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    FString CurrentPath;
    FString Snapshot;
    if (!TestTrue(TEXT("current snapshot succeeds"), CurrentSnapshot(Levels, CurrentPath, Snapshot, Error))) return false;
    TestEqual(TEXT("base is current"), CurrentPath, BasePath);

    const TSharedRef<FUnrealMCPRecord> InvalidCreate = BlankCreate(CreatedPath, Snapshot, false);
    InvalidCreate->SetArrayField(TEXT("settings"), {
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("WorldToMeters"), MakeShared<FUnrealMCPValueNumber>(333.0))),
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("KillZ"), MakeShared<FUnrealMCPValueString>(TEXT("invalid"))))});
    TestFalse(TEXT("invalid initial setup rejects"), Management.Manage(InvalidCreate, Result, Error));
    TestEqual(TEXT("invalid initial setup error is stable"), Error.Code, FString(TEXT("invalid_argument")));

    if (!TestTrue(TEXT("blank exact map creates"),
        Management.Manage(BlankCreate(CreatedPath, Snapshot, false), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(TEXT("creation saves"), Result->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("creation reload verifies"), Result->GetBoolField(TEXT("reload_verified")));
    TestTrue(TEXT("creation preserves current map"), Result->GetBoolField(TEXT("current_map_preserved")));
    TestNull(TEXT("inactive verification package is released"), FindPackage(nullptr, *CreatedPackage));
    TestFalse(TEXT("non-partition facts are exact"), Result->GetObjectField(TEXT("effective_creation"))->GetBoolField(TEXT("world_partition")));
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushAsyncLoading();
    TestFalse(TEXT("duplicate destination rejects"),
        Management.Manage(BlankCreate(CreatedPath, Snapshot, false), Result, Error));
    TestEqual(TEXT("duplicate rejection is stable"), Error.Code, FString(TEXT("already_exists")));

    const TSharedRef<FUnrealMCPRecord> Open = MakeShared<FUnrealMCPRecord>();
    Open->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Open->SetStringField(TEXT("map_path"), CreatedPath);
    if (!TestTrue(TEXT("created map opens explicitly"), Levels.Open(Open, Result, Error))) return false;
    const FString CreatedSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    UWorld* OpenedWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    const double OriginalWorldToMeters = OpenedWorld != nullptr && OpenedWorld->GetWorldSettings() != nullptr
        ? OpenedWorld->GetWorldSettings()->WorldToMeters : 0.0;
    const TSharedRef<FUnrealMCPRecord> InvalidConfigure = MakeShared<FUnrealMCPRecord>();
    InvalidConfigure->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    InvalidConfigure->SetStringField(TEXT("operation"), TEXT("configure"));
    InvalidConfigure->SetStringField(TEXT("map_path"), CreatedPath);
    InvalidConfigure->SetStringField(TEXT("expected_current_snapshot"), CreatedSnapshot);
    InvalidConfigure->SetBoolField(TEXT("reload_after_save"), false);
    InvalidConfigure->SetArrayField(TEXT("settings"), {
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("WorldToMeters"), MakeShared<FUnrealMCPValueNumber>(333.0))),
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("KillZ"), MakeShared<FUnrealMCPValueString>(TEXT("invalid"))))});
    TestFalse(TEXT("multi-property failure rejects"), Management.Manage(InvalidConfigure, Result, Error));
    OpenedWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    TestEqual(TEXT("failed setup restores prior property"),
        OpenedWorld->GetWorldSettings()->WorldToMeters, static_cast<float>(OriginalWorldToMeters));
    TestFalse(TEXT("failed setup restores clean package"), OpenedWorld->GetPackage()->IsDirty());

    const TSharedRef<FUnrealMCPRecord> Configure = MakeShared<FUnrealMCPRecord>();
    Configure->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Configure->SetStringField(TEXT("operation"), TEXT("configure"));
    Configure->SetStringField(TEXT("map_path"), CreatedPath);
    Configure->SetStringField(TEXT("expected_current_snapshot"), CreatedSnapshot);
    Configure->SetBoolField(TEXT("reload_after_save"), true);
    Configure->SetArrayField(TEXT("settings"), {
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("WorldToMeters"), MakeShared<FUnrealMCPValueNumber>(250.0)))});
    if (!TestTrue(TEXT("current map configures"), Management.Manage(Configure, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(TEXT("configuration reload verifies"), Result->GetBoolField(TEXT("reload_verified")));
    TestEqual(TEXT("configuration reads back one field"), Result->GetArrayField(TEXT("changed_properties")).Num(), 1);

    FString AfterConfigurePath;
    FString AfterConfigureSnapshot;
    if (!CurrentSnapshot(Levels, AfterConfigurePath, AfterConfigureSnapshot, Error)) return false;
    const TSharedRef<FUnrealMCPRecord> Unsupported = MakeShared<FUnrealMCPRecord>(*Configure);
    Unsupported->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Unsupported->SetStringField(TEXT("expected_current_snapshot"), AfterConfigureSnapshot);
    Unsupported->SetArrayField(TEXT("settings"), {
        MakeShared<FUnrealMCPValueObject>(Setting(TEXT("WorldPartition"), MakeShared<FUnrealMCPValueString>(TEXT("unsupported"))))});
    TestFalse(TEXT("post-creation partition conversion rejects"), Management.Manage(Unsupported, Result, Error));
    TestEqual(TEXT("unsupported property is stable"), Error.Code, FString(TEXT("unsupported_property")));

    const TSharedRef<FUnrealMCPRecord> ReopenBase = MakeShared<FUnrealMCPRecord>();
    ReopenBase->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    ReopenBase->SetStringField(TEXT("map_path"), BasePath);
    if (!TestTrue(TEXT("clean base map reopens"), Levels.Open(ReopenBase, Result, Error))) return false;
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushAsyncLoading();
    FUnrealMCPAssetReferenceService References;
    const TSharedRef<FUnrealMCPRecord> InspectReferences = MakeShared<FUnrealMCPRecord>();
    InspectReferences->SetStringField(TEXT("asset_path"), CreatedPath);
    InspectReferences->SetNumberField(TEXT("page_size"), 100);
    if (!TestTrue(TEXT("map reference snapshot succeeds"), References.Inspect(InspectReferences, Result, Error))) return false;
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushAsyncLoading();
    FUnrealMCPAssetDeletionService Deletion(References);
    if (!TestTrue(TEXT("clean inactive map closure deletes"),
        Deletion.Delete(LevelManagementDeleteArguments(CreatedPath, Result->GetStringField(TEXT("snapshot_id"))), Result, Error)))
    {
        FString Flags;
        if (Error.Details.IsValid())
        {
            for (const TCHAR* Name : {TEXT("is_playing"), TEXT("is_simulating"), TEXT("is_saving"),
                TEXT("is_garbage_collecting"), TEXT("transaction_active"), TEXT("undo_redo_active"),
                TEXT("compiling_assets"), TEXT("async_loading")})
            {
                bool bValue = false;
                Error.Details->TryGetBoolField(Name, bValue);
                Flags += FString::Printf(TEXT(" %s=%s"), Name, bValue ? TEXT("true") : TEXT("false"));
            }
        }
        AddError(Error.Code + TEXT(": ") + Error.Message + Flags);
        return false;
    }
    TestTrue(TEXT("map deletion is verified"), Result->GetBoolField(TEXT("deleted")));
    TestTrue(TEXT("map package closure is complete"), Result->GetBoolField(TEXT("package_closure_complete")));

    FString BaseAfterDelete;
    FString BaseSnapshot;
    if (!TestTrue(TEXT("base remains current after deletion"),
        CurrentSnapshot(Levels, BaseAfterDelete, BaseSnapshot, Error))) return false;
    if (!TestTrue(TEXT("partitioned blank map creates"), Management.Manage(
        BlankCreate(PartitionedPath, BaseSnapshot, false, true, true, true), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const TSharedPtr<FUnrealMCPRecord> PartitionFacts = Result->GetObjectField(TEXT("effective_creation"));
    TestTrue(TEXT("partition fact reads back"), PartitionFacts->GetBoolField(TEXT("world_partition")));
    TestTrue(TEXT("partition streaming fact reads back"), PartitionFacts->GetBoolField(TEXT("world_partition_streaming")));
    TestTrue(TEXT("external actors fact reads back"), PartitionFacts->GetBoolField(TEXT("external_actors")));
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushAsyncLoading();
    InspectReferences->SetStringField(TEXT("asset_path"), PartitionedPath);
    if (!TestTrue(TEXT("partitioned map reference snapshot succeeds"),
        References.Inspect(InspectReferences, Result, Error))) return false;
    FAssetCompilingManager::Get().FinishAllCompilation();
    FlushAsyncLoading();
    if (!TestTrue(TEXT("partitioned inactive map closure deletes"), Deletion.Delete(
        LevelManagementDeleteArguments(PartitionedPath, Result->GetStringField(TEXT("snapshot_id"))), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(TEXT("partitioned map deletion is verified"), Result->GetBoolField(TEXT("deleted")));
    TestTrue(TEXT("partitioned package closure is complete"), Result->GetBoolField(TEXT("package_closure_complete")));

    FAutomationEditorCommonUtils::CreateNewMap();
    IFileManager::Get().Delete(*BaseFilename);
    return true;
}

#endif
