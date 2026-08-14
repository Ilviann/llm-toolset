#include "UnrealMCPAssetAuthoringKernel.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

namespace
{
bool IsLowerHexIdentity(const FString& Value)
{
    if (Value.Len() != 32)
    {
        return false;
    }
    for (const TCHAR Character : Value)
    {
        if (!((Character >= TEXT('0') && Character <= TEXT('9'))
            || (Character >= TEXT('a') && Character <= TEXT('f'))))
        {
            return false;
        }
    }
    return true;
}

bool AuthoringPathContainsSymlink(const FString& Root, const FString& Candidate)
{
    IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
    FString Current = Root;
    FPaths::NormalizeDirectoryName(Current);
    if (Platform.IsSymlink(*Current) == ESymlinkResult::Symlink)
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
        if ((Platform.FileExists(*Current) || Platform.DirectoryExists(*Current))
            && Platform.IsSymlink(*Current) == ESymlinkResult::Symlink)
        {
            return true;
        }
    }
    return false;
}

bool IsUnsafeEditorState()
{
    return GEditor == nullptr
        || GEditor->PlayWorld != nullptr
        || GEditor->IsTransactionActive()
        || UE::IsSavingPackage()
        || IsGarbageCollecting()
        || IsAsyncLoading();
}

bool RestoreEdit(
    TUniquePtr<FScopedTransaction>& Transaction,
    const FUnrealMCPAssetEditRequest& Request,
    const FUnrealMCPAssetEditHooks& Hooks,
    const FString& BeforeSnapshot,
    FUnrealMCPError& OutError)
{
    Transaction.Reset();
    if (GEditor == nullptr || !GEditor->UndoTransaction())
    {
        OutError = {TEXT("rollback_failed"), TEXT("The rejected asset mutation could not be restored through Undo")};
        return false;
    }
    if (Request.bPersist)
    {
        FUnrealMCPError PersistError;
        if (!Hooks.Persist || !Hooks.Persist(Request.Asset, PersistError))
        {
            OutError = {TEXT("rollback_failed"), TEXT("The restored asset state could not be persisted")};
            return false;
        }
    }
    FString RestoredSnapshot;
    FUnrealMCPError ReadBackError;
    if (!Hooks.ReadBack(Request.Asset, RestoredSnapshot, ReadBackError)
        || RestoredSnapshot != BeforeSnapshot)
    {
        OutError = {TEXT("rollback_failed"), TEXT("The rejected asset mutation did not restore its exact prior snapshot")};
        return false;
    }
    return true;
}
}

FString FUnrealMCPAssetAuthoringKernel::ObjectPathForPackage(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

bool FUnrealMCPAssetAuthoringKernel::ValidateOperationId(
    const FString& OperationId,
    FUnrealMCPError& OutError)
{
    if (OperationId.IsEmpty() || IsLowerHexIdentity(OperationId))
    {
        return true;
    }
    OutError = {
        TEXT("invalid_argument"),
        TEXT("operation_id must be one 32-character lowercase hexadecimal identity")};
    return false;
}

bool FUnrealMCPAssetAuthoringKernel::ValidateCanonicalTarget(
    const FString& PackageName,
    const FString& ObjectPath,
    FUnrealMCPError& OutError)
{
    if (!PackageName.StartsWith(TEXT("/"))
        || PackageName.StartsWith(TEXT("//"))
        || PackageName.EndsWith(TEXT("/"))
        || PackageName.Contains(TEXT(".."))
        || PackageName.Contains(TEXT("\\"))
        || PackageName.Contains(TEXT("."))
        || PackageName.Len() > 512
        || !FPackageName::IsValidLongPackageName(PackageName, true)
        || ObjectPath != ObjectPathForPackage(PackageName)
        || !FPackageName::IsValidObjectPath(ObjectPath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The authoring target must be one exact canonical Unreal asset path")};
        return false;
    }
    return true;
}

bool FUnrealMCPAssetAuthoringKernel::ValidateMutationScope(
    const FString& PackageName,
    FUnrealMCPError& OutError)
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
        if ((FPaths::IsSamePath(PhysicalTarget, ProjectContent)
                || FPaths::IsUnderDirectory(PhysicalTarget, ProjectContent))
            && !AuthoringPathContainsSymlink(ProjectContent, PhysicalTarget))
        {
            return true;
        }
        OutError = {TEXT("mutation_scope_denied"), TEXT("Project content resolves outside its physical mount")};
        return false;
    }

    const int32 Slash = PackageName.Find(
        TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    FString MountDirectory;
    if (Slash == INDEX_NONE
        || !FPackageName::TryConvertLongPackageNameToFilename(
            PackageName.Left(Slash + 1), MountDirectory))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("Only project content and local project-plugin content are mutable")};
        return false;
    }

    FString PluginRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
    FString Mount = FPaths::ConvertRelativePathToFull(MountDirectory);
    FPaths::NormalizeDirectoryName(PluginRoot);
    FPaths::NormalizeDirectoryName(Mount);
    if (!FPaths::IsUnderDirectory(Mount, PluginRoot)
        || !FPaths::IsUnderDirectory(PhysicalTarget, PluginRoot)
        || AuthoringPathContainsSymlink(PluginRoot, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("Only symlink-free local project-plugin mounts are mutable")};
        return false;
    }
    for (FString Candidate = Mount; FPaths::IsUnderDirectory(Candidate, PluginRoot);)
    {
        TArray<FString> Descriptors;
        IFileManager::Get().FindFiles(
            Descriptors, *(Candidate / TEXT("*.uplugin")), true, false);
        if (!Descriptors.IsEmpty())
        {
            return true;
        }
        const FString Parent = FPaths::GetPath(Candidate);
        if (Parent == Candidate)
        {
            break;
        }
        Candidate = Parent;
    }
    OutError = {TEXT("mutation_scope_denied"), TEXT("The content mount is not owned by a local project plugin")};
    return false;
}

bool FUnrealMCPAssetAuthoringKernel::ResolveWritableFilename(
    const FString& PackageName,
    FString& OutFilename,
    FUnrealMCPError& OutError)
{
    if (!FPackageName::TryConvertLongPackageNameToFilename(
            PackageName, OutFilename, FPackageName::GetAssetPackageExtension()))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The package does not resolve to mounted content")};
        return false;
    }
    FString Directory = FPaths::GetPath(OutFilename);
    while (!Directory.IsEmpty() && !IFileManager::Get().DirectoryExists(*Directory))
    {
        const FString Parent = FPaths::GetPath(Directory);
        if (Parent == Directory)
        {
            break;
        }
        Directory = Parent;
    }
    if (Directory.IsEmpty()
        || IFileManager::Get().IsReadOnly(*Directory)
        || (IFileManager::Get().FileExists(*OutFilename)
            && IFileManager::Get().IsReadOnly(*OutFilename)))
    {
        OutError = {TEXT("write_conflict"), TEXT("The package destination is read-only or unavailable")};
        return false;
    }
    return true;
}

bool FUnrealMCPAssetAuthoringKernel::DestinationExists(
    const FString& PackageName,
    const FString& ObjectPath)
{
    if (UPackage* Package = FindPackage(nullptr, *PackageName))
    {
        if (!Package->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
        {
            return true;
        }
    }
    if (UObject* Object = FindObject<UObject>(nullptr, *ObjectPath))
    {
        if (!Object->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
        {
            return true;
        }
    }
    FString Filename;
    return FPackageName::DoesPackageExist(PackageName, &Filename)
        || (!Filename.IsEmpty() && IFileManager::Get().FileExists(*Filename))
        || FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
}

void FUnrealMCPAssetAuthoringKernel::CleanupCreation(
    UPackage* Package,
    UObject* Asset,
    const FString& Filename,
    bool bPublished)
{
    if (Package == nullptr)
    {
        return;
    }
    if (bPublished && Asset != nullptr)
    {
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get().AssetDeleted(Asset);
    }
    if (!Filename.IsEmpty() && IFileManager::Get().FileExists(*Filename))
    {
        IFileManager::Get().Delete(*Filename, false, true, true);
    }
    Package->SetDirtyFlag(false);
    Package->Rename(
        *(TEXT("/Temp/UnrealMCPFailed_")
            + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
        nullptr,
        REN_DontCreateRedirectors | REN_NonTransactional);
    ForEachObjectWithPackage(Package, [](UObject* Object)
    {
        Object->ClearFlags(RF_Public | RF_Standalone);
        Object->MarkAsGarbage();
        return true;
    }, EGetObjectsFlags::IncludeNestedObjects);
    Package->MarkAsGarbage();
}

bool FUnrealMCPAssetAuthoringKernel::ExecuteCreation(
    const FUnrealMCPAssetCreationRequest& Request,
    const FUnrealMCPAssetCreationHooks& Hooks,
    FUnrealMCPAssetCreationResult& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    OutResult = {};
    if (!ValidateOperationId(Request.OperationId, OutError)
        || !ValidateCanonicalTarget(Request.PackageName, Request.ObjectPath, OutError)
        || !ValidateMutationScope(Request.PackageName, OutError))
    {
        return false;
    }
    if (!Hooks.Create || !Hooks.Persist || !Hooks.ReadBack)
    {
        OutError = {TEXT("internal_error"), TEXT("The asset creation lifecycle is incomplete")};
        return false;
    }
    if (DestinationExists(Request.PackageName, Request.ObjectPath))
    {
        OutError = {TEXT("already_exists"), TEXT("The destination package or asset already exists")};
        return false;
    }
    FString Filename;
    if (!ResolveWritableFilename(Request.PackageName, Filename, OutError))
    {
        return false;
    }
    if (IsUnsafeEditorState())
    {
        OutError = {TEXT("busy"), TEXT("Asset creation is unavailable in the current editor state"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }

    UPackage* Package = CreatePackage(*Request.PackageName);
    UObject* Asset = nullptr;
    bool bPublished = false;
    if (Package == nullptr || !Hooks.Create(Package, Asset, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("internal_error"), TEXT("Unreal could not create the requested asset")};
        }
        CleanupCreation(Package, Asset, Filename, false);
        return false;
    }
    if (Asset == nullptr || Asset->GetOutermost() != Package
        || Asset->GetPathName() != Request.ObjectPath)
    {
        OutError = {TEXT("internal_error"), TEXT("The creation adapter returned an unexpected asset identity")};
        CleanupCreation(Package, Asset, Filename, false);
        return false;
    }
    if (Hooks.Finalize && !Hooks.Finalize(Asset, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("compile_failed"), TEXT("The new asset did not finalize successfully")};
        }
        CleanupCreation(Package, Asset, Filename, false);
        return false;
    }
    if (!Hooks.Persist(Asset, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("save_failed"), TEXT("The new asset package could not be saved")};
        }
        CleanupCreation(Package, Asset, Filename, false);
        return false;
    }
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get().AssetCreated(Asset);
    bPublished = true;
    FString Snapshot;
    if (!Hooks.ReadBack(Asset, Snapshot, OutError) || Snapshot.Len() != 40)
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("internal_error"), TEXT("Creation read-back did not return one exact snapshot")};
        }
        CleanupCreation(Package, Asset, Filename, bPublished);
        return false;
    }

    OutResult.Asset = Asset;
    OutResult.ObjectPath = Request.ObjectPath;
    OutResult.SnapshotId = Snapshot;
    return true;
}

bool FUnrealMCPAssetAuthoringKernel::ExecuteEdit(
    const FUnrealMCPAssetEditRequest& Request,
    const FUnrealMCPAssetEditHooks& Hooks,
    FUnrealMCPAssetEditResult& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    OutResult = {};
    const FString PackageName = FPackageName::ObjectPathToPackageName(Request.ObjectPath);
    if (!ValidateOperationId(Request.OperationId, OutError)
        || !ValidateCanonicalTarget(PackageName, Request.ObjectPath, OutError)
        || !ValidateMutationScope(PackageName, OutError))
    {
        return false;
    }
    if (Request.Asset == nullptr || !IsValid(Request.Asset)
        || Request.Asset->GetPathName() != Request.ObjectPath
        || Request.Asset->GetOutermost()->GetName() != PackageName)
    {
        OutError = {TEXT("not_found"), TEXT("The exact loaded authoring target is unavailable")};
        return false;
    }
    if (Request.ExpectedSnapshot.Len() != 40
        || Request.TransactionLabel.IsEmpty()
        || Request.TransactionLabel.Len() > 128
        || !Hooks.ReadBack || !Hooks.Mutate
        || (Request.bPersist && !Hooks.Persist))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The asset edit lifecycle is incomplete or unbounded")};
        return false;
    }
    FString Filename;
    if (Request.bPersist
        && !ResolveWritableFilename(PackageName, Filename, OutError))
    {
        return false;
    }
    if (IsUnsafeEditorState())
    {
        OutError = {TEXT("busy"), TEXT("Asset editing is unavailable in the current editor state"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    if (Hooks.ValidateState && !Hooks.ValidateState(Request.Asset, OutError))
    {
        return false;
    }

    FString BeforeSnapshot;
    if (!Hooks.ReadBack(Request.Asset, BeforeSnapshot, OutError)
        || BeforeSnapshot.Len() != 40)
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("internal_error"), TEXT("Edit admission could not read one exact current snapshot")};
        }
        return false;
    }
    if (BeforeSnapshot != Request.ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The asset snapshot changed before mutation")};
        OutError.Details->SetStringField(TEXT("current_snapshot"), BeforeSnapshot);
        return false;
    }

    UPackage* AssetPackage = Request.Asset->GetOutermost();
    const bool bPackageWasDirty = AssetPackage != nullptr && AssetPackage->IsDirty();
    TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(
        FText::FromString(Request.TransactionLabel));
    Request.Asset->SetFlags(RF_Transactional);
    Request.Asset->Modify();
    if (!Hooks.Mutate(Request.Asset, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("no_change"), TEXT("The requested asset edit made no change")};
        }
        FString RejectedSnapshot;
        FUnrealMCPError RejectedReadBackError;
        if (Hooks.ReadBack(Request.Asset, RejectedSnapshot, RejectedReadBackError)
            && RejectedSnapshot == BeforeSnapshot)
        {
            Transaction->Cancel();
            Transaction.Reset();
            if (AssetPackage != nullptr)
            {
                AssetPackage->SetDirtyFlag(bPackageWasDirty);
            }
            return false;
        }
        RestoreEdit(Transaction, Request, Hooks, BeforeSnapshot, OutError);
        return false;
    }
    if (Request.bPersist && !Hooks.Persist(Request.Asset, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("save_failed"), TEXT("The asset mutation could not be saved")};
        }
        RestoreEdit(Transaction, Request, Hooks, BeforeSnapshot, OutError);
        return false;
    }
    FString AfterSnapshot;
    if (!Hooks.ReadBack(Request.Asset, AfterSnapshot, OutError)
        || AfterSnapshot.Len() != 40
        || (Request.bRequireChangedSnapshot && AfterSnapshot == BeforeSnapshot))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {TEXT("internal_error"), TEXT("Asset read-back did not verify the requested postcondition")};
        }
        RestoreEdit(Transaction, Request, Hooks, BeforeSnapshot, OutError);
        return false;
    }
    Transaction.Reset();
    OutResult.BeforeSnapshot = BeforeSnapshot;
    OutResult.SnapshotId = AfterSnapshot;
    return true;
}
