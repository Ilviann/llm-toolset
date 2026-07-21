#include "UnrealMCPBlueprintMutator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "SubobjectDataSubsystem.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPPropertyCodec.h"
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
    if (!HasOnlyFields(Arguments, {TEXT("asset_path"), TEXT("operation_id"), TEXT("expected_snapshot")}))
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

bool ValidateExpectedSnapshot(
    FUnrealMCPBlueprintInspector& Inspector,
    const FJsonObject& Arguments,
    const FString& ObjectPath,
    FUnrealMCPError& OutError)
{
    if (!Arguments.HasField(TEXT("expected_snapshot"))) return true;
    FString Expected;
    FString Current;
    if (!Arguments.TryGetStringField(TEXT("expected_snapshot"), Expected) || Expected.Len() != 40)
    {
        OutError = {TEXT("invalid_argument"), TEXT("expected_snapshot must be one 40-character structural snapshot")};
        return false;
    }
    if (!ReadSnapshot(Inspector, ObjectPath, Current, OutError)) return false;
    if (Current != Expected)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The Blueprint structural snapshot changed before mutation")};
        OutError.Details->SetStringField(TEXT("current_snapshot"), Current);
        return false;
    }
    return true;
}

bool RequireMutationPreconditions(const FJsonObject& Arguments, FUnrealMCPError& OutError)
{
    FString OperationId;
    FString ExpectedSnapshot;
    if (!Arguments.TryGetStringField(TEXT("operation_id"), OperationId)
        || !Arguments.TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot))
    {
        OutError = {TEXT("invalid_argument"), TEXT("operation_id and expected_snapshot are required")};
        return false;
    }
    return true;
}

bool IsLegalComponentName(const FString& Name)
{
    return !Name.IsEmpty() && Name.Len() <= 128 && !FName(*Name).IsNone() && FName(*Name).IsValidXName();
}

USCS_Node* FindLocalNode(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr || Identity.Len() != 32) return nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node != nullptr && Node->VariableGuid.IsValid()
            && Node->VariableGuid.ToString(EGuidFormats::Digits).ToLower() == Identity)
        {
            return Node;
        }
    }
    return nullptr;
}

USCS_Node* FindLocalNodeByName(UBlueprint* Blueprint, const FString& Name)
{
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr) return nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node != nullptr && Node->GetVariableName().ToString() == Name) return Node;
    }
    return nullptr;
}

struct FComponentHandles
{
    FSubobjectDataHandle Context;
    TMap<FString, FSubobjectDataHandle> ById;
};

bool GatherComponentHandles(UBlueprint* Blueprint, USubobjectDataSubsystem* Subsystem, FComponentHandles& Out, FUnrealMCPError& OutError)
{
    if (Blueprint == nullptr || Subsystem == nullptr || Blueprint->SimpleConstructionScript == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The Blueprint component subsystem is unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }
    TArray<FSubobjectDataHandle> Handles;
    Subsystem->K2_GatherSubobjectDataForBlueprint(Blueprint, Handles);
    for (const FSubobjectDataHandle& Handle : Handles)
    {
        const FSubobjectData* Data = Handle.GetData();
        if (Data != nullptr && Data->IsActor())
        {
            Out.Context = Handle;
            break;
        }
    }
    if (!Out.Context.IsValid() && !Handles.IsEmpty()) Out.Context = Handles[0];
    if (!Out.Context.IsValid())
    {
        OutError = {TEXT("busy"), TEXT("The Blueprint component context is unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node == nullptr || !Node->VariableGuid.IsValid() || Node->ComponentTemplate == nullptr) continue;
        FSubobjectDataHandle Handle = Subsystem->FindHandleForObject(Out.Context, Node->ComponentTemplate, Blueprint);
        if (!Handle.IsValid())
        {
            for (const FSubobjectDataHandle& Candidate : Handles)
            {
                const FSubobjectData* Data = Candidate.GetData();
                if (Data != nullptr && (Data->GetObjectForBlueprint(Blueprint) == Node->ComponentTemplate
                    || Data->GetVariableName() == Node->GetVariableName()))
                {
                    Handle = Candidate;
                    break;
                }
            }
        }
        if (Handle.IsValid()) Out.ById.Add(Node->VariableGuid.ToString(EGuidFormats::Digits).ToLower(), Handle);
    }
    return true;
}

bool ResolveComponentClass(const FString& Path, UClass*& OutClass, FUnrealMCPError& OutError)
{
    if (Path.Len() < 3 || Path.Len() > 512 || !Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")))
    {
        OutError = {TEXT("invalid_component"), TEXT("component_class must be one bounded Unreal class path")};
        return false;
    }
    OutClass = LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (OutClass == nullptr || !OutClass->IsChildOf(UActorComponent::StaticClass())
        || OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || OutClass->GetOutermost() == GetTransientPackage() || IsEditorOnlyObject(OutClass)
        || OutClass->HasMetaDataHierarchical(TEXT("BlueprintSpawnableComponent")) == nullptr)
    {
        OutError = {TEXT("invalid_component"), TEXT("The class is not a usable spawnable Actor component class")};
        return false;
    }
    return true;
}

TSharedRef<FJsonObject> BuildEditResult(
    UBlueprint* Blueprint,
    const FString& ObjectPath,
    const FString& Snapshot,
    const FString& Edit,
    const TSharedPtr<FJsonObject>& Changed,
    const TArray<FString>& ReconstructedIds = {})
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("edit"), Edit);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    Result->SetObjectField(TEXT("changed"), Changed.IsValid() ? Changed : MakeShared<FJsonObject>());
    TArray<TSharedPtr<FJsonValue>> Ids;
    for (const FString& Id : ReconstructedIds) Ids.Add(MakeShared<FJsonValueString>(Id));
    Result->SetArrayField(TEXT("reconstructed_identities"), Ids);
    return Result;
}

bool RestoreFailedTransaction(FUnrealMCPError& OutError)
{
    if (GEditor != nullptr && GEditor->UndoTransaction()) return true;
    OutError = {TEXT("internal_error"), TEXT("An unexpected mutation result could not be restored through Undo")};
    return false;
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
    if (Command == TEXT("blueprint_component_edit")) return ComponentEdit(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_default_edit")) return DefaultEdit(Arguments, OutResult, OutError);
    OutError = {TEXT("invalid_argument"), TEXT("Unknown Blueprint mutation command")};
    return false;
}

bool FUnrealMCPBlueprintMutator::Create(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
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

bool FUnrealMCPBlueprintMutator::ComponentEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!RequireMutationPreconditions(*Arguments, OutError)) return false;
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_component_edit requires one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("component_class"), TEXT("name"), TEXT("parent_id")});
    else if (Operation == TEXT("remove")) Allowed.Add(TEXT("component_id"));
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("component_id"), TEXT("new_name")});
    else if (Operation == TEXT("reparent")) Allowed.Append({TEXT("component_id"), TEXT("new_parent_id")});
    else if (Operation == TEXT("set_root")) Allowed.Add(TEXT("component_id"));
    else if (Operation == TEXT("set_property")) Allowed.Append({TEXT("component_id"), TEXT("property_name"), TEXT("value")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown component edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The component edit contains a field not accepted by its operation")};
            return false;
        }
    }

    FString RawAsset;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAsset))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must identify one exact Blueprint asset")};
        return false;
    }
    const TSharedRef<FJsonObject> AssetOnly = MakeShared<FJsonObject>();
    AssetOnly->SetStringField(TEXT("asset_path"), RawAsset);
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*AssetOnly, Blueprint, ObjectPath, PackageName, OutError)
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError))
    {
        return false;
    }
    USubobjectDataSubsystem* Subsystem = USubobjectDataSubsystem::Get();
    FComponentHandles Handles;
    if (!GatherComponentHandles(Blueprint, Subsystem, Handles, OutError)) return false;

    FString ComponentId;
    FString NewParentId;
    FString NewName;
    FString ComponentClassPath;
    FString PropertyName;
    USCS_Node* Node = nullptr;
    USCS_Node* ParentNode = nullptr;
    FSubobjectDataHandle Handle;
    FSubobjectDataHandle ParentHandle = Handles.Context;
    UClass* ComponentClass = nullptr;

    if (Operation == TEXT("add"))
    {
        if (!Arguments->TryGetStringField(TEXT("component_class"), ComponentClassPath)
            || !Arguments->TryGetStringField(TEXT("name"), NewName) || !IsLegalComponentName(NewName)
            || !ResolveComponentClass(ComponentClassPath, ComponentClass, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_component"), TEXT("add requires a valid spawnable component_class and unique legal name")};
            return false;
        }
        if (FindLocalNodeByName(Blueprint, NewName) != nullptr)
        {
            OutError = {TEXT("invalid_component"), TEXT("A local component already uses that exact name")};
            return false;
        }
        if (Arguments->HasField(TEXT("parent_id")))
        {
            if (!Arguments->TryGetStringField(TEXT("parent_id"), NewParentId)
                || (ParentNode = FindLocalNode(Blueprint, NewParentId)) == nullptr || !Handles.ById.Contains(NewParentId))
            {
                OutError = {TEXT("stale_precondition"), TEXT("The requested local parent component identity is unavailable")};
                return false;
            }
            if (!ComponentClass->IsChildOf(USceneComponent::StaticClass())
                || ParentNode->ComponentClass == nullptr || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
            {
                OutError = {TEXT("invalid_component"), TEXT("Only scene components can use a scene-component parent")};
                return false;
            }
            ParentHandle = Handles.ById[NewParentId];
        }
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("component_id"), ComponentId)
            || (Node = FindLocalNode(Blueprint, ComponentId)) == nullptr || !Handles.ById.Contains(ComponentId))
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local component identity is unavailable")};
            return false;
        }
        Handle = Handles.ById[ComponentId];
        const FSubobjectData* Data = Handle.GetData();
        if (Data == nullptr || Data->IsInheritedComponent() || Data->IsNativeComponent() || Data->IsInstancedComponent())
        {
            OutError = {TEXT("invalid_component"), TEXT("Only locally owned SCS components are mutable")};
            return false;
        }
    }

    if (Operation == TEXT("remove"))
    {
        if (!Node->GetChildNodes().IsEmpty())
        {
            OutError = {TEXT("invalid_component"), TEXT("Reparent or remove child components before removing their parent")};
            return false;
        }
        if (Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Node))
        {
            for (USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Candidate != Node && Candidate != nullptr && Candidate->ComponentClass != nullptr
                    && Candidate->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
                {
                    OutError = {TEXT("invalid_component"), TEXT("Select another scene root before removing the current root")};
                    return false;
                }
            }
        }
    }
    else if (Operation == TEXT("rename"))
    {
        if (!Arguments->TryGetStringField(TEXT("new_name"), NewName) || !IsLegalComponentName(NewName))
        {
            OutError = {TEXT("invalid_component"), TEXT("new_name must be one legal bounded component name")};
            return false;
        }
        USCS_Node* Existing = FindLocalNodeByName(Blueprint, NewName);
        if (Existing != nullptr && Existing != Node)
        {
            OutError = {TEXT("invalid_component"), TEXT("A local component already uses that exact name")};
            return false;
        }
    }
    else if (Operation == TEXT("reparent"))
    {
        if (!Arguments->TryGetStringField(TEXT("new_parent_id"), NewParentId)
            || (ParentNode = FindLocalNode(Blueprint, NewParentId)) == nullptr || !Handles.ById.Contains(NewParentId))
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested new parent identity is unavailable")};
            return false;
        }
        if (Node == ParentNode || ParentNode->IsChildOf(Node)
            || Node->ComponentClass == nullptr || ParentNode->ComponentClass == nullptr
            || !Node->ComponentClass->IsChildOf(USceneComponent::StaticClass())
            || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
        {
            OutError = {TEXT("invalid_component"), TEXT("Reparenting requires two distinct local scene components and must not create a cycle")};
            return false;
        }
        ParentHandle = Handles.ById[NewParentId];
    }
    else if (Operation == TEXT("set_root")
        && (Node->ComponentClass == nullptr || !Node->ComponentClass->IsChildOf(USceneComponent::StaticClass())))
    {
        OutError = {TEXT("invalid_component"), TEXT("Only a local scene component can become the Actor root")};
        return false;
    }
    else if (Operation == TEXT("set_property"))
    {
        if (!Arguments->TryGetStringField(TEXT("property_name"), PropertyName) || !Arguments->HasField(TEXT("value")))
        {
            OutError = {TEXT("invalid_argument"), TEXT("set_property requires one exact property_name and value")};
            return false;
        }
    }

    bool bApplied = false;
    TSharedPtr<FJsonObject> Changed = MakeShared<FJsonObject>();
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP component edit")));
        Blueprint->Modify();
        Blueprint->SimpleConstructionScript->Modify();
        if (Operation == TEXT("add"))
        {
            FAddNewSubobjectParams Params;
            Params.ParentHandle = ParentHandle;
            Params.NewClass = ComponentClass;
            Params.BlueprintContext = Blueprint;
            FText Failure;
            FSubobjectDataHandle NewHandle = Subsystem->AddNewSubobject(Params, Failure);
            bApplied = NewHandle.IsValid() && Subsystem->RenameSubobject(NewHandle, FText::FromString(NewName));
            Node = FindLocalNodeByName(Blueprint, NewName);
            bApplied = bApplied && Node != nullptr && Node->VariableGuid.IsValid() && Node->ComponentClass == ComponentClass;
            if (bApplied)
            {
                ComponentId = Node->VariableGuid.ToString(EGuidFormats::Digits).ToLower();
                Changed->SetStringField(TEXT("component_id"), ComponentId);
                Changed->SetStringField(TEXT("name"), NewName);
                Changed->SetStringField(TEXT("class_path"), ComponentClass->GetPathName());
                Changed->SetStringField(TEXT("parent_id"), NewParentId);
            }
        }
        else if (Operation == TEXT("remove"))
        {
            bApplied = Subsystem->DeleteSubobject(Handles.Context, Handle, Blueprint) == 1
                && FindLocalNode(Blueprint, ComponentId) == nullptr;
            Changed->SetStringField(TEXT("component_id"), ComponentId);
        }
        else if (Operation == TEXT("rename"))
        {
            bApplied = Subsystem->RenameSubobject(Handle, FText::FromString(NewName));
            Node = FindLocalNode(Blueprint, ComponentId);
            bApplied = bApplied && Node != nullptr && Node->GetVariableName().ToString() == NewName;
            Changed->SetStringField(TEXT("component_id"), ComponentId);
            Changed->SetStringField(TEXT("name"), NewName);
        }
        else if (Operation == TEXT("reparent"))
        {
            FReparentSubobjectParams Params;
            Params.NewParentHandle = ParentHandle;
            Params.BlueprintContext = Blueprint;
            Params.ActorPreviewContext = Blueprint->GeneratedClass != nullptr ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
            bApplied = Params.ActorPreviewContext != nullptr && Subsystem->ReparentSubobject(Params, Handle);
            Node = FindLocalNode(Blueprint, ComponentId);
            ParentNode = FindLocalNode(Blueprint, NewParentId);
            bApplied = bApplied && Node != nullptr && ParentNode != nullptr && ParentNode->GetChildNodes().Contains(Node);
            Changed->SetStringField(TEXT("component_id"), ComponentId);
            Changed->SetStringField(TEXT("parent_id"), NewParentId);
        }
        else if (Operation == TEXT("set_root"))
        {
            bApplied = Subsystem->MakeNewSceneRoot(Handles.Context, Handle, Blueprint);
            Node = FindLocalNode(Blueprint, ComponentId);
            bApplied = bApplied && Node != nullptr && Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Node);
            Changed->SetStringField(TEXT("component_id"), ComponentId);
            Changed->SetBoolField(TEXT("root"), true);
        }
        else
        {
            Node->ComponentTemplate->Modify();
            bApplied = UnrealMCP::PropertyCodec::Set(
                Node->ComponentTemplate, PropertyName, Arguments->Values.FindRef(TEXT("value")), Changed, OutError);
            if (bApplied)
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                Node = FindLocalNode(Blueprint, ComponentId);
                FProperty* LiveProperty = Node != nullptr && Node->ComponentTemplate != nullptr
                    ? Node->ComponentTemplate->GetClass()->FindPropertyByName(FName(*PropertyName)) : nullptr;
                bApplied = Node != nullptr && Node->ComponentTemplate != nullptr && LiveProperty != nullptr;
                if (bApplied) Changed = UnrealMCP::PropertyCodec::Encode(Node->ComponentTemplate, LiveProperty);
            }
            if (Changed.IsValid()) Changed->SetStringField(TEXT("component_id"), ComponentId);
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_component"), TEXT("Unreal rejected the component edit without a committed change")};
        return false;
    }

    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Changed,
        Operation == TEXT("add") ? TArray<FString>{ComponentId} : TArray<FString>{});
    return true;
}

bool FUnrealMCPBlueprintMutator::DefaultEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!RequireMutationPreconditions(*Arguments, OutError)) return false;
    if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("property_name"), TEXT("value")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_default_edit accepts only operation metadata, property_name, and value")};
        return false;
    }
    FString RawAsset;
    FString PropertyName;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAsset)
        || !Arguments->TryGetStringField(TEXT("property_name"), PropertyName) || !Arguments->HasField(TEXT("value")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_default_edit requires asset_path, property_name, and value")};
        return false;
    }
    const TSharedRef<FJsonObject> AssetOnly = MakeShared<FJsonObject>();
    AssetOnly->SetStringField(TEXT("asset_path"), RawAsset);
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*AssetOnly, Blueprint, ObjectPath, PackageName, OutError)
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError))
    {
        return false;
    }
    UObject* Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    if (Defaults == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The generated-class default object is unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }
    TSharedPtr<FJsonObject> Changed;
    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP Blueprint default edit")));
        Blueprint->Modify();
        Defaults->SetFlags(RF_Transactional);
        Defaults->Modify();
        bApplied = UnrealMCP::PropertyCodec::Set(
            Defaults, PropertyName, Arguments->Values.FindRef(TEXT("value")), Changed, OutError);
        if (bApplied)
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
            FProperty* LiveProperty = Defaults != nullptr ? Defaults->GetClass()->FindPropertyByName(FName(*PropertyName)) : nullptr;
            bApplied = Defaults != nullptr && LiveProperty != nullptr;
            if (bApplied) Changed = UnrealMCP::PropertyCodec::Encode(Defaults, LiveProperty);
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, TEXT("set_property"), Changed);
    return true;
}
