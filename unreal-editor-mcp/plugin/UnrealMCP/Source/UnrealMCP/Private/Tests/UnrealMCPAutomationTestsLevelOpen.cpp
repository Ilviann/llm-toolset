#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPVersion.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPLevelOpenTest,
    "UnrealMCP.LevelOpen.DiscoverySnapshotsAndSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPLevelOpenTest::RunTest(const FString& Parameters)
{
    const FString PackageRoot = TEXT("/Game/UnrealMCPLevelOpen");
    const FString PackageA = PackageRoot + TEXT("/LevelA");
    const FString PackageB = PackageRoot + TEXT("/LevelB");
    const FString PathA = PackageA + TEXT(".LevelA");
    const FString PathB = PackageB + TEXT(".LevelB");
    const FString FilenameA = FPackageName::LongPackageNameToFilename(
        PackageA, FPackageName::GetMapPackageExtension());
    const FString FilenameB = FPackageName::LongPackageNameToFilename(
        PackageB, FPackageName::GetMapPackageExtension());

    const auto CreateMap = [this](const FString& Filename)
    {
        UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
        TestNotNull(TEXT("new editor map exists"), World);
        return World != nullptr && FEditorFileUtils::SaveMap(World, Filename);
    };
    if (!CreateMap(FilenameA) || !CreateMap(FilenameB)
        || !FEditorFileUtils::LoadMap(FilenameA, false, false))
    {
        AddError(TEXT("Could not prepare level-open map fixtures"));
        return false;
    }

    UPackage* NonMapPackage = CreatePackage(*(PackageRoot + TEXT("/NotAMap")));
    UCurveFloat* NonMap = NewObject<UCurveFloat>(
        NonMapPackage, TEXT("NotAMap"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(NonMap);

    double CurrentTime = 100.0;
    FUnrealMCPLevelService Service(
        TEXT("1111111111111111111111111111111111111111"),
        [&CurrentTime] { return CurrentTime; });
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;

    const TSharedRef<FUnrealMCPRecord> Discover = MakeShared<FUnrealMCPRecord>();
    Discover->SetStringField(TEXT("mode"), TEXT("discover"));
    Discover->SetStringField(TEXT("package_path"), PackageRoot);
    Discover->SetNumberField(TEXT("page_size"), 1);
    TestTrue(TEXT("bounded map discovery succeeds"), Service.Inspect(Discover, Result, Error));
    TestEqual(TEXT("two fixture maps are discoverable"), Result->GetIntegerField(TEXT("record_count")), 2);
    TestTrue(TEXT("discovery provides a continuation"), Result->GetBoolField(TEXT("has_more")));
    const FString DiscoverySnapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString Cursor = Result->GetStringField(TEXT("next_cursor"));
    TestEqual(TEXT("discovery snapshot is exact"), DiscoverySnapshot.Len(), 40);

    const TSharedRef<FUnrealMCPRecord> Continue = MakeShared<FUnrealMCPRecord>();
    Continue->SetStringField(TEXT("cursor"), Cursor);
    Continue->SetNumberField(TEXT("page_size"), 1);
    TestTrue(TEXT("discovery cursor continues once"), Service.Inspect(Continue, Result, Error));
    TestEqual(TEXT("continuation keeps its query snapshot"), Result->GetStringField(TEXT("snapshot_id")), DiscoverySnapshot);
    TestFalse(TEXT("continuation finishes the page set"), Result->GetBoolField(TEXT("has_more")));
    TestFalse(TEXT("consumed cursor cannot replay"), Service.Inspect(Continue, Result, Error));
    TestEqual(TEXT("consumed cursor reports expiry"), Error.Code, FString(TEXT("cursor_expired")));

    TestTrue(TEXT("a fresh discovery cursor is issued"), Service.Inspect(Discover, Result, Error));
    const FString ExpiringCursor = Result->GetStringField(TEXT("next_cursor"));
    CurrentTime += UnrealMCP::CursorLifetimeSeconds + 1.0;
    const TSharedRef<FUnrealMCPRecord> Expired = MakeShared<FUnrealMCPRecord>();
    Expired->SetStringField(TEXT("cursor"), ExpiringCursor);
    TestFalse(TEXT("expired cursor cannot continue"), Service.Inspect(Expired, Result, Error));
    TestEqual(TEXT("expired cursor reports expiry"), Error.Code, FString(TEXT("cursor_expired")));

    const TSharedRef<FUnrealMCPRecord> Current = MakeShared<FUnrealMCPRecord>();
    Current->SetStringField(TEXT("mode"), TEXT("current"));
    TestTrue(TEXT("current map inspection succeeds"), Service.Inspect(Current, Result, Error));
    const TSharedPtr<FUnrealMCPRecord> CurrentRecord = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("current map is LevelA"), CurrentRecord->GetStringField(TEXT("map_path")), PathA);
    TestEqual(TEXT("map identity is exact"), CurrentRecord->GetStringField(TEXT("map_id")).Len(), 40);
    TestEqual(TEXT("map revision is exact"), CurrentRecord->GetStringField(TEXT("map_revision")).Len(), 40);
    TestFalse(TEXT("saved current map is clean"), CurrentRecord->GetBoolField(TEXT("dirty")));

    const auto OpenArguments = [](const FString& Path)
    {
        const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
        Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("map_path"), Path);
        return Arguments;
    };
    TestTrue(TEXT("opening the current map is non-destructive"), Service.Open(OpenArguments(PathA), Result, Error));
    TestTrue(TEXT("current map is reported without reload"), Result->GetBoolField(TEXT("already_current")));

    UWorld* WorldA = GEditor->GetEditorWorldContext().World();
    TestNotNull(TEXT("LevelA editor world exists"), WorldA);
    WorldA->GetPackage()->SetDirtyFlag(true);
    TestFalse(TEXT("dirty current map refuses switching"), Service.Open(OpenArguments(PathB), Result, Error));
    TestEqual(TEXT("dirty refusal is explicit"), Error.Code, FString(TEXT("busy")));
    TestEqual(
        TEXT("dirty refusal preserves the current map"),
        Service.Inspect(Current, Result, Error)
            ? Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetStringField(TEXT("map_path"))
            : FString(),
        PathA);
    WorldA->GetPackage()->SetDirtyFlag(false);

    TestTrue(TEXT("clean exact map switch succeeds"), Service.Open(OpenArguments(PathB), Result, Error));
    TestTrue(TEXT("map switch is reported"), Result->GetBoolField(TEXT("opened")));
    TestEqual(TEXT("opened map is verified"), Result->GetStringField(TEXT("map_path")), PathB);
    TestEqual(
        TEXT("open returns current-map read-back"),
        Result->GetObjectField(TEXT("current_map"))->GetStringField(TEXT("map_path")),
        PathB);

    const TSharedRef<FUnrealMCPRecord> Invalid = OpenArguments(TEXT("/Game/Missing.Missing"));
    TestFalse(TEXT("missing map rejects"), Service.Open(Invalid, Result, Error));
    TestEqual(TEXT("missing map is explicit"), Error.Code, FString(TEXT("not_found")));
    TestFalse(
        TEXT("mounted non-World asset rejects"),
        Service.Open(OpenArguments(PackageRoot + TEXT("/NotAMap.NotAMap")), Result, Error));
    TestEqual(TEXT("non-World rejection is explicit"), Error.Code, FString(TEXT("wrong_type")));

    FAutomationEditorCommonUtils::CreateNewMap();
    FAssetRegistryModule::AssetDeleted(NonMap);
    NonMap->ClearFlags(RF_Public | RF_Standalone);
    NonMapPackage->SetDirtyFlag(false);
    IFileManager::Get().Delete(*FilenameA);
    IFileManager::Get().Delete(*FilenameB);
    return true;
}

#endif
