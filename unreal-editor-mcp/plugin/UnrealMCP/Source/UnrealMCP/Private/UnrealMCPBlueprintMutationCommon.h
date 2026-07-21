#pragma once

#include "UnrealMCPBlueprintMutator.h"

#include "UnrealMCPBlueprintReferenceScanner.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "K2Node.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "SubobjectDataSubsystem.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealMCP::BlueprintMutationPrivate
{
static bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
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

static FString ObjectPathForPackage(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

static bool NormalizePackagePath(const FString& Input, FString& OutPackageName)
{
    OutPackageName = Input;
    return Input.StartsWith(TEXT("/")) && !Input.StartsWith(TEXT("//")) && !Input.EndsWith(TEXT("/"))
        && !Input.Contains(TEXT("..")) && !Input.Contains(TEXT("\\")) && !Input.Contains(TEXT("."))
        && Input.Len() <= 512 && FPackageName::IsValidLongPackageName(Input, true)
        && !FPackageName::GetLongPackageAssetName(Input).IsEmpty();
}

static bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath, FString& OutPackageName)
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

static bool PathContainsSymlink(const FString& Root, const FString& Candidate)
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

static bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError)
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

static bool ExistingWritableDirectory(const FString& Filename, FString& OutDirectory)
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

static bool ValidateWritableTarget(const FString& PackageName, FString& OutFilename, FUnrealMCPError& OutError)
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

static bool ResolveParent(const FString& Path, UClass*& OutClass, FUnrealMCPError& OutError)
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

static FString CompileState(EBlueprintStatus Status)
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

static FString SeverityName(EMessageSeverity::Type Severity)
{
    if (Severity == EMessageSeverity::Error) return TEXT("error");
    if (Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning) return TEXT("warning");
    return TEXT("note");
}

static void AddDiagnostics(const FCompilerResultsLog& Log, const TSharedRef<FJsonObject>& Result)
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

static bool ResolveMutableBlueprint(
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

static bool ReadSnapshot(
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

static TSharedRef<FJsonObject> BuildResult(
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

static bool PackageAlreadyExists(const FString& PackageName)
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

static void CleanupFailedCreation(UPackage* Package, UBlueprint* Blueprint, const FString& Filename, bool bPublished)
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

static bool ValidateExpectedSnapshot(
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

static bool RequireMutationPreconditions(const FJsonObject& Arguments, FUnrealMCPError& OutError)
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

static bool IsLegalComponentName(const FString& Name)
{
    return !Name.IsEmpty() && Name.Len() <= 128 && !FName(*Name).IsNone() && FName(*Name).IsValidXName();
}

static USCS_Node* FindLocalNode(UBlueprint* Blueprint, const FString& Identity)
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

static USCS_Node* FindLocalNodeByName(UBlueprint* Blueprint, const FString& Name)
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

static bool GatherComponentHandles(UBlueprint* Blueprint, USubobjectDataSubsystem* Subsystem, FComponentHandles& Out, FUnrealMCPError& OutError)
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

static bool ResolveComponentClass(const FString& Path, UClass*& OutClass, FUnrealMCPError& OutError)
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

static TSharedRef<FJsonObject> BuildEditResult(
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

static bool RestoreFailedTransaction(FUnrealMCPError& OutError)
{
    if (GEditor != nullptr && GEditor->UndoTransaction()) return true;
    OutError = {TEXT("internal_error"), TEXT("An unexpected mutation result could not be restored through Undo")};
    return false;
}

static FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

static FBPVariableDescription* FindLocalMember(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (GuidString(Variable.VarGuid) == Identity) return &Variable;
    }
    return nullptr;
}

static FBPVariableDescription* FindLocalMemberByName(UBlueprint* Blueprint, const FString& Name)
{
    if (Blueprint == nullptr) return nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName.ToString() == Name) return &Variable;
    }
    return nullptr;
}

static bool ValidateMemberName(UBlueprint* Blueprint, const FString& Name, const FName ExistingName, FUnrealMCPError& OutError)
{
    if (Name.IsEmpty() || Name.Len() > 128 || FName(*Name).IsNone() || !FName(*Name).IsValidXName())
    {
        OutError = {TEXT("invalid_member"), TEXT("The member name is not one legal bounded Blueprint name")};
        return false;
    }
    FKismetNameValidator Validator(Blueprint, ExistingName);
    if (Validator.IsValid(Name) != EValidatorResult::Ok)
    {
        OutError = {TEXT("invalid_member"), TEXT("The member name collides with an inherited or cross-kind Blueprint member")};
        return false;
    }
    return true;
}


static UEdGraph* FindLocalFunction(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == Identity) return Graph;
    }
    return nullptr;
}

static UK2Node_FunctionEntry* FindFunctionEntry(UEdGraph* Graph)
{
    return Graph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)) : nullptr;
}

static bool IsUserOwnedFunction(UBlueprint* Blueprint, UEdGraph* Graph)
{
    if (Blueprint == nullptr || Graph == nullptr || !Blueprint->FunctionGraphs.Contains(Graph)) return false;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Graphs.Contains(Graph)) return false;
    }
    UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
    return Entry != nullptr && Entry->IsEditable();
}


static UEdGraph* FindLocalMacro(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == Identity) return Graph;
    }
    return nullptr;
}

static UEdGraph* FindLocalEventGraph(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == Identity
            && FBlueprintEditorUtils::IsEventGraph(Graph)) return Graph;
    }
    return nullptr;
}

static UK2Node_CustomEvent* FindLocalCustomEvent(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph == nullptr || !FBlueprintEditorUtils::IsEventGraph(Graph)) continue;
        TArray<UK2Node_CustomEvent*> Events;
        Graph->GetNodesOfClass(Events);
        for (UK2Node_CustomEvent* Event : Events)
        {
            if (Event != nullptr && GuidString(Event->NodeGuid) == Identity) return Event;
        }
    }
    return nullptr;
}




}
