#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase3CreationTest, "UnrealMCP.Phase3.CreationContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase3CreationTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString ParentPackage = Base + TEXT("/BP_Created");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    TestTrue(TEXT("native Actor Blueprint creation succeeds"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), ParentPackage), Result, Error));
    const FString ParentObject = ParentPackage + TEXT(".BP_Created");
    TestEqual(TEXT("created asset path is exact"), Result->GetStringField(TEXT("asset_path")), ParentObject);
    TestEqual(TEXT("created parent identity is exact"), Result->GetStringField(TEXT("parent_class")), FString(TEXT("/Script/Engine.Actor")));
    TestTrue(TEXT("creation compiles"), Result->GetBoolField(TEXT("compile_succeeded")));
    TestTrue(TEXT("creation saves"), Result->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("saved creation is clean"), Result->GetBoolField(TEXT("package_dirty")));
    TestEqual(TEXT("creation returns a structural snapshot"), Result->GetStringField(TEXT("snapshot_id")).Len(), 40);

    UBlueprint* ParentBlueprint = LoadObject<UBlueprint>(nullptr, *ParentObject);
    TestNotNull(TEXT("created Blueprint loads"), ParentBlueprint);
    if (ParentBlueprint == nullptr)
    {
        return false;
    }
    const FString ChildPackage = Base + TEXT("/BP_Child");
    TestTrue(TEXT("Blueprint-generated Actor parent is accepted"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(ParentBlueprint->GeneratedClass->GetPathName(), ChildPackage), Result, Error));
    TestEqual(TEXT("generated parent identity is retained"), Result->GetStringField(TEXT("parent_class")), ParentBlueprint->GeneratedClass->GetPathName());

    TestFalse(TEXT("existing destination is never overwritten"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), ParentPackage), Result, Error));
    TestEqual(TEXT("duplicate error is stable"), Error.Code, FString(TEXT("already_exists")));
    TestNotNull(TEXT("existing destination remains available"), LoadObject<UBlueprint>(nullptr, *ParentObject));
    TestFalse(TEXT("case-only duplicate destination is rejected"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), ParentPackage.ToLower()), Result, Error));
    TestEqual(TEXT("case-only duplicate error is stable"), Error.Code, FString(TEXT("already_exists")));

    TestFalse(TEXT("non-Actor parent rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/CoreUObject.Object"), Base + TEXT("/BP_Invalid")), Result, Error));
    TestEqual(TEXT("invalid parent error is stable"), Error.Code, FString(TEXT("invalid_parent")));
    TestFalse(TEXT("abstract Actor parent rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Light"), Base + TEXT("/BP_Abstract")), Result, Error));
    TestEqual(TEXT("abstract parent error is stable"), Error.Code, FString(TEXT("invalid_parent")));
    TestFalse(TEXT("Blueprint skeleton parent rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(ParentBlueprint->SkeletonGeneratedClass->GetPathName(), Base + TEXT("/BP_Skeleton")), Result, Error));
    TestEqual(TEXT("skeleton parent error is stable"), Error.Code, FString(TEXT("invalid_parent")));
    TestFalse(TEXT("engine mutation rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), TEXT("/Engine/UnrealMCP/BP_Forbidden")), Result, Error));
    TestEqual(TEXT("engine scope error is stable"), Error.Code, FString(TEXT("mutation_scope_denied")));
    TestFalse(TEXT("object-suffixed package destination rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), Base + TEXT("/BP_Bad.BP_Bad")), Result, Error));
    TestEqual(TEXT("invalid package path error is stable"), Error.Code, FString(TEXT("invalid_argument")));

    const FString ExternalDirectory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPExternalContent"));
    IFileManager::Get().MakeDirectory(*ExternalDirectory, true);
    FPackageName::RegisterMountPoint(TEXT("/UnrealMCPExternal/"), ExternalDirectory + TEXT("/"));
    TestFalse(TEXT("external plugin-style mount rejects mutation"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), TEXT("/UnrealMCPExternal/BP_Forbidden")), Result, Error));
    TestEqual(TEXT("external mount error is stable"), Error.Code, FString(TEXT("mutation_scope_denied")));
    FPackageName::UnRegisterMountPoint(TEXT("/UnrealMCPExternal/"), ExternalDirectory + TEXT("/"));

    const FString LocalPluginRoot = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UnrealMCPPhase3TestPlugin"));
    const FString LocalPluginDirectory = FPaths::Combine(LocalPluginRoot, TEXT("Content"));
    IFileManager::Get().MakeDirectory(*LocalPluginDirectory, true);
    TestTrue(TEXT("local plugin descriptor is written"), FFileHelper::SaveStringToFile(
        TEXT("{\"FileVersion\":3}"), *FPaths::Combine(LocalPluginRoot, TEXT("UnrealMCPPhase3TestPlugin.uplugin"))));
    FPackageName::RegisterMountPoint(TEXT("/UnrealMCPPhase3Plugin/"), LocalPluginDirectory + TEXT("/"));
    const FString LocalPluginPackage = TEXT("/UnrealMCPPhase3Plugin/BP_Local_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestTrue(TEXT("local project-plugin content creation succeeds"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), LocalPluginPackage), Result, Error));
    // Keep the dynamic mount alive through the editor's deferred validate-on-save pass.

    TestTrue(TEXT("explicit compile succeeds"), Mutator.Execute(TEXT("blueprint_compile"), AssetArguments(ParentObject), Result, Error));
    TestTrue(TEXT("explicit compile reports success"), Result->GetBoolField(TEXT("compile_succeeded")));
    TestFalse(TEXT("compile does not claim save"), Result->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("explicit save succeeds"), Mutator.Execute(TEXT("blueprint_save"), AssetArguments(ParentObject), Result, Error));
    TestTrue(TEXT("explicit save reports save"), Result->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("explicit save leaves package clean"), Result->GetBoolField(TEXT("package_dirty")));

    const FString ExistingFilename = FPackageName::LongPackageNameToFilename(ParentPackage, FPackageName::GetAssetPackageExtension());
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    TestTrue(TEXT("existing package becomes read-only"), PlatformFile.SetReadOnly(*ExistingFilename, true));
    TestFalse(TEXT("read-only existing package rejects save"), Mutator.Execute(
        TEXT("blueprint_save"), AssetArguments(ParentObject), Result, Error));
    TestEqual(TEXT("read-only save error is distinct"), Error.Code, FString(TEXT("write_conflict")));
    TestTrue(TEXT("existing package permissions are restored"), PlatformFile.SetReadOnly(*ExistingFilename, false));

    const FString ReadOnlyDirectory = FPackageName::LongPackageNameToFilename(Base + TEXT("/ReadOnly"));
    IFileManager::Get().MakeDirectory(*ReadOnlyDirectory, true);
    TestTrue(TEXT("test directory becomes read-only"), PlatformFile.SetReadOnly(*ReadOnlyDirectory, true));
    TestFalse(TEXT("read-only creation rejects before package creation"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), Base + TEXT("/ReadOnly/BP_NoWrite")), Result, Error));
    TestEqual(TEXT("read-only error is distinct"), Error.Code, FString(TEXT("write_conflict")));
    TestTrue(TEXT("test directory permissions are restored"), PlatformFile.SetReadOnly(*ReadOnlyDirectory, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase3FailureTest, "UnrealMCP.Phase3.FailureCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase3FailureTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    const FString CompileFailurePackage = Base + TEXT("/BP_CompileFailure");
    FUnrealMCPBlueprintMutator CompileFailure(Inspector,
        [](UBlueprint* Blueprint, FCompilerResultsLog& Log)
        {
            Log.Error(TEXT("Synthetic bounded compile failure"));
            Blueprint->Status = BS_Error;
        });
    TestFalse(TEXT("creation compile failure is reported"), CompileFailure.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), CompileFailurePackage), Result, Error));
    TestEqual(TEXT("creation compile error is distinct"), Error.Code, FString(TEXT("compile_failed")));
    TestFalse(TEXT("compile-failed creation is not registered"), FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(
        FSoftObjectPath(CompileFailurePackage + TEXT(".BP_CompileFailure"))).IsValid());
    FString CompileFailureFilename;
    FPackageName::TryConvertLongPackageNameToFilename(CompileFailurePackage, CompileFailureFilename, FPackageName::GetAssetPackageExtension());
    TestFalse(TEXT("compile-failed creation leaves no file"), IFileManager::Get().FileExists(*CompileFailureFilename));

    const FString SaveFailurePackage = Base + TEXT("/BP_SaveFailure");
    FUnrealMCPBlueprintMutator SaveFailure(Inspector, FUnrealMCPBlueprintMutator::FCompile(), [](UBlueprint*) { return false; });
    TestFalse(TEXT("creation save failure is reported"), SaveFailure.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), SaveFailurePackage), Result, Error));
    TestEqual(TEXT("creation save error is distinct"), Error.Code, FString(TEXT("save_failed")));
    TestFalse(TEXT("save-failed creation is not registered"), FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(
        FSoftObjectPath(SaveFailurePackage + TEXT(".BP_SaveFailure"))).IsValid());

    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TestTrue(TEXT("compile-failed destination can be retried"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), CompileFailurePackage), Result, Error));
    TestTrue(TEXT("save-failed destination can be retried"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), SaveFailurePackage), Result, Error));

    const FString ExistingObject = CompileFailurePackage + TEXT(".BP_CompileFailure");
    FUnrealMCPBlueprintMutator ExplicitCompileFailure(Inspector,
        [](UBlueprint* Blueprint, FCompilerResultsLog& Log)
        {
            Log.Error(TEXT("Synthetic explicit compile failure"));
            Blueprint->Status = BS_Error;
        });
    TestTrue(TEXT("explicit compile failure returns structured result"), ExplicitCompileFailure.Execute(
        TEXT("blueprint_compile"), AssetArguments(ExistingObject), Result, Error));
    TestFalse(TEXT("explicit compile result reports failure"), Result->GetBoolField(TEXT("compile_succeeded")));
    TestTrue(TEXT("explicit compile result contains diagnostics"), Result->GetIntegerField(TEXT("diagnostic_count")) > 0);
    TestTrue(TEXT("normal compile restores fixture"), Mutator.Execute(TEXT("blueprint_compile"), AssetArguments(ExistingObject), Result, Error));

    FUnrealMCPBlueprintMutator ExplicitSaveFailure(Inspector, FUnrealMCPBlueprintMutator::FCompile(), [](UBlueprint*) { return false; });
    TestFalse(TEXT("explicit save failure is reported"), ExplicitSaveFailure.Execute(
        TEXT("blueprint_save"), AssetArguments(ExistingObject), Result, Error));
    TestEqual(TEXT("explicit save error is distinct"), Error.Code, FString(TEXT("save_failed")));
    TestNotNull(TEXT("save failure preserves existing asset"), LoadObject<UBlueprint>(nullptr, *ExistingObject));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase3LiveFixtureTest, "UnrealMCP.Phase3.CreationLiveFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase3LiveFixtureTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPPhase3/BP_CreationFixture_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    TestTrue(TEXT("live creation fixture is created through production mutator"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), PackageName), Result, Error));
    if (!Result.IsValid())
    {
        return false;
    }
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestEqual(TEXT("live fixture snapshot is bounded"), Snapshot.Len(), 40);
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_PHASE3_SNAPSHOT=%s"), *Snapshot);
    return true;
}


#endif
