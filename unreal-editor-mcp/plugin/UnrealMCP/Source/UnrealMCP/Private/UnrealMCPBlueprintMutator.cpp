#include "UnrealMCPBlueprintMutator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

namespace
{
bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed)
    {
        Names.Add(Name);
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key))
        {
            return false;
        }
    }
    return true;
}

FString ObjectPathForPackage(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

bool NormalizePackagePath(const FString& Input, FString& OutPackageName)
{
    OutPackageName = Input;
    return Input.StartsWith(TEXT("/")) && !Input.StartsWith(TEXT("//")) && !Input.EndsWith(TEXT("/"))
        && !Input.Contains(TEXT("..")) && !Input.Contains(TEXT("\\")) && !Input.Contains(TEXT("."))
        && Input.Len() <= 512 && FPackageName::IsValidLongPackageName(Input, true)
        && !FPackageName::GetLongPackageAssetName(Input).IsEmpty();
}

bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath, FString& OutPackageName)
{
    if (!Input.StartsWith(TEXT("/")) || Input.StartsWith(TEXT("//")) || Input.Contains(TEXT(".."))
        || Input.Contains(TEXT("\\")) || Input.Len() > 512)
    {
        return false;
    }
    OutPackageName = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(OutPackageName, true))
    {
        return false;
    }
    if (Input.Contains(TEXT(".")))
    {
        if (!FPackageName::IsValidObjectPath(Input)
            || FPackageName::ObjectPathToObjectName(Input) != FPackageName::GetLongPackageAssetName(OutPackageName))
        {
            return false;
        }
        OutObjectPath = Input;
    }
    else
    {
        OutObjectPath = ObjectPathForPackage(OutPackageName);
    }
    return FPackageName::IsValidObjectPath(OutObjectPath);
}

bool PathContainsSymlink(const FString& Root, const FString& Candidate)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FString Current = Root;
    FPaths::NormalizeDirectoryName(Current);
    if (PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink)
    {
        return true;
    }
    FString Relative = Candidate;
    FPaths::NormalizeDirectoryName(Relative);
    if (!FPaths::MakePathRelativeTo(Relative, *(Current + TEXT("/"))))
    {
        return true;
    }
    TArray<FString> Segments;
    Relative.ParseIntoArray(Segments, TEXT("/"), true);
    for (const FString& Segment : Segments)
    {
        Current /= Segment;
        if (PlatformFile.FileExists(*Current) || PlatformFile.DirectoryExists(*Current))
        {
            if (PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink)
            {
                return true;
            }
        }
    }
    return false;
}

bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError)
{
    FString PhysicalTarget;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The destination mount is not available")};
        return false;
    }
    PhysicalTarget = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PhysicalTarget));
    FPaths::NormalizeDirectoryName(PhysicalTarget);
    if (PackageName.StartsWith(TEXT("/Game/")))
    {
        FString ProjectContent = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeDirectoryName(ProjectContent);
        if (!(FPaths::IsSamePath(PhysicalTarget, ProjectContent) || FPaths::IsUnderDirectory(PhysicalTarget, ProjectContent))
            || PathContainsSymlink(ProjectContent, PhysicalTarget))
        {
            OutError = {TEXT("mutation_scope_denied"), TEXT("Project content resolves outside its physical mount")};
            return false;
        }
        return true;
    }
    const int32 SecondSlash = PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    if (SecondSlash == INDEX_NONE)
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The destination is outside mutable project content")};
        return false;
    }
    const FString MountRoot = PackageName.Left(SecondSlash + 1);
    FString MountDirectory;
    if (!FPackageName::TryConvertLongPackageNameToFilename(MountRoot, MountDirectory))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The destination mount is not available")};
        return false;
    }
    FString ProjectPlugins = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
    FString PhysicalMount = FPaths::ConvertRelativePathToFull(MountDirectory);
    FPaths::NormalizeDirectoryName(ProjectPlugins);
    FPaths::NormalizeDirectoryName(PhysicalMount);
    if (!FPaths::IsUnderDirectory(PhysicalMount, ProjectPlugins)
        || !(FPaths::IsSamePath(PhysicalTarget, PhysicalMount) || FPaths::IsUnderDirectory(PhysicalTarget, PhysicalMount))
        || PathContainsSymlink(ProjectPlugins, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("Only local project-plugin content mounts are mutable outside /Game")};
        return false;
    }
    bool bOwnedByPlugin = false;
    FString Candidate = PhysicalMount;
    while (FPaths::IsUnderDirectory(Candidate, ProjectPlugins))
    {
        TArray<FString> PluginDescriptors;
        IFileManager::Get().FindFiles(PluginDescriptors, *(Candidate / TEXT("*.uplugin")), true, false);
        if (!PluginDescriptors.IsEmpty())
        {
            bOwnedByPlugin = true;
            break;
        }
        const FString Parent = FPaths::GetPath(Candidate);
        if (Parent == Candidate)
        {
            break;
        }
        Candidate = Parent;
    }
    if (!bOwnedByPlugin)
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The local content mount is not owned by a project plugin")};
        return false;
    }
    return true;
}

bool ExistingWritableDirectory(const FString& Filename, FString& OutDirectory)
{
    OutDirectory = FPaths::GetPath(Filename);
    while (!OutDirectory.IsEmpty() && !IFileManager::Get().DirectoryExists(*OutDirectory))
    {
        const FString Parent = FPaths::GetPath(OutDirectory);
        if (Parent == OutDirectory)
        {
            break;
        }
        OutDirectory = Parent;
    }
    return !OutDirectory.IsEmpty() && !IFileManager::Get().IsReadOnly(*OutDirectory);
}

bool ValidateWritableTarget(const FString& PackageName, FString& OutFilename, FUnrealMCPError& OutError)
{
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, OutFilename, FPackageName::GetAssetPackageExtension()))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The package path does not resolve to writable mounted content")};
        return false;
    }
    FString Directory;
    if ((IFileManager::Get().FileExists(*OutFilename) && IFileManager::Get().IsReadOnly(*OutFilename))
        || !ExistingWritableDirectory(OutFilename, Directory))
    {
        OutError = {TEXT("write_conflict"), TEXT("The package destination is read-only or unavailable")};
        return false;
    }
    return true;
}

bool ResolveParent(const FString& Path, UClass*& OutClass, FUnrealMCPError& OutError)
{
    if (Path.Len() < 3 || Path.Len() > 512 || !Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")))
    {
        OutError = {TEXT("invalid_parent"), TEXT("parent_class must be one bounded Unreal class path")};
        return false;
    }
    OutClass = LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (OutClass == nullptr)
    {
        OutError = {TEXT("invalid_parent"), TEXT("The parent class was not found")};
        return false;
    }
    const bool bGenerated = Cast<UBlueprintGeneratedClass>(OutClass) != nullptr;
    const bool bUnusableFlags = OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
    const bool bTransientName = OutClass->GetName().StartsWith(TEXT("SKEL_")) || OutClass->GetName().StartsWith(TEXT("REINST_"));
    if (!OutClass->IsChildOf(AActor::StaticClass()) || bUnusableFlags || bTransientName
        || FKismetEditorUtilities::IsClassABlueprintSkeleton(OutClass) || IsEditorOnlyObject(OutClass)
        || (!bGenerated && !FKismetEditorUtilities::CanCreateBlueprintOfClass(OutClass)))
    {
        OutError = {TEXT("invalid_parent"), TEXT("The parent must be a usable Blueprint-compatible Actor class")};
        return false;
    }
    if (bGenerated)
    {
        UBlueprint* ParentBlueprint = UBlueprint::GetBlueprintFromClass(OutClass);
        if (ParentBlueprint == nullptr || ParentBlueprint->Status == BS_Error || ParentBlueprint->bBeingCompiled)
        {
            OutError = {TEXT("invalid_parent"), TEXT("The Blueprint-generated parent is unavailable or has compile errors")};
            return false;
        }
    }
    return true;
}

FString CompileState(EBlueprintStatus Status)
{
    switch (Status)
    {
    case BS_Dirty: return TEXT("dirty");
    case BS_Error: return TEXT("error");
    case BS_UpToDate: return TEXT("up_to_date");
    case BS_BeingCreated: return TEXT("being_created");
    case BS_UpToDateWithWarnings: return TEXT("up_to_date_with_warnings");
    default: return TEXT("unknown");
    }
}

FString SeverityName(EMessageSeverity::Type Severity)
{
    if (Severity == EMessageSeverity::Error) return TEXT("error");
    if (Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning) return TEXT("warning");
    return TEXT("note");
}

void AddDiagnostics(const FCompilerResultsLog& Log, const TSharedRef<FJsonObject>& Result)
{
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    const int32 Count = FMath::Min(Log.Messages.Num(), UnrealMCP::MaxCompilerDiagnostics);
    Diagnostics.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const TSharedRef<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("severity"), SeverityName(Log.Messages[Index]->GetSeverity()));
        Diagnostic->SetStringField(TEXT("message"), Log.Messages[Index]->ToText().ToString().Left(UnrealMCP::MaxDiagnosticChars));
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    Result->SetNumberField(TEXT("diagnostic_count"), Log.Messages.Num());
    Result->SetBoolField(TEXT("diagnostics_truncated"), Log.Messages.Num() > Count);
}

bool ResolveMutableBlueprint(
    const FJsonObject& Arguments,
    UBlueprint*& OutBlueprint,
    FString& OutObjectPath,
    FString& OutPackageName,
    FUnrealMCPError& OutError)
{
    if (!HasOnlyFields(Arguments, {TEXT("asset_path")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The command accepts only asset_path")};
        return false;
    }
    FString RawPath;
    if (!Arguments.TryGetStringField(TEXT("asset_path"), RawPath)
        || !NormalizeAssetPath(RawPath, OutObjectPath, OutPackageName))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must identify one exact Blueprint asset")};
        return false;
    }
    if (!ValidateMutationScope(OutPackageName, OutError))
    {
        return false;
    }
    const FAssetData Asset = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(OutObjectPath));
    if (!Asset.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested asset was not found")};
        return false;
    }
    OutBlueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (OutBlueprint == nullptr)
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested asset is not a Blueprint")};
        return false;
    }
    if (OutBlueprint->ParentClass == nullptr || !OutBlueprint->ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested Blueprint is not Actor-derived")};
        return false;
    }
    if (OutBlueprint->bBeingCompiled)
    {
        OutError = {TEXT("busy"), TEXT("The requested Blueprint is already compiling"), MakeShared<FJsonObject>(), true};
        return false;
    }
    return true;
}

bool ReadSnapshot(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& ObjectPath,
    FString& OutSnapshot,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), ObjectPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("summary"))});
    Arguments->SetNumberField(TEXT("page_size"), 1);
    TSharedPtr<FJsonObject> Inspection;
    if (!Inspector.Execute(Arguments, Inspection, OutError) || !Inspection.IsValid()
        || !Inspection->TryGetStringField(TEXT("snapshot_id"), OutSnapshot))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("internal_error"), TEXT("Blueprint read-back verification did not return a snapshot")};
        }
        return false;
    }
    return true;
}

TSharedRef<FJsonObject> BuildResult(
    UBlueprint* Blueprint,
    const FString& ObjectPath,
    const FString& Snapshot,
    const FCompilerResultsLog* Log,
    bool bCompileSucceeded,
    bool bSaved)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->GetPathName() : FString());
    Result->SetStringField(TEXT("compile_state"), CompileState(Blueprint->Status));
    Result->SetBoolField(TEXT("compile_succeeded"), bCompileSucceeded);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    if (Log != nullptr)
    {
        AddDiagnostics(*Log, Result);
    }
    else
    {
        Result->SetArrayField(TEXT("diagnostics"), {});
        Result->SetNumberField(TEXT("diagnostic_count"), 0);
        Result->SetBoolField(TEXT("diagnostics_truncated"), false);
    }
    return Result;
}

bool PackageAlreadyExists(const FString& PackageName)
{
    const FString ObjectPath = ObjectPathForPackage(PackageName);
    FString Filename;
    FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetAssetPackageExtension());
    UPackage* LoadedPackage = FindPackage(nullptr, *PackageName);
    UObject* LoadedObject = FindObject<UObject>(nullptr, *ObjectPath);
    return (LoadedPackage != nullptr && !LoadedPackage->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
        || (LoadedObject != nullptr && !LoadedObject->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
        || FPackageName::DoesPackageExist(PackageName)
        || (!Filename.IsEmpty() && IFileManager::Get().FileExists(*Filename))
        || FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
}

void CleanupFailedCreation(UPackage* Package, UBlueprint* Blueprint, const FString& Filename, bool bPublished)
{
    if (Package == nullptr)
    {
        return;
    }
    if (bPublished && Blueprint != nullptr)
    {
        FAssetRegistryModule::AssetDeleted(Blueprint);
    }
    if (!Filename.IsEmpty() && IFileManager::Get().FileExists(*Filename))
    {
        IFileManager::Get().Delete(*Filename, false, true, true);
    }
    Package->SetDirtyFlag(false);
    Package->Rename(
        *(TEXT("/Temp/UnrealMCPFailed_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
        nullptr,
        REN_DontCreateRedirectors | REN_NonTransactional);
    if (Blueprint != nullptr)
    {
        Blueprint->ClearFlags(RF_Public | RF_Standalone);
        Blueprint->MarkAsGarbage();
    }
    ForEachObjectWithPackage(Package, [](UObject* Object)
    {
        Object->ClearFlags(RF_Public | RF_Standalone);
        Object->MarkAsGarbage();
        return true;
    }, EGetObjectsFlags::IncludeNestedObjects);
    Package->MarkAsGarbage();
}
}

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
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    if (Command == TEXT("blueprint_create")) return Create(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_compile")) return Compile(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_save")) return Save(Arguments, OutResult, OutError);
    OutError = {TEXT("invalid_argument"), TEXT("Unknown Blueprint mutation command")};
    return false;
}

bool FUnrealMCPBlueprintMutator::Create(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!HasOnlyFields(*Arguments, {TEXT("parent_class"), TEXT("package_path")}))
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
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*Arguments, Blueprint, ObjectPath, PackageName, OutError))
    {
        return false;
    }
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
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*Arguments, Blueprint, ObjectPath, PackageName, OutError))
    {
        return false;
    }
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
