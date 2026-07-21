#include "UnrealMCPBlueprintCallableMutationSupport.h"


FUnrealMCPBlueprintMutator::FUnrealMCPBlueprintMutator(
    FUnrealMCPBlueprintInspector& InInspector,
    FCompile InCompile,
    FSave InSave)
    : Inspector(InInspector)
    , CompileBlueprint(InCompile ? MoveTemp(InCompile) : FCompile([](UBlueprint* Blueprint, FCompilerResultsLog& Log)
      {
          FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
      }))
    , SaveBlueprint(InSave ? MoveTemp(InSave) : FSave([](UBlueprint* Blueprint)
      {
          const FString Filename = FPackageName::LongPackageNameToFilename(
              Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
          FSavePackageArgs SaveArgs;
          SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
          SaveArgs.SaveFlags = SAVE_NoError;
          SaveArgs.bSlowTask = false;
          return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
      }))
{
}

bool FUnrealMCPBlueprintMutator::Execute(
    const FString& Command,
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    if (Command == TEXT("blueprint_create")) return Create(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_compile")) return Compile(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_save")) return Save(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_component_edit")) return ComponentEdit(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_default_edit")) return DefaultEdit(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_member_edit")) return MemberEdit(Arguments, OutResult, OutError);
    OutError = {TEXT("invalid_argument"), TEXT("Unknown Blueprint mutation command")};
    return false;
}

bool FUnrealMCPBlueprintMutator::Create(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("parent_class"), TEXT("package_path")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_create accepts only parent_class and package_path")};
        return false;
    }
    FString ParentPath;
    FString RawPackagePath;
    FString PackageName;
    if (!Arguments->TryGetStringField(TEXT("parent_class"), ParentPath)
        || !Arguments->TryGetStringField(TEXT("package_path"), RawPackagePath)
        || !NormalizePackagePath(RawPackagePath, PackageName))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be one valid long package name without an object suffix")};
        return false;
    }
    if (!ValidateMutationScope(PackageName, OutError))
    {
        return false;
    }
    UClass* ParentClass = nullptr;
    if (!ResolveParent(ParentPath, ParentClass, OutError))
    {
        return false;
    }
    if (PackageAlreadyExists(PackageName))
    {
        OutError = {TEXT("already_exists"), TEXT("The destination package or asset already exists")};
        return false;
    }
    FString Filename;
    if (!ValidateWritableTarget(PackageName, Filename, OutError))
    {
        return false;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UPackage* Package = CreatePackage(*PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, FName(*AssetName), BPTYPE_Normal, FName(TEXT("UnrealMCP")));
    if (Blueprint == nullptr)
    {
        CleanupFailedCreation(Package, nullptr, Filename, false);
        OutError = {TEXT("internal_error"), TEXT("Unreal could not create the Blueprint package")};
        return false;
    }

    FCompilerResultsLog Log;
    Log.bSilentMode = true;
    CompileBlueprint(Blueprint, Log);
    const bool bCompiled = Log.NumErrors == 0 && Blueprint->Status != BS_Error;
    if (!bCompiled)
    {
        const FString First = Log.Messages.IsEmpty() ? FString() : Log.Messages[0]->ToText().ToString().Left(UnrealMCP::MaxDiagnosticChars);
        CleanupFailedCreation(Package, Blueprint, Filename, false);
        OutError = {TEXT("compile_failed"), TEXT("The new Blueprint did not compile")};
        OutError.Details->SetNumberField(TEXT("diagnostic_count"), Log.Messages.Num());
        if (!First.IsEmpty()) OutError.Details->SetStringField(TEXT("first_diagnostic"), First);
        return false;
    }
    if (!SaveBlueprint(Blueprint))
    {
        CleanupFailedCreation(Package, Blueprint, Filename, false);
        OutError = {TEXT("save_failed"), TEXT("The new Blueprint package could not be saved")};
        return false;
    }
    FAssetRegistryModule::AssetCreated(Blueprint);
    FString Snapshot;
    if (!ReadSnapshot(Inspector, Blueprint->GetPathName(), Snapshot, OutError))
    {
        CleanupFailedCreation(Package, Blueprint, Filename, true);
        return false;
    }
    OutResult = BuildResult(Blueprint, Blueprint->GetPathName(), Snapshot, &Log, true, true);
    return true;
}

bool FUnrealMCPBlueprintMutator::Compile(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (Arguments->HasField(TEXT("operation_id")) && !Arguments->HasField(TEXT("expected_snapshot")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("expected_snapshot is required for a reconciled compile")};
        return false;
    }
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*Arguments, Blueprint, ObjectPath, PackageName, OutError))
    {
        return false;
    }
    if (!ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    FCompilerResultsLog Log;
    Log.bSilentMode = true;
    CompileBlueprint(Blueprint, Log);
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        return false;
    }
    const bool bCompiled = Log.NumErrors == 0 && Blueprint->Status != BS_Error;
    OutResult = BuildResult(Blueprint, ObjectPath, Snapshot, &Log, bCompiled, false);
    return true;
}

bool FUnrealMCPBlueprintMutator::Save(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (Arguments->HasField(TEXT("operation_id")) && !Arguments->HasField(TEXT("expected_snapshot")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("expected_snapshot is required for a reconciled save")};
        return false;
    }
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*Arguments, Blueprint, ObjectPath, PackageName, OutError))
    {
        return false;
    }
    if (!ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    FString Filename;
    if (!ValidateWritableTarget(PackageName, Filename, OutError))
    {
        return false;
    }
    if (!SaveBlueprint(Blueprint))
    {
        OutError = {TEXT("save_failed"), TEXT("The Blueprint package could not be saved")};
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        return false;
    }
    OutResult = BuildResult(Blueprint, ObjectPath, Snapshot, nullptr, Blueprint->Status != BS_Error, true);
    return true;
}
