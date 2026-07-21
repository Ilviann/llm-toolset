#include "UnrealMCPBlueprintMutator.h"

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
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
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

FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

FBPVariableDescription* FindLocalMember(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (GuidString(Variable.VarGuid) == Identity) return &Variable;
    }
    return nullptr;
}

FBPVariableDescription* FindLocalMemberByName(UBlueprint* Blueprint, const FString& Name)
{
    if (Blueprint == nullptr) return nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName.ToString() == Name) return &Variable;
    }
    return nullptr;
}

bool ValidateMemberName(UBlueprint* Blueprint, const FString& Name, const FName ExistingName, FUnrealMCPError& OutError)
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

TSharedRef<FJsonObject> MemberReferences(UBlueprint* Blueprint, const FName VariableName)
{
    TArray<UK2Node*> Nodes;
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph == nullptr) continue;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            UK2Node* Node = Cast<UK2Node>(GraphNode);
            if (Node != nullptr && Node->ReferencesVariable(VariableName, nullptr)) Nodes.Add(Node);
        }
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsVariableUsed(Blueprint, VariableName);
    Nodes.Sort([](const UK2Node& Left, const UK2Node& Right)
    {
        const FString LeftGraph = Left.GetGraph() != nullptr ? GuidString(Left.GetGraph()->GraphGuid) : FString();
        const FString RightGraph = Right.GetGraph() != nullptr ? GuidString(Right.GetGraph()->GraphGuid) : FString();
        return LeftGraph + GuidString(Left.NodeGuid) < RightGraph + GuidString(Right.NodeGuid);
    });
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("referenced"), bReferenced);
    Summary->SetNumberField(TEXT("reference_count"), Nodes.Num());
    Summary->SetBoolField(TEXT("unresolved_references"), bReferenced && Nodes.IsEmpty());
    Summary->SetBoolField(TEXT("references_truncated"), Nodes.Num() > UnrealMCP::MaxVariableReferences);
    TArray<TSharedPtr<FJsonValue>> References;
    for (int32 Index = 0; Index < FMath::Min(Nodes.Num(), UnrealMCP::MaxVariableReferences); ++Index)
    {
        UK2Node* Node = Nodes[Index];
        if (Node == nullptr) continue;
        const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
        Reference->SetStringField(TEXT("graph_id"), Node->GetGraph() != nullptr ? GuidString(Node->GetGraph()->GraphGuid) : FString());
        Reference->SetStringField(TEXT("node_id"), GuidString(Node->NodeGuid));
        Reference->SetStringField(TEXT("node_class"), Node->GetClass()->GetPathName());
        Reference->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Summary->SetArrayField(TEXT("references"), References);
    return Summary;
}

UEdGraph* FindLocalFunction(UBlueprint* Blueprint, const FString& Identity)
{
    if (Blueprint == nullptr || Identity.Len() != 32) return nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == Identity) return Graph;
    }
    return nullptr;
}

UK2Node_FunctionEntry* FindFunctionEntry(UEdGraph* Graph)
{
    return Graph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)) : nullptr;
}

bool IsUserOwnedFunction(UBlueprint* Blueprint, UEdGraph* Graph)
{
    if (Blueprint == nullptr || Graph == nullptr || !Blueprint->FunctionGraphs.Contains(Graph)) return false;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Graphs.Contains(Graph)) return false;
    }
    UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
    return Entry != nullptr && Entry->IsEditable();
}

TSharedRef<FJsonObject> FunctionReferences(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
    TArray<UK2Node*> Nodes;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr) continue;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            UK2Node* Node = Cast<UK2Node>(GraphNode);
            if (Node != nullptr && !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>()
                && Node->ReferencesFunction(FunctionGraph->GetFName(), Blueprint->SkeletonGeneratedClass)) Nodes.Add(Node);
        }
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsFunctionUsed(Blueprint, FunctionGraph->GetFName());
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("referenced"), bReferenced);
    Summary->SetNumberField(TEXT("reference_count"), Nodes.Num());
    Summary->SetBoolField(TEXT("unresolved_references"), bReferenced && Nodes.IsEmpty());
    Summary->SetBoolField(TEXT("references_truncated"), Nodes.Num() > UnrealMCP::MaxVariableReferences);
    TArray<TSharedPtr<FJsonValue>> References;
    for (int32 Index = 0; Index < FMath::Min(Nodes.Num(), UnrealMCP::MaxVariableReferences); ++Index)
    {
        UK2Node* Node = Nodes[Index];
        const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
        Reference->SetStringField(TEXT("graph_id"), Node->GetGraph() != nullptr ? GuidString(Node->GetGraph()->GraphGuid) : FString());
        Reference->SetStringField(TEXT("node_id"), GuidString(Node->NodeGuid));
        Reference->SetStringField(TEXT("node_class"), Node->GetClass()->GetPathName());
        Reference->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Summary->SetArrayField(TEXT("references"), References);
    return Summary;
}

TSharedRef<FJsonObject> LocalReferences(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FName VariableName)
{
    TArray<UK2Node*> Nodes;
    const UStruct* Scope = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName()) : nullptr;
    for (UEdGraphNode* GraphNode : FunctionGraph->Nodes)
    {
        UK2Node* Node = Cast<UK2Node>(GraphNode);
        if (Node != nullptr && Node->ReferencesVariable(VariableName, Scope)) Nodes.Add(Node);
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsVariableUsed(Blueprint, VariableName, FunctionGraph);
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("referenced"), bReferenced);
    Summary->SetNumberField(TEXT("reference_count"), Nodes.Num());
    Summary->SetBoolField(TEXT("unresolved_references"), bReferenced && Nodes.IsEmpty());
    Summary->SetBoolField(TEXT("references_truncated"), Nodes.Num() > UnrealMCP::MaxVariableReferences);
    TArray<TSharedPtr<FJsonValue>> References;
    for (int32 Index = 0; Index < FMath::Min(Nodes.Num(), UnrealMCP::MaxVariableReferences); ++Index)
    {
        UK2Node* Node = Nodes[Index];
        const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
        Reference->SetStringField(TEXT("graph_id"), GuidString(FunctionGraph->GraphGuid));
        Reference->SetStringField(TEXT("node_id"), GuidString(Node->NodeGuid));
        Reference->SetStringField(TEXT("node_class"), Node->GetClass()->GetPathName());
        Reference->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Summary->SetArrayField(TEXT("references"), References);
    return Summary;
}

struct FFunctionParameterSpec
{
    FName Name;
    FString Direction;
    FEdGraphPinType Type;
    FString DefaultValue;
};

struct FFunctionSignatureSpec
{
    FString Access;
    bool bPure = false;
    bool bConst = false;
    TArray<FFunctionParameterSpec> Parameters;
};

bool DecodeFunctionSignature(
    const TSharedPtr<FJsonObject>& Signature,
    FFunctionSignatureSpec& Out,
    FUnrealMCPError& OutError)
{
    if (!Signature.IsValid() || !HasOnlyFields(*Signature, {TEXT("access"), TEXT("pure"), TEXT("const"), TEXT("parameters")})
        || !Signature->TryGetStringField(TEXT("access"), Out.Access)
        || !Signature->TryGetBoolField(TEXT("pure"), Out.bPure)
        || !Signature->TryGetBoolField(TEXT("const"), Out.bConst)
        || (Out.Access != TEXT("public") && Out.Access != TEXT("protected") && Out.Access != TEXT("private")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("signature requires exact access, pure, const, and parameters fields")};
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
    if (!Signature->TryGetArrayField(TEXT("parameters"), Parameters) || Parameters == nullptr || Parameters->Num() > 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("signature parameters must be one bounded array")};
        return false;
    }
    TSet<FName> Names;
    for (const TSharedPtr<FJsonValue>& Value : *Parameters)
    {
        const TSharedPtr<FJsonObject>* Parameter = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Parameter) || Parameter == nullptr
            || !HasOnlyFields(**Parameter, {TEXT("name"), TEXT("direction"), TEXT("type"), TEXT("default")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Each function parameter must be one exact object")};
            return false;
        }
        FString Name;
        FFunctionParameterSpec Spec;
        const TSharedPtr<FJsonObject>* Type = nullptr;
        if (!(*Parameter)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 128
            || FName(*Name).IsNone() || !FName(*Name).IsValidXName() || Names.Contains(FName(*Name))
            || !(*Parameter)->TryGetStringField(TEXT("direction"), Spec.Direction)
            || (Spec.Direction != TEXT("input") && Spec.Direction != TEXT("output"))
            || !(*Parameter)->TryGetObjectField(TEXT("type"), Type) || Type == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*Type, Spec.Type, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Function parameter names and directions must be legal and unique")};
            return false;
        }
        Spec.Name = FName(*Name);
        if (Spec.Direction == TEXT("output") && (Spec.Type.bIsReference || Spec.Type.bIsConst || (*Parameter)->HasField(TEXT("default"))))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Output parameters cannot be reference, const, or have defaults")};
            return false;
        }
        if (Spec.Direction == TEXT("input") && Spec.Type.bIsReference && (*Parameter)->HasField(TEXT("default")))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Reference input parameters cannot have defaults")};
            return false;
        }
        if ((*Parameter)->HasField(TEXT("default")))
        {
            const TSharedPtr<FJsonObject>* Default = nullptr;
            if (!(*Parameter)->TryGetObjectField(TEXT("default"), Default) || Default == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Spec.Type, *Default, Spec.DefaultValue, OutError)) return false;
        }
        Names.Add(Spec.Name);
        Out.Parameters.Add(MoveTemp(Spec));
    }
    return true;
}

bool ValidateFunctionMetadata(const TSharedPtr<FJsonObject>& Metadata, FUnrealMCPError& OutError)
{
    if (!Metadata.IsValid() || Metadata->Values.IsEmpty()
        || !HasOnlyFields(*Metadata, {TEXT("category"), TEXT("tooltip"), TEXT("keywords"), TEXT("call_in_editor")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("function metadata must contain supported exact fields")};
        return false;
    }
    FString Text;
    bool Flag = false;
    if ((Metadata->HasField(TEXT("category")) && (!Metadata->TryGetStringField(TEXT("category"), Text) || Text.Len() > 128))
        || (Metadata->HasField(TEXT("tooltip")) && (!Metadata->TryGetStringField(TEXT("tooltip"), Text) || Text.Len() > 512))
        || (Metadata->HasField(TEXT("keywords")) && (!Metadata->TryGetStringField(TEXT("keywords"), Text) || Text.Len() > 256))
        || (Metadata->HasField(TEXT("call_in_editor")) && !Metadata->TryGetBoolField(TEXT("call_in_editor"), Flag)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("function metadata contains an invalid bounded value")};
        return false;
    }
    return true;
}

void ApplyFunctionMetadata(UK2Node_FunctionEntry* Entry, const TSharedPtr<FJsonObject>& Metadata)
{
    FString Text;
    bool Flag = false;
    Entry->Modify();
    if (Metadata->TryGetStringField(TEXT("category"), Text)) Entry->MetaData.Category = FText::FromString(Text);
    if (Metadata->TryGetStringField(TEXT("tooltip"), Text)) Entry->MetaData.ToolTip = FText::FromString(Text);
    if (Metadata->TryGetStringField(TEXT("keywords"), Text)) Entry->MetaData.Keywords = FText::FromString(Text);
    if (Metadata->TryGetBoolField(TEXT("call_in_editor"), Flag)) Entry->MetaData.bCallInEditor = Flag;
}

bool ApplyFunctionSignature(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FFunctionSignatureSpec& Signature,
    FUnrealMCPError& OutError)
{
    UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
    if (Entry == nullptr)
    {
        OutError = {TEXT("invalid_member"), TEXT("The function graph has no required entry node")};
        return false;
    }
    UK2Node_FunctionResult* PrimaryResult = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
    if (PrimaryResult == nullptr)
    {
        OutError = {TEXT("invalid_member"), TEXT("Unreal could not create the required function result node")};
        return false;
    }
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Results);
    Entry->Modify();
    for (UK2Node_FunctionResult* Result : Results) Result->Modify();
    for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Entry->UserDefinedPins)) Entry->RemoveUserDefinedPin(Pin);
    for (UK2Node_FunctionResult* Result : Results)
    {
        for (const TSharedPtr<FUserPinInfo>& Pin : TArray<TSharedPtr<FUserPinInfo>>(Result->UserDefinedPins)) Result->RemoveUserDefinedPin(Pin);
    }
    int32 Flags = Entry->GetExtraFlags();
    Flags &= ~(FUNC_Public | FUNC_Protected | FUNC_Private | FUNC_BlueprintPure | FUNC_Const);
    Flags |= Signature.Access == TEXT("private") ? FUNC_Private : Signature.Access == TEXT("protected") ? FUNC_Protected : FUNC_Public;
    if (Signature.bPure) Flags |= FUNC_BlueprintPure;
    if (Signature.bConst) Flags |= FUNC_Const;
    Entry->SetExtraFlags(Flags);
    for (const FFunctionParameterSpec& Parameter : Signature.Parameters)
    {
        if (Parameter.Direction == TEXT("input"))
        {
            FText Reason;
            if (!Entry->CanCreateUserDefinedPin(Parameter.Type, EGPD_Output, Reason)
                || Entry->CreateUserDefinedPin(Parameter.Name, Parameter.Type, EGPD_Output, false) == nullptr)
            {
                OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live function entry rejected a parameter type") : Reason.ToString().Left(512)};
                return false;
            }
            if (!Parameter.DefaultValue.IsEmpty() && !Entry->UserDefinedPins.IsEmpty()
                && !Entry->ModifyUserDefinedPinDefaultValue(Entry->UserDefinedPins.Last(), Parameter.DefaultValue))
            {
                OutError = {TEXT("invalid_argument"), TEXT("The live function entry rejected a parameter default")};
                return false;
            }
        }
        else
        {
            for (UK2Node_FunctionResult* Result : Results)
            {
                FText Reason;
                if (!Result->CanCreateUserDefinedPin(Parameter.Type, EGPD_Input, Reason)
                    || Result->CreateUserDefinedPin(Parameter.Name, Parameter.Type, EGPD_Input, false) == nullptr)
                {
                    OutError = {TEXT("unsupported_type"), Reason.IsEmpty() ? TEXT("The live function result rejected a parameter type") : Reason.ToString().Left(512)};
                    return false;
                }
            }
        }
    }
    Entry->ReconstructNode();
    for (UK2Node_FunctionResult* Result : Results) Result->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

FBPVariableDescription* FindLocalById(UK2Node_FunctionEntry* Entry, const FString& Identity)
{
    if (Entry == nullptr || Identity.Len() != 32) return nullptr;
    for (FBPVariableDescription& Variable : Entry->LocalVariables)
    {
        if (GuidString(Variable.VarGuid) == Identity) return &Variable;
    }
    return nullptr;
}

FBPVariableDescription* FindLocalByName(UK2Node_FunctionEntry* Entry, const FString& Name)
{
    if (Entry == nullptr) return nullptr;
    for (FBPVariableDescription& Variable : Entry->LocalVariables)
    {
        if (Variable.VarName.ToString() == Name) return &Variable;
    }
    return nullptr;
}

bool ValidateLocalName(UBlueprint* Blueprint, UK2Node_FunctionEntry* Entry, const FString& Name, const FName Existing, FUnrealMCPError& OutError)
{
    if (Name.IsEmpty() || Name.Len() > 128 || FName(*Name).IsNone() || !FName(*Name).IsValidXName())
    {
        OutError = {TEXT("invalid_member"), TEXT("The local-variable name is not one legal bounded Blueprint name")};
        return false;
    }
    for (const FBPVariableDescription& Local : Entry->LocalVariables)
    {
        if (Local.VarName == FName(*Name) && Local.VarName != Existing)
        {
            OutError = {TEXT("invalid_member"), TEXT("The local-variable name collides in its function scope")};
            return false;
        }
    }
    for (const TSharedPtr<FUserPinInfo>& Parameter : Entry->UserDefinedPins)
    {
        if (Parameter.IsValid() && Parameter->PinName == FName(*Name))
        {
            OutError = {TEXT("invalid_member"), TEXT("The local-variable name collides with a function parameter")};
            return false;
        }
    }
    return ValidateMemberName(Blueprint, Name, Existing, OutError);
}

void SetPropertyFlag(uint64& Flags, EPropertyFlags Flag, bool bEnabled)
{
    if (bEnabled) Flags |= static_cast<uint64>(Flag);
    else Flags &= ~static_cast<uint64>(Flag);
}

bool ReadMetadataBool(const FJsonObject& Metadata, const TCHAR* Name, bool& InOut, FUnrealMCPError& OutError)
{
    if (!Metadata.HasField(Name)) return true;
    if (!Metadata.TryGetBoolField(Name, InOut))
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("metadata.%s must be boolean"), Name)};
        return false;
    }
    return true;
}

bool ValidateAndApplyMetadata(
    UBlueprint* Blueprint,
    FBPVariableDescription& Variable,
    const TSharedPtr<FJsonObject>& Metadata,
    bool bApply,
    FUnrealMCPError& OutError)
{
    if (!Metadata.IsValid() || Metadata->Values.IsEmpty()
        || !HasOnlyFields(*Metadata, {TEXT("category"), TEXT("tooltip"), TEXT("instance_editable"), TEXT("blueprint_visible"),
            TEXT("blueprint_read_only"), TEXT("expose_on_spawn"), TEXT("private"), TEXT("save_game"), TEXT("advanced_display"), TEXT("replication"),
            TEXT("rep_notify_function"), TEXT("replication_condition")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("metadata must contain one or more supported exact fields")};
        return false;
    }
    FString Category = Variable.Category.ToString();
    FString Tooltip = Variable.HasMetaData(TEXT("tooltip")) ? Variable.GetMetaData(TEXT("tooltip")) : FString();
    FString Replication;
    FString RepNotifyFunction = Variable.RepNotifyFunc.ToString();
    FString ReplicationCondition = StaticEnum<ELifetimeCondition>()->GetNameStringByValue(Variable.ReplicationCondition);
    bool bInstanceEditable = (Variable.PropertyFlags & CPF_Edit) != 0 && (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0;
    bool bBlueprintVisible = (Variable.PropertyFlags & CPF_BlueprintVisible) != 0;
    bool bBlueprintReadOnly = (Variable.PropertyFlags & CPF_BlueprintReadOnly) != 0;
    bool bExposeOnSpawn = Variable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
    bool bPrivate = Variable.HasMetaData(FBlueprintMetadata::MD_Private);
    bool bSaveGame = (Variable.PropertyFlags & CPF_SaveGame) != 0;
    bool bAdvancedDisplay = (Variable.PropertyFlags & CPF_AdvancedDisplay) != 0;
    if ((Metadata->HasField(TEXT("category")) && (!Metadata->TryGetStringField(TEXT("category"), Category) || Category.Len() > 128))
        || (Metadata->HasField(TEXT("tooltip")) && (!Metadata->TryGetStringField(TEXT("tooltip"), Tooltip) || Tooltip.Len() > 512))
        || !ReadMetadataBool(*Metadata, TEXT("instance_editable"), bInstanceEditable, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("blueprint_visible"), bBlueprintVisible, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("blueprint_read_only"), bBlueprintReadOnly, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("expose_on_spawn"), bExposeOnSpawn, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("private"), bPrivate, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("save_game"), bSaveGame, OutError)
        || !ReadMetadataBool(*Metadata, TEXT("advanced_display"), bAdvancedDisplay, OutError)
        || (Metadata->HasField(TEXT("replication")) && !Metadata->TryGetStringField(TEXT("replication"), Replication))
        || (Metadata->HasField(TEXT("rep_notify_function"))
            && (!Metadata->TryGetStringField(TEXT("rep_notify_function"), RepNotifyFunction) || RepNotifyFunction.IsEmpty() || RepNotifyFunction.Len() > 128))
        || (Metadata->HasField(TEXT("replication_condition"))
            && (!Metadata->TryGetStringField(TEXT("replication_condition"), ReplicationCondition) || ReplicationCondition.IsEmpty() || ReplicationCondition.Len() > 64)))
    {
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_argument"), TEXT("metadata contains an invalid bounded value")};
        return false;
    }
    if (bExposeOnSpawn && (!bInstanceEditable || !bBlueprintVisible || bPrivate))
    {
        OutError = {TEXT("invalid_member"), TEXT("Expose-on-spawn requires a visible, non-private, instance-editable variable")};
        return false;
    }
    const FString FinalReplication = !Replication.IsEmpty() ? Replication
        : !Variable.RepNotifyFunc.IsNone() ? TEXT("rep_notify")
        : (Variable.PropertyFlags & CPF_Net) != 0 ? TEXT("replicated") : TEXT("none");
    if (FinalReplication != TEXT("none") && FinalReplication != TEXT("replicated") && FinalReplication != TEXT("rep_notify"))
    {
        OutError = {TEXT("invalid_argument"), TEXT("replication must be none, replicated, or rep_notify")};
        return false;
    }
    if (FinalReplication != TEXT("rep_notify") && Metadata->HasField(TEXT("rep_notify_function")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("rep_notify_function is accepted only with rep_notify replication")};
        return false;
    }
    if (FinalReplication != TEXT("none") && (Variable.VarType.IsSet() || Variable.VarType.IsMap()))
    {
        OutError = {TEXT("invalid_member"), TEXT("The live Blueprint capability does not support replicated set or map variables")};
        return false;
    }
    const int64 ConditionValue = StaticEnum<ELifetimeCondition>()->GetValueByNameString(ReplicationCondition);
    if (ConditionValue == INDEX_NONE || ConditionValue < 0 || ConditionValue > MAX_uint8)
    {
        OutError = {TEXT("invalid_argument"), TEXT("replication_condition is not one exact live lifetime condition")};
        return false;
    }
    if (FinalReplication == TEXT("none") && Metadata->HasField(TEXT("replication_condition")) && ConditionValue != COND_None)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Non-replicated variables require condition COND_None")};
        return false;
    }
    if (FinalReplication == TEXT("rep_notify"))
    {
        UEdGraph* NotifyGraph = nullptr;
        if (Blueprint != nullptr)
        {
            for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
            {
                if (Candidate != nullptr && Candidate->GetName() == RepNotifyFunction) { NotifyGraph = Candidate; break; }
            }
        }
        UK2Node_FunctionEntry* NotifyEntry = FindFunctionEntry(NotifyGraph);
        TArray<UK2Node_FunctionResult*> NotifyResults;
        if (NotifyGraph != nullptr) NotifyGraph->GetNodesOfClass(NotifyResults);
        bool bHasOutputs = false;
        for (UK2Node_FunctionResult* Result : NotifyResults) bHasOutputs |= Result != nullptr && !Result->UserDefinedPins.IsEmpty();
        if (NotifyGraph == nullptr || NotifyEntry == nullptr || !IsUserOwnedFunction(Blueprint, NotifyGraph)
            || !NotifyEntry->UserDefinedPins.IsEmpty() || bHasOutputs || (NotifyEntry->GetFunctionFlags() & FUNC_BlueprintPure) != 0)
        {
            OutError = {TEXT("invalid_member"), TEXT("rep_notify_function must identify an impure user-owned function with no parameters or return values")};
            return false;
        }
    }
    if (!bApply) return true;
    Variable.Category = FText::FromString(Category);
    if (Tooltip.IsEmpty()) Variable.RemoveMetaData(TEXT("tooltip")); else Variable.SetMetaData(TEXT("tooltip"), Tooltip);
    SetPropertyFlag(Variable.PropertyFlags, CPF_Edit, true);
    SetPropertyFlag(Variable.PropertyFlags, CPF_DisableEditOnInstance, !bInstanceEditable);
    SetPropertyFlag(Variable.PropertyFlags, CPF_BlueprintVisible, bBlueprintVisible);
    SetPropertyFlag(Variable.PropertyFlags, CPF_BlueprintReadOnly, bBlueprintReadOnly);
    SetPropertyFlag(Variable.PropertyFlags, CPF_SaveGame, bSaveGame);
    SetPropertyFlag(Variable.PropertyFlags, CPF_AdvancedDisplay, bAdvancedDisplay);
    if (bExposeOnSpawn) Variable.SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
    else Variable.RemoveMetaData(FBlueprintMetadata::MD_ExposeOnSpawn);
    if (bPrivate) Variable.SetMetaData(FBlueprintMetadata::MD_Private, TEXT("true"));
    else Variable.RemoveMetaData(FBlueprintMetadata::MD_Private);
    if (!Replication.IsEmpty() || Metadata->HasField(TEXT("rep_notify_function")) || Metadata->HasField(TEXT("replication_condition")))
    {
        SetPropertyFlag(Variable.PropertyFlags, CPF_Net, FinalReplication != TEXT("none"));
        SetPropertyFlag(Variable.PropertyFlags, CPF_RepNotify, FinalReplication == TEXT("rep_notify"));
        Variable.RepNotifyFunc = FinalReplication == TEXT("rep_notify") ? FName(*RepNotifyFunction) : NAME_None;
        Variable.ReplicationCondition = FinalReplication == TEXT("none") ? COND_None : static_cast<ELifetimeCondition>(ConditionValue);
    }
    return true;
}

bool ReadInspectedMember(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& ObjectPath,
    const FString& MemberId,
    TSharedPtr<FJsonObject>& OutMember,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), ObjectPath);
    Arguments->SetStringField(TEXT("member_id"), MemberId);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    Arguments->SetNumberField(TEXT("page_size"), 1);
    TSharedPtr<FJsonObject> Inspection;
    if (!Inspector.Execute(Arguments, Inspection, OutError) || !Inspection.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Inspection->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1)
    {
        OutError = {TEXT("internal_error"), TEXT("Member read-back did not return one exact variable record")};
        return false;
    }
    const TSharedPtr<FJsonObject>* Record = nullptr;
    if (!(*Records)[0].IsValid() || !(*Records)[0]->TryGetObject(Record) || Record == nullptr || !Record->IsValid())
    {
        OutError = {TEXT("internal_error"), TEXT("Member read-back returned an invalid variable record")};
        return false;
    }
    OutMember = *Record;
    return true;
}


bool ReadInspectedScopedRecord(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& ObjectPath,
    const TCHAR* FilterName,
    const FString& Identity,
    const TCHAR* Section,
    TSharedPtr<FJsonObject>& OutRecord,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), ObjectPath);
    Arguments->SetStringField(FilterName, Identity);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(Section)});
    Arguments->SetNumberField(TEXT("page_size"), 1);
    TSharedPtr<FJsonObject> Inspection;
    if (!Inspector.Execute(Arguments, Inspection, OutError) || !Inspection.IsValid()) return false;
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Inspection->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1)
    {
        OutError = {TEXT("internal_error"), TEXT("Scoped member read-back did not return one exact record")};
        return false;
    }
    const TSharedPtr<FJsonObject>* Record = nullptr;
    if (!(*Records)[0].IsValid() || !(*Records)[0]->TryGetObject(Record) || Record == nullptr || !Record->IsValid())
    {
        OutError = {TEXT("internal_error"), TEXT("Scoped member read-back returned an invalid record")};
        return false;
    }
    OutRecord = *Record;
    return true;
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
    if (Command == TEXT("blueprint_member_edit")) return MemberEdit(Arguments, OutResult, OutError);
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

bool FUnrealMCPBlueprintMutator::MemberEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!RequireMutationPreconditions(*Arguments, OutError)) return false;
    FString Target;
    if (Arguments->TryGetStringField(TEXT("target"), Target))
    {
        if (Target == TEXT("function")) return FunctionEdit(Arguments, OutResult, OutError);
        if (Target == TEXT("local_variable")) return LocalVariableEdit(Arguments, OutResult, OutError);
        OutError = {TEXT("invalid_argument"), TEXT("target must be function or local_variable when supplied")};
        return false;
    }
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_member_edit requires one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("type"), TEXT("default"), TEXT("metadata")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("member_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("member_id"), TEXT("field"), TEXT("type"), TEXT("default"), TEXT("metadata"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("member_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown member edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The member edit contains a field not accepted by its operation")};
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
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    if (Blueprint->BlueprintType != BPTYPE_Normal)
    {
        OutError = {TEXT("unsupported_type"), TEXT("This live Blueprint kind does not support member-variable editing")};
        return false;
    }

    FString MemberId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    FBPVariableDescription* Variable = nullptr;
    TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
    FEdGraphPinType NewType;
    FString NewDefault;
    const TSharedPtr<FJsonObject>* TypeObject = nullptr;
    const TSharedPtr<FJsonObject>* DefaultObject = nullptr;
    const TSharedPtr<FJsonObject>* MetadataObject = nullptr;

    if (Operation == TEXT("add"))
    {
        if (!Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateMemberName(Blueprint, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
        if (NewType.bIsReference || NewType.bIsConst)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Member-variable types cannot be reference or const")};
            return false;
        }
        if (Arguments->HasField(TEXT("default")))
        {
            if (!Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(NewType, *DefaultObject, NewDefault, OutError)) return false;
        }
        if (Arguments->HasField(TEXT("metadata")))
        {
            if (!Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr) return false;
            FBPVariableDescription Preview;
            Preview.VarType = NewType;
            Preview.PropertyFlags = CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance;
            if (!ValidateAndApplyMetadata(Blueprint, Preview, *MetadataObject, false, OutError)) return false;
        }
    }
    else
    {
        if (Operation == TEXT("update")
            && (!Arguments->TryGetStringField(TEXT("field"), Field)
                || (Field != TEXT("type") && Field != TEXT("default") && Field != TEXT("metadata"))))
        {
            OutError = {TEXT("invalid_argument"), TEXT("update requires field type, default, or metadata")};
            return false;
        }
        if (!Arguments->TryGetStringField(TEXT("member_id"), MemberId)
            || (Variable = FindLocalMember(Blueprint, MemberId)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local member identity is unavailable")};
            return false;
        }
        Name = Variable->VarName.ToString();
        References = MemberReferences(Blueprint, Variable->VarName);
        if (Operation == TEXT("remove") || (Operation == TEXT("update") && Field == TEXT("type")))
        {
            if (!Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Removal and type change require policy reject_if_referenced")};
                return false;
            }
            if (References->GetBoolField(TEXT("referenced")))
            {
                OutError = {TEXT("referenced_member"), TEXT("The member is referenced and the reject-only policy forbids this mutation")};
                OutError.Details->SetStringField(TEXT("member_id"), MemberId);
                OutError.Details->SetNumberField(TEXT("reference_count"), References->GetNumberField(TEXT("reference_count")));
                return false;
            }
        }
    }

    if (Operation == TEXT("rename"))
    {
        if (!Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
            || !ValidateMemberName(Blueprint, NewName, Variable->VarName, OutError)) return false;
    }
    else if (Operation == TEXT("update"))
    {
        if (Field == TEXT("type"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("member_id"), TEXT("field"), TEXT("type"), TEXT("policy")})
                || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
            if (NewType.bIsReference || NewType.bIsConst)
            {
                OutError = {TEXT("invalid_argument"), TEXT("Member-variable types cannot be reference or const")};
                return false;
            }
            if ((NewType.IsSet() || NewType.IsMap()) && (Variable->PropertyFlags & CPF_Net) != 0)
            {
                OutError = {TEXT("invalid_member"), TEXT("Replicated members cannot change to a live K2 set or map type")};
                return false;
            }
        }
        else if (Field == TEXT("default"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("member_id"), TEXT("field"), TEXT("default")})
                || !Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Variable->VarType, *DefaultObject, NewDefault, OutError)) return false;
        }
        else if (Field == TEXT("metadata"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"),
                    TEXT("member_id"), TEXT("field"), TEXT("metadata")})
                || !Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                || !ValidateAndApplyMetadata(Blueprint, *Variable, *MetadataObject, false, OutError)) return false;
        }
        else
        {
            OutError = {TEXT("invalid_argument"), TEXT("update field must be type, default, or metadata")};
            return false;
        }
    }
    else if (Operation == TEXT("remove")
        && (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("member_id"), TEXT("policy")})))
    {
        OutError = {TEXT("invalid_argument"), TEXT("remove accepts only member_id and reject_if_referenced policy")};
        return false;
    }

    bool bApplied = false;
    if (Operation == TEXT("rename"))
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP member rename")));
        Blueprint->Modify();
        const FName RepNotifyFunction = Variable->RepNotifyFunc;
        Variable->RepNotifyFunc = NAME_None;
        FBlueprintEditorUtils::RenameMemberVariable(Blueprint, Variable->VarName, FName(*NewName));
        Variable = FindLocalMember(Blueprint, MemberId);
        if (Variable != nullptr) Variable->RepNotifyFunc = RepNotifyFunction;
        bApplied = Variable != nullptr && Variable->VarName.ToString() == NewName;
        if (bApplied) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    else
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP member edit")));
        Blueprint->Modify();
        if (Operation == TEXT("add"))
        {
            bApplied = FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), NewType, NewDefault);
            Variable = FindLocalMemberByName(Blueprint, Name);
            bApplied = bApplied && Variable != nullptr && Variable->VarGuid.IsValid();
            if (bApplied && MetadataObject != nullptr)
            {
                bApplied = ValidateAndApplyMetadata(Blueprint, *Variable, *MetadataObject, true, OutError);
                if (bApplied) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            }
            if (bApplied) MemberId = GuidString(Variable->VarGuid);
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Variable->VarName);
            bApplied = FindLocalMember(Blueprint, MemberId) == nullptr;
        }
        else if (Field == TEXT("type"))
        {
            Variable->VarType = NewType;
            Variable->DefaultValue.Reset();
            const UClass* ObjectClass = Cast<UClass>(NewType.PinSubCategoryObject.Get());
            SetPropertyFlag(Variable->PropertyFlags, CPF_DisableEditOnTemplate,
                NewType.PinCategory == UEdGraphSchema_K2::PC_Object && ObjectClass != nullptr && ObjectClass->IsChildOf(AActor::StaticClass()));
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Variable = FindLocalMember(Blueprint, MemberId);
            bApplied = Variable != nullptr && Variable->VarType == NewType;
        }
        else if (Field == TEXT("default"))
        {
            Variable->DefaultValue = NewDefault;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Variable = FindLocalMember(Blueprint, MemberId);
            bApplied = Variable != nullptr && Variable->DefaultValue == NewDefault;
        }
        else
        {
            bApplied = ValidateAndApplyMetadata(Blueprint, *Variable, *MetadataObject, true, OutError);
            if (bApplied)
            {
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                Variable = FindLocalMember(Blueprint, MemberId);
                bApplied = Variable != nullptr;
            }
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the member edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Member;
    TSharedPtr<FJsonObject> ResultReferences = References;
    if (Operation != TEXT("remove"))
    {
        if (!ReadInspectedMember(Inspector, ObjectPath, MemberId, Member, OutError))
        {
            RestoreFailedTransaction(OutError);
            return false;
        }
        const TSharedPtr<FJsonObject>* ReadReferences = nullptr;
        if (Member->TryGetObjectField(TEXT("reference_summary"), ReadReferences) && ReadReferences != nullptr)
        {
            ResultReferences = *ReadReferences;
        }
    }
    else
    {
        Member = MakeShared<FJsonObject>();
        Member->SetStringField(TEXT("id"), MemberId);
        Member->SetStringField(TEXT("name"), Name);
        Member->SetBoolField(TEXT("removed"), true);
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Member,
        Operation == TEXT("add") ? TArray<FString>{MemberId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("member"), Member);
    OutResult->SetObjectField(TEXT("reference_summary"), ResultReferences);
    return true;
}

bool FUnrealMCPBlueprintMutator::FunctionEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    FString Operation;
    FString Target;
    if (!Arguments->TryGetStringField(TEXT("target"), Target) || Target != TEXT("function")
        || !Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Function edits require target function and one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("signature"), TEXT("metadata")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("function_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("function_id"), TEXT("field"), TEXT("signature"), TEXT("metadata"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("function_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown function edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The function edit contains a field not accepted by its operation")};
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
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    if (Blueprint->BlueprintType != BPTYPE_Normal)
    {
        OutError = {TEXT("unsupported_type"), TEXT("This live Blueprint kind does not support user function editing")};
        return false;
    }

    FString FunctionId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    UEdGraph* Graph = nullptr;
    UK2Node_FunctionEntry* Entry = nullptr;
    FFunctionSignatureSpec Signature;
    const TSharedPtr<FJsonObject>* SignatureObject = nullptr;
    const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
    TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
    References->SetBoolField(TEXT("referenced"), false);
    References->SetNumberField(TEXT("reference_count"), 0);
    References->SetBoolField(TEXT("unresolved_references"), false);
    References->SetBoolField(TEXT("references_truncated"), false);
    References->SetArrayField(TEXT("references"), {});

    if (Operation == TEXT("add"))
    {
        if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("name"), TEXT("signature"), TEXT("metadata")})
            || !Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateMemberName(Blueprint, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
            || !DecodeFunctionSignature(*SignatureObject, Signature, OutError)) return false;
        if (Arguments->HasField(TEXT("metadata"))
            && (!Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                || !ValidateFunctionMetadata(*MetadataObject, OutError))) return false;
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("function_id"), FunctionId)
            || (Graph = FindLocalFunction(Blueprint, FunctionId)) == nullptr || !IsUserOwnedFunction(Blueprint, Graph)
            || (Entry = FindFunctionEntry(Graph)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable user-owned function identity is unavailable")};
            return false;
        }
        Name = Graph->GetName();
        References = FunctionReferences(Blueprint, Graph);
        if (Operation == TEXT("rename"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("new_name")})
                || !Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
                || !ValidateMemberName(Blueprint, NewName, Graph->GetFName(), OutError)) return false;
        }
        else if (Operation == TEXT("update"))
        {
            if (!Arguments->TryGetStringField(TEXT("field"), Field))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Function update requires field signature or metadata")};
                return false;
            }
            if (Field == TEXT("signature"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("function_id"), TEXT("field"), TEXT("signature"), TEXT("policy")})
                    || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
                    || !Arguments->TryGetObjectField(TEXT("signature"), SignatureObject) || SignatureObject == nullptr
                    || !DecodeFunctionSignature(*SignatureObject, Signature, OutError)) return false;
                if (References->GetBoolField(TEXT("referenced")))
                {
                    OutError = {TEXT("referenced_member"), TEXT("The function is referenced and the reject-only policy forbids a signature change")};
                    OutError.Details->SetStringField(TEXT("function_id"), FunctionId);
                    OutError.Details->SetNumberField(TEXT("reference_count"), References->GetNumberField(TEXT("reference_count")));
                    return false;
                }
                bool bRepNotify = false;
                for (const FBPVariableDescription& Variable : Blueprint->NewVariables) bRepNotify |= Variable.RepNotifyFunc == Graph->GetFName();
                if (bRepNotify && (Signature.bPure || !Signature.Parameters.IsEmpty()))
                {
                    OutError = {TEXT("invalid_member"), TEXT("A RepNotify function must remain impure with no inputs or outputs")};
                    return false;
                }
            }
            else if (Field == TEXT("metadata"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("function_id"), TEXT("field"), TEXT("metadata")})
                    || !Arguments->TryGetObjectField(TEXT("metadata"), MetadataObject) || MetadataObject == nullptr
                    || !ValidateFunctionMetadata(*MetadataObject, OutError)) return false;
            }
            else
            {
                OutError = {TEXT("invalid_argument"), TEXT("Function update field must be signature or metadata")};
                return false;
            }
        }
        else if (Operation == TEXT("remove"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("policy")})
                || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Function removal requires policy reject_if_referenced")};
                return false;
            }
            if (References->GetBoolField(TEXT("referenced")))
            {
                OutError = {TEXT("referenced_member"), TEXT("The function is referenced and the reject-only policy forbids removal")};
                OutError.Details->SetStringField(TEXT("function_id"), FunctionId);
                OutError.Details->SetNumberField(TEXT("reference_count"), References->GetNumberField(TEXT("reference_count")));
                return false;
            }
            for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
            {
                if (Variable.RepNotifyFunc == Graph->GetFName())
                {
                    OutError = {TEXT("referenced_member"), TEXT("The function is coupled to a RepNotify member and cannot be removed")};
                    return false;
                }
            }
        }
    }

    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP function edit")));
        Blueprint->Modify();
        if (Operation == TEXT("add"))
        {
            Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            if (Graph != nullptr)
            {
                FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
                Entry = FindFunctionEntry(Graph);
                bApplied = Entry != nullptr && ApplyFunctionSignature(Blueprint, Graph, Signature, OutError);
                if (bApplied && MetadataObject != nullptr) ApplyFunctionMetadata(Entry, *MetadataObject);
                if (bApplied)
                {
                    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
                    FunctionId = GuidString(Graph->GraphGuid);
                    bApplied = FunctionId.Len() == 32;
                }
            }
        }
        else if (Operation == TEXT("rename"))
        {
            Graph->Modify();
            FBlueprintEditorUtils::RenameGraph(Graph, NewName);
            bApplied = Graph->GetName() == NewName;
            if (bApplied)
            {
                for (FBPVariableDescription& Variable : Blueprint->NewVariables)
                {
                    if (Variable.RepNotifyFunc == FName(*Name)) Variable.RepNotifyFunc = FName(*NewName);
                }
                FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            }
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
            bApplied = FindLocalFunction(Blueprint, FunctionId) == nullptr;
        }
        else if (Field == TEXT("signature"))
        {
            Graph->Modify();
            bApplied = ApplyFunctionSignature(Blueprint, Graph, Signature, OutError);
        }
        else
        {
            ApplyFunctionMetadata(Entry, *MetadataObject);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            bApplied = true;
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the function edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Function;
    if (Operation == TEXT("remove"))
    {
        Function = MakeShared<FJsonObject>();
        Function->SetStringField(TEXT("id"), FunctionId);
        Function->SetStringField(TEXT("name"), Name);
        Function->SetBoolField(TEXT("removed"), true);
    }
    else if (!ReadInspectedScopedRecord(Inspector, ObjectPath, TEXT("function_id"), FunctionId, TEXT("functions"), Function, OutError))
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
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Function,
        Operation == TEXT("add") ? TArray<FString>{FunctionId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("function"), Function);
    OutResult->SetObjectField(TEXT("reference_summary"), Operation == TEXT("remove") ? References : Function->GetObjectField(TEXT("reference_summary")));
    return true;
}

bool FUnrealMCPBlueprintMutator::LocalVariableEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    FString Target;
    FString Operation;
    FString FunctionId;
    if (!Arguments->TryGetStringField(TEXT("target"), Target) || Target != TEXT("local_variable")
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || !Arguments->TryGetStringField(TEXT("function_id"), FunctionId))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Local-variable edits require a function scope and one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"), TEXT("function_id")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("name"), TEXT("type"), TEXT("default")});
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("local_id"), TEXT("new_name")});
    else if (Operation == TEXT("update")) Allowed.Append({TEXT("local_id"), TEXT("field"), TEXT("type"), TEXT("default"), TEXT("policy")});
    else if (Operation == TEXT("remove")) Allowed.Append({TEXT("local_id"), TEXT("policy")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown local-variable edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The local-variable edit contains a field not accepted by its operation")};
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
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    UEdGraph* Graph = FindLocalFunction(Blueprint, FunctionId);
    UK2Node_FunctionEntry* Entry = FindFunctionEntry(Graph);
    if (Graph == nullptr || Entry == nullptr || !IsUserOwnedFunction(Blueprint, Graph)
        || !FBlueprintEditorUtils::DoesSupportLocalVariables(Graph))
    {
        OutError = {TEXT("unsupported_type"), TEXT("The requested function does not support editable local variables")};
        return false;
    }

    FString LocalId;
    FString Name;
    FString NewName;
    FString Field;
    FString Policy;
    FBPVariableDescription* Variable = nullptr;
    FEdGraphPinType NewType;
    FString NewDefault;
    const TSharedPtr<FJsonObject>* TypeObject = nullptr;
    const TSharedPtr<FJsonObject>* DefaultObject = nullptr;
    TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
    References->SetBoolField(TEXT("referenced"), false);
    References->SetNumberField(TEXT("reference_count"), 0);
    References->SetBoolField(TEXT("unresolved_references"), false);
    References->SetBoolField(TEXT("references_truncated"), false);
    References->SetArrayField(TEXT("references"), {});

    if (Operation == TEXT("add"))
    {
        if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                TEXT("function_id"), TEXT("name"), TEXT("type"), TEXT("default")})
            || !Arguments->TryGetStringField(TEXT("name"), Name) || !ValidateLocalName(Blueprint, Entry, Name, NAME_None, OutError)
            || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
        if (NewType.bIsReference || NewType.bIsConst)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Local-variable types cannot be reference or const")};
            return false;
        }
        if (Arguments->HasField(TEXT("default"))
            && (!Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(NewType, *DefaultObject, NewDefault, OutError))) return false;
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("local_id"), LocalId) || (Variable = FindLocalById(Entry, LocalId)) == nullptr)
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local-variable identity is unavailable")};
            return false;
        }
        Name = Variable->VarName.ToString();
        References = LocalReferences(Blueprint, Graph, Variable->VarName);
        if (Operation == TEXT("rename"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("local_id"), TEXT("new_name")})
                || !Arguments->TryGetStringField(TEXT("new_name"), NewName) || NewName == Name
                || !ValidateLocalName(Blueprint, Entry, NewName, Variable->VarName, OutError)) return false;
        }
        else if (Operation == TEXT("update"))
        {
            if (!Arguments->TryGetStringField(TEXT("field"), Field) || (Field != TEXT("type") && Field != TEXT("default")))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Local-variable update field must be type or default")};
                return false;
            }
            if (Field == TEXT("type"))
            {
                if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                        TEXT("function_id"), TEXT("local_id"), TEXT("field"), TEXT("type"), TEXT("policy")})
                    || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced")
                    || !Arguments->TryGetObjectField(TEXT("type"), TypeObject) || TypeObject == nullptr
                    || !UnrealMCP::K2TypeCodec::DecodeType(*TypeObject, NewType, OutError)) return false;
                if (NewType.bIsReference || NewType.bIsConst)
                {
                    OutError = {TEXT("invalid_argument"), TEXT("Local-variable types cannot be reference or const")};
                    return false;
                }
                if (References->GetBoolField(TEXT("referenced")))
                {
                    OutError = {TEXT("referenced_member"), TEXT("The local variable is referenced and the reject-only policy forbids a type change")};
                    OutError.Details->SetStringField(TEXT("local_id"), LocalId);
                    return false;
                }
            }
            else if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("local_id"), TEXT("field"), TEXT("default")})
                || !Arguments->TryGetObjectField(TEXT("default"), DefaultObject) || DefaultObject == nullptr
                || !UnrealMCP::K2TypeCodec::DecodeDefault(Variable->VarType, *DefaultObject, NewDefault, OutError)) return false;
        }
        else if (Operation == TEXT("remove"))
        {
            if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("target"), TEXT("operation"),
                    TEXT("function_id"), TEXT("local_id"), TEXT("policy")})
                || !Arguments->TryGetStringField(TEXT("policy"), Policy) || Policy != TEXT("reject_if_referenced"))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Local-variable removal requires policy reject_if_referenced")};
                return false;
            }
            if (References->GetBoolField(TEXT("referenced")))
            {
                OutError = {TEXT("referenced_member"), TEXT("The local variable is referenced and the reject-only policy forbids removal")};
                OutError.Details->SetStringField(TEXT("local_id"), LocalId);
                return false;
            }
        }
    }

    UStruct* Scope = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(Graph->GetFName()) : nullptr;
    if (Operation != TEXT("add") && Scope == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The live generated function scope is unavailable; compile and inspect before retrying"), MakeShared<FJsonObject>(), true};
        return false;
    }
    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP local variable edit")));
        Blueprint->Modify();
        Entry->Modify();
        if (Operation == TEXT("add"))
        {
            bApplied = FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, FName(*Name), NewType, NewDefault);
            Variable = FindLocalByName(FindFunctionEntry(Graph), Name);
            bApplied = bApplied && Variable != nullptr && Variable->VarGuid.IsValid();
            if (bApplied) LocalId = GuidString(Variable->VarGuid);
        }
        else if (Operation == TEXT("rename"))
        {
            FBlueprintEditorUtils::RenameLocalVariable(Blueprint, Scope, Variable->VarName, FName(*NewName));
            Variable = FindLocalById(FindFunctionEntry(Graph), LocalId);
            bApplied = Variable != nullptr && Variable->VarName == FName(*NewName);
        }
        else if (Operation == TEXT("remove"))
        {
            FBlueprintEditorUtils::RemoveLocalVariable(Blueprint, Scope, Variable->VarName);
            bApplied = FindLocalById(FindFunctionEntry(Graph), LocalId) == nullptr;
        }
        else if (Field == TEXT("type"))
        {
            FBlueprintEditorUtils::ChangeLocalVariableType(Blueprint, Scope, Variable->VarName, NewType);
            Variable = FindLocalById(FindFunctionEntry(Graph), LocalId);
            bApplied = Variable != nullptr && Variable->VarType == NewType;
        }
        else
        {
            Variable->DefaultValue = NewDefault;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            Variable = FindLocalById(FindFunctionEntry(Graph), LocalId);
            bApplied = Variable != nullptr && Variable->DefaultValue == NewDefault;
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_member"), TEXT("Unreal rejected the local-variable edit without a committed change")};
        return false;
    }

    TSharedPtr<FJsonObject> Local;
    TSharedPtr<FJsonObject> ResultReferences = References;
    if (Operation == TEXT("remove"))
    {
        Local = MakeShared<FJsonObject>();
        Local->SetStringField(TEXT("id"), LocalId);
        Local->SetStringField(TEXT("name"), Name);
        Local->SetBoolField(TEXT("removed"), true);
    }
    else
    {
        if (!ReadInspectedScopedRecord(Inspector, ObjectPath, TEXT("local_id"), LocalId, TEXT("local_variables"), Local, OutError))
        {
            RestoreFailedTransaction(OutError);
            return false;
        }
        const TSharedPtr<FJsonObject>* ReadReferences = nullptr;
        if (Local->TryGetObjectField(TEXT("reference_summary"), ReadReferences) && ReadReferences != nullptr) ResultReferences = *ReadReferences;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Local,
        Operation == TEXT("add") ? TArray<FString>{LocalId} : TArray<FString>{});
    OutResult->SetObjectField(TEXT("local_variable"), Local);
    OutResult->SetObjectField(TEXT("reference_summary"), ResultReferences);
    return true;
}
