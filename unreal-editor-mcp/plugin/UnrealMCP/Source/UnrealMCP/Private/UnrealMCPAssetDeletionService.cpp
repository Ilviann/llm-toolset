#include "UnrealMCPAssetDeletionService.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealMCPAssetReferenceService.h"
#include "UnrealMCPVersion.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealMCPAssetDeletionPrivate
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

bool IsLowerHex(const FString& Value, int32 Length)
{
    if (Value.Len() != Length)
    {
        return false;
    }
    for (const TCHAR Character : Value)
    {
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character))
        {
            return false;
        }
    }
    return true;
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
        if ((PlatformFile.FileExists(*Current) || PlatformFile.DirectoryExists(*Current))
            && PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink)
        {
            return true;
        }
    }
    return false;
}

bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError)
{
    FString PhysicalTarget;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The target mount is unavailable")};
        return false;
    }
    PhysicalTarget = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PhysicalTarget));
    FPaths::NormalizeDirectoryName(PhysicalTarget);
    if (PackageName.StartsWith(TEXT("/Game/")))
    {
        FString ProjectContent = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeDirectoryName(ProjectContent);
        if (!(FPaths::IsSamePath(PhysicalTarget, ProjectContent)
                || FPaths::IsUnderDirectory(PhysicalTarget, ProjectContent))
            || PathContainsSymlink(ProjectContent, PhysicalTarget))
        {
            OutError = {
                TEXT("mutation_scope_denied"),
                TEXT("Project content resolves outside its symlink-free physical mount")};
            return false;
        }
        return true;
    }

    const int32 SecondSlash =
        PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    if (SecondSlash == INDEX_NONE)
    {
        OutError = {
            TEXT("mutation_scope_denied"),
            TEXT("Only project content and local project-plugin content are mutable")};
        return false;
    }
    const FString MountRoot = PackageName.Left(SecondSlash + 1);
    FString MountDirectory;
    if (!FPackageName::TryConvertLongPackageNameToFilename(MountRoot, MountDirectory))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The target mount is unavailable")};
        return false;
    }
    FString ProjectPlugins = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
    FString PhysicalMount = FPaths::ConvertRelativePathToFull(MountDirectory);
    FPaths::NormalizeDirectoryName(ProjectPlugins);
    FPaths::NormalizeDirectoryName(PhysicalMount);
    if (!FPaths::IsUnderDirectory(PhysicalMount, ProjectPlugins)
        || !(FPaths::IsSamePath(PhysicalTarget, PhysicalMount)
            || FPaths::IsUnderDirectory(PhysicalTarget, PhysicalMount))
        || PathContainsSymlink(ProjectPlugins, PhysicalTarget))
    {
        OutError = {
            TEXT("mutation_scope_denied"),
            TEXT("Only symlink-free local project-plugin content mounts are mutable outside /Game")};
        return false;
    }
    FString Candidate = PhysicalMount;
    while (FPaths::IsUnderDirectory(Candidate, ProjectPlugins))
    {
        TArray<FString> Descriptors;
        IFileManager::Get().FindFiles(Descriptors, *(Candidate / TEXT("*.uplugin")), true, false);
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
    OutError = {
        TEXT("mutation_scope_denied"),
        TEXT("The target mount is not owned by a local project plugin")};
    return false;
}

bool ReferenceScansSufficient(
    const FUnrealMCPAssetReferenceSnapshot& Snapshot,
    bool& OutLiveScanComplete,
    FUnrealMCPError& OutError)
{
    OutLiveScanComplete = false;
    if (!Snapshot.Scans.IsValid())
    {
        OutError = {TEXT("reference_preflight_failed"), TEXT("Reference scan metadata is unavailable")};
        return false;
    }
    for (const TCHAR* Name : {
        TEXT("serialized"), TEXT("management"), TEXT("searchable_name")})
    {
        const TSharedPtr<FJsonObject>* Status = nullptr;
        bool bComplete = false;
        if (!Snapshot.Scans->TryGetObjectField(Name, Status) || Status == nullptr
            || !Status->IsValid() || !(*Status)->TryGetBoolField(TEXT("complete"), bComplete)
            || !bComplete)
        {
            const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("scan"), Name);
            if (Status != nullptr && Status->IsValid())
            {
                Details->SetObjectField(TEXT("status"), *Status);
            }
            OutError = {
                TEXT("reference_preflight_incomplete"),
                TEXT("Deletion requires complete serialized, management, and searchable-name scans"),
                Details,
                true};
            return false;
        }
    }
    const TSharedPtr<FJsonObject>* LiveStatus = nullptr;
    bool bUnsupported = true;
    bool bStale = true;
    if (!Snapshot.Scans->TryGetObjectField(TEXT("live_memory"), LiveStatus)
        || LiveStatus == nullptr || !LiveStatus->IsValid()
        || !(*LiveStatus)->TryGetBoolField(TEXT("unsupported"), bUnsupported)
        || !(*LiveStatus)->TryGetBoolField(TEXT("stale"), bStale)
        || bUnsupported || bStale)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("scan"), TEXT("live_memory"));
        OutError = {
            TEXT("reference_preflight_incomplete"),
            TEXT("Deletion requires a supported, current bounded live-memory scan"),
            Details,
            true};
        return false;
    }
    (*LiveStatus)->TryGetBoolField(TEXT("complete"), OutLiveScanComplete);
    return true;
}

bool HasUnsafeEditorWork(FUnrealMCPError& OutError)
{
    const bool bPlaying = GEditor != nullptr && GEditor->IsPlayingSessionInEditor();
    const bool bSimulating = GEditor != nullptr && GEditor->IsSimulatingInEditor();
    const bool bSaving = UE::IsSavingPackage();
    const bool bCollecting = IsGarbageCollecting();
    const bool bTransaction = GEditor != nullptr && GEditor->IsTransactionActive();
    const bool bUndoRedo = GIsTransacting;
    const bool bCompiling = FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
    const bool bAsyncLoading = IsAsyncLoading();
    if (!bPlaying && !bSimulating && !bSaving && !bCollecting && !bTransaction
        && !bUndoRedo && !bCompiling && !bAsyncLoading)
    {
        return false;
    }
    const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetBoolField(TEXT("is_playing"), bPlaying);
    Details->SetBoolField(TEXT("is_simulating"), bSimulating);
    Details->SetBoolField(TEXT("is_saving"), bSaving);
    Details->SetBoolField(TEXT("is_garbage_collecting"), bCollecting);
    Details->SetBoolField(TEXT("transaction_active"), bTransaction);
    Details->SetBoolField(TEXT("undo_redo_active"), bUndoRedo);
    Details->SetBoolField(TEXT("compiling_assets"), bCompiling);
    Details->SetBoolField(TEXT("async_loading"), bAsyncLoading);
    OutError = {
        TEXT("busy"),
        TEXT("Asset deletion refused while unsafe editor work is active"),
        Details,
        true};
    return true;
}

bool IsCurrentMapPackage(const FString& PackageName)
{
    if (GEditor == nullptr)
    {
        return false;
    }
    const UWorld* World = GEditor->GetEditorWorldContext().World();
    return World != nullptr && World->GetOutermost() != nullptr
        && World->GetOutermost()->GetName() == PackageName;
}

TSharedRef<FJsonObject> BuildResult(
    const FString& AssetPath,
    const FString& PackageName,
    const FString& ExpectedSnapshot,
    const FString& FinalSnapshot,
    bool bRegistryAbsent,
    bool bStorageAbsent,
    bool bLiveScanComplete,
    const FString& OperationState)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("package_name"), PackageName);
    Result->SetStringField(TEXT("expected_reference_snapshot"), ExpectedSnapshot);
    Result->SetStringField(TEXT("predelete_reference_snapshot"), FinalSnapshot);
    Result->SetBoolField(TEXT("delete_api_succeeded"), true);
    Result->SetBoolField(TEXT("asset_registry_absent"), bRegistryAbsent);
    Result->SetBoolField(TEXT("storage_absent"), bStorageAbsent);
    Result->SetBoolField(TEXT("bounded_live_scan_complete"), bLiveScanComplete);
    Result->SetBoolField(TEXT("engine_reference_check_complete"), true);
    Result->SetBoolField(TEXT("deleted"), bRegistryAbsent && bStorageAbsent);
    Result->SetBoolField(TEXT("undo_supported"), false);
    Result->SetStringField(TEXT("operation_state"), OperationState);
    return Result;
}

bool IsLoadedLevelPackage(const FString& PackageName)
{
    if (GEditor == nullptr) return false;
    UWorld* Current = GEditor->GetEditorWorldContext().World();
    if (Current == nullptr) return false;
    for (const ULevelStreaming* Streaming : Current->GetStreamingLevels())
    {
        if (Streaming != nullptr && (Streaming->GetWorldAssetPackageFName() == FName(*PackageName)
            || (Streaming->GetLoadedLevel() != nullptr
                && Streaming->GetLoadedLevel()->GetOutermost()->GetName() == PackageName))) return true;
    }
    return false;
}

bool AddPackagesBelowPath(
    const FString& LongPackagePath,
    TSet<FString>& OutPackages,
    FUnrealMCPError& OutError)
{
    FString Directory;
    if (!FPackageName::TryConvertLongPackageNameToFilename(LongPackagePath, Directory))
    {
        OutError = {TEXT("package_closure_unsupported"), TEXT("A map-owned package path could not be resolved")};
        return false;
    }
    TArray<FString> Filenames;
    FPackageName::FindPackagesInDirectory(Filenames, Directory);
    for (const FString& Filename : Filenames)
    {
        FString PackageName;
        if (!FPackageName::TryConvertFilenameToLongPackageName(Filename, PackageName))
        {
            OutError = {TEXT("package_closure_unsupported"), TEXT("A map-owned package filename could not be mapped to mounted content")};
            return false;
        }
        OutPackages.Add(PackageName);
        if (OutPackages.Num() > UnrealMCP::MaxLevelOwnedPackages)
        {
            OutError = {TEXT("package_closure_truncated"), TEXT("The complete map-owned package closure exceeds the published bound")};
            return false;
        }
    }
    return true;
}

bool BuildMapClosure(
    UWorld* World,
    const FString& RootPackage,
    TSet<FString>& OutPackages,
    TArray<UObject*>& OutObjects,
    FUnrealMCPError& OutError)
{
    if (World == nullptr || World->PersistentLevel == nullptr)
    {
        OutError = {TEXT("package_closure_unsupported"), TEXT("The map package could not provide a persistent level")};
        return false;
    }
    OutPackages.Add(RootPackage);
    OutObjects.Add(World);
    ObjectTools::AddExtraObjectsToDelete(OutObjects);
    for (UObject* Object : OutObjects)
    {
        if (Object != nullptr && Object->GetOutermost() != nullptr)
        {
            OutPackages.Add(Object->GetOutermost()->GetName());
        }
    }
    TArray<FString> OwnedPaths = ULevel::GetExternalActorsPaths(RootPackage);
    OwnedPaths.Append(ULevel::GetExternalObjectsPaths(RootPackage));
    for (const FString& Path : OwnedPaths)
    {
        if (!AddPackagesBelowPath(Path, OutPackages, OutError)) return false;
    }
    for (UPackage* Package : World->PersistentLevel->GetLoadedExternalObjectPackages())
    {
        if (Package != nullptr) OutPackages.Add(Package->GetName());
    }
    if (OutPackages.Num() > UnrealMCP::MaxLevelOwnedPackages)
    {
        OutError = {TEXT("package_closure_truncated"), TEXT("The complete map-owned package closure exceeds the published bound")};
        return false;
    }
    return true;
}

bool HasExternalClosureReferencers(
    IAssetRegistry& Registry,
    const TSet<FString>& Closure,
    FUnrealMCPError& OutError)
{
    if (Registry.IsGathering())
    {
        OutError = {TEXT("reference_preflight_incomplete"), TEXT("Map deletion requires a current complete Asset Registry"), MakeShared<FJsonObject>(), true};
        return true;
    }
    int32 CandidateCount = 0;
    for (const FString& PackageName : Closure)
    {
        TArray<FAssetIdentifier> Referencers;
        Registry.GetReferencers(
            FAssetIdentifier(FName(*PackageName)), Referencers,
            UE::AssetRegistry::EDependencyCategory::All);
        CandidateCount += Referencers.Num();
        if (CandidateCount > UnrealMCP::MaxAssetReferenceRegistryCandidates)
        {
            OutError = {TEXT("reference_preflight_incomplete"), TEXT("Map-closure referencer enumeration exceeds the published bound")};
            return true;
        }
        for (const FAssetIdentifier& Referencer : Referencers)
        {
            const FString ReferencerPackage = Referencer.PackageName.ToString();
            if (ReferencerPackage.IsEmpty() || !Closure.Contains(ReferencerPackage))
            {
                const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
                Details->SetStringField(TEXT("owned_package"), PackageName);
                Details->SetStringField(TEXT("referencer"), Referencer.ToString().Left(512));
                OutError = {TEXT("asset_referenced"), TEXT("A map-owned package has an external registry referencer"), Details};
                return true;
            }
        }
    }
    return false;
}

bool RecordsContainExternalReferences(
    const FUnrealMCPAssetReferenceSnapshot& Snapshot,
    const TSet<FString>& Closure,
    FUnrealMCPError& OutError)
{
    for (const TSharedPtr<FJsonValue>& Value : Snapshot.Records)
    {
        const TSharedPtr<FJsonObject> Record = Value.IsValid() ? Value->AsObject() : nullptr;
        FString ReferencerPackage;
        if (!Record.IsValid() || !Record->TryGetStringField(TEXT("referencer_package"), ReferencerPackage)
            || ReferencerPackage.IsEmpty() || !Closure.Contains(ReferencerPackage))
        {
            OutError = {TEXT("asset_referenced"), TEXT("The map has an external serialized or live-memory referencer")};
            return true;
        }
    }
    return false;
}

bool DeleteMap(
    FUnrealMCPAssetReferenceService& References,
    const FAssetData& Asset,
    const FString& AssetPath,
    const FString& ExpectedSnapshot,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    const FString PackageName = Asset.PackageName.ToString();
    if (IsCurrentMapPackage(PackageName) || IsLoadedLevelPackage(PackageName))
    {
        OutError = {TEXT("active_map"), TEXT("Map deletion refuses the current map and loaded streaming or sublevel maps")};
        return false;
    }
    UWorld* World = Cast<UWorld>(Asset.GetAsset({ULevel::LoadAllExternalObjectsTag}));
    // The exact map load may queue post-load work for its owned external objects. Drain only
    // that operation-owned work before the fresh reference and editor-state preflight.
    FlushAsyncLoading();
    TSet<FString> Closure;
    TArray<UObject*> DeleteObjects;
    if (!BuildMapClosure(World, PackageName, Closure, DeleteObjects, OutError)) return false;
    TMap<FString, FString> OriginalFiles;
    for (const FString& OwnedPackage : Closure)
    {
        if (!ValidateMutationScope(OwnedPackage, OutError)) return false;
        UPackage* Loaded = FindPackage(nullptr, *OwnedPackage);
        if (Loaded != nullptr && Loaded->IsDirty())
        {
            OutError = {TEXT("dirty_package"), TEXT("A map-owned package has unsaved changes")};
            return false;
        }
        FString Filename;
        if (!FPackageName::DoesPackageExist(OwnedPackage, &Filename) || Filename.IsEmpty())
        {
            OutError = {TEXT("package_closure_unsupported"), TEXT("A map-owned package has no verifiable persisted storage")};
            return false;
        }
        if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Filename) == ESymlinkResult::Symlink
            || IFileManager::Get().IsReadOnly(*Filename))
        {
            OutError = {TEXT("write_conflict"), TEXT("A map-owned package is symlinked or read-only")};
            return false;
        }
        OriginalFiles.Add(OwnedPackage, Filename);
    }
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    if (HasExternalClosureReferencers(Registry, Closure, OutError)) return false;
    FUnrealMCPAssetReferenceSnapshot Final;
    bool bLiveScanComplete = false;
    if (!References.Capture(AssetPath, Final, OutError)
        || !ReferenceScansSufficient(Final, bLiveScanComplete, OutError)
        || RecordsContainExternalReferences(Final, Closure, OutError)) return false;
    bool bReferenced = false;
    bool bReferencedByUndo = false;
    ObjectTools::GatherObjectReferencersForDeletion(World, bReferenced, bReferencedByUndo, nullptr, false);
    if (bReferenced || bReferencedByUndo)
    {
        OutError = {TEXT("asset_referenced"), TEXT("Unreal's full deletion check found retained map or Undo references")};
        return false;
    }
    if (HasUnsafeEditorWork(OutError)) return false;
    const TArray<FAssetData> Assets{Asset};
    const int32 DeletedCount = ObjectTools::DeleteAssets(Assets, false);
    TArray<TSharedPtr<FJsonValue>> PackageResults;
    bool bAllRegistryAbsent = true;
    bool bAllStorageAbsent = true;
    TArray<FString> Sorted = Closure.Array();
    Sorted.Sort();
    for (const FString& OwnedPackage : Sorted)
    {
        TArray<FAssetData> Remaining;
        Registry.GetAssetsByPackageName(FName(*OwnedPackage), Remaining, true);
        const bool bRegistryAbsent = Remaining.IsEmpty();
        FString RemainingFilename;
        const bool bStorageAbsent = !FPackageName::DoesPackageExist(OwnedPackage, &RemainingFilename)
            && !IFileManager::Get().FileExists(*OriginalFiles.FindChecked(OwnedPackage));
        bAllRegistryAbsent &= bRegistryAbsent;
        bAllStorageAbsent &= bStorageAbsent;
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("package_name"), OwnedPackage);
        Record->SetBoolField(TEXT("asset_registry_absent"), bRegistryAbsent);
        Record->SetBoolField(TEXT("storage_absent"), bStorageAbsent);
        PackageResults.Add(MakeShared<FJsonValueObject>(Record));
    }
    const bool bDeleted = DeletedCount > 0 && bAllRegistryAbsent && bAllStorageAbsent;
    const TSharedRef<FJsonObject> Result = BuildResult(
        AssetPath, PackageName, ExpectedSnapshot, Final.SnapshotId,
        bAllRegistryAbsent, bAllStorageAbsent, bLiveScanComplete,
        bDeleted ? TEXT("committed") : TEXT("partial"));
    Result->SetBoolField(TEXT("map_deletion"), true);
    Result->SetNumberField(TEXT("owned_package_count"), Closure.Num());
    Result->SetArrayField(TEXT("package_closure"), PackageResults);
    Result->SetBoolField(TEXT("package_closure_complete"), true);
    Result->SetBoolField(TEXT("delete_api_succeeded"), DeletedCount > 0);
    Result->SetBoolField(TEXT("deleted"), bDeleted);
    OutResult = Result;
    return true;
}
}

FUnrealMCPAssetDeletionService::FUnrealMCPAssetDeletionService(
    FUnrealMCPAssetReferenceService& InReferences)
    : References(InReferences)
{
}

bool FUnrealMCPAssetDeletionService::Delete(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid()
        || !UnrealMCPAssetDeletionPrivate::HasOnlyFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot")}))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("asset_delete accepts only operation_id, asset_path, and expected_snapshot")};
        return false;
    }
    FString OperationId;
    FString AssetPath;
    FString ExpectedSnapshot;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId)
        || !UnrealMCPAssetDeletionPrivate::IsLowerHex(OperationId, 32)
        || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot)
        || !UnrealMCPAssetDeletionPrivate::IsLowerHex(ExpectedSnapshot, 40))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("asset_delete requires valid operation_id, exact asset_path, and reference snapshot")};
        return false;
    }
    if (UnrealMCPAssetDeletionPrivate::HasUnsafeEditorWork(OutError))
    {
        return false;
    }

    FUnrealMCPAssetReferenceSnapshot Initial;
    if (!References.Capture(AssetPath, Initial, OutError))
    {
        return false;
    }
    if (Initial.SnapshotId != ExpectedSnapshot)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("expected_snapshot"), ExpectedSnapshot);
        Details->SetStringField(TEXT("actual_snapshot"), Initial.SnapshotId);
        OutError = {
            TEXT("stale_precondition"),
            TEXT("The asset-reference snapshot changed before deletion"),
            Details};
        return false;
    }

    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
    if (!Asset.IsValid() || Asset.GetObjectPathString() != AssetPath)
    {
        OutError = {TEXT("not_found"), TEXT("The exact deletion target is no longer registered")};
        return false;
    }
    const FString PackageName = Asset.PackageName.ToString();
    if (!UnrealMCPAssetDeletionPrivate::ValidateMutationScope(
        PackageName,
        OutError))
    {
        return false;
    }
    if (Asset.IsRedirector())
    {
        OutError = {TEXT("unsupported_asset"), TEXT("Redirectors cannot be deleted by asset_delete")};
        return false;
    }
    if (Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
    {
        return UnrealMCPAssetDeletionPrivate::DeleteMap(
            References, Asset, AssetPath, ExpectedSnapshot, OutResult, OutError);
    }
    if (UnrealMCPAssetDeletionPrivate::IsCurrentMapPackage(PackageName))
    {
        OutError = {TEXT("unsupported_asset"), TEXT("Current-world packages are outside ordinary asset_delete scope")};
        return false;
    }
    if (PackageName.Contains(TEXT("/__ExternalActors__/"))
        || PackageName.Contains(TEXT("/__ExternalObjects__/")))
    {
        OutError = {
            TEXT("unsupported_asset"),
            TEXT("Generated external actor/object packages cannot be deleted directly")};
        return false;
    }
    TArray<FAssetData> PackageAssets;
    Registry.GetAssetsByPackageName(Asset.PackageName, PackageAssets, false);
    if (PackageAssets.Num() != 1 || PackageAssets[0].GetObjectPathString() != AssetPath)
    {
        OutError = {
            TEXT("multi_asset_package"),
            TEXT("asset_delete requires a package containing exactly the requested asset")};
        return false;
    }

    FString PackageFilename;
    if (!FPackageName::DoesPackageExist(PackageName, &PackageFilename)
        || PackageFilename.IsEmpty())
    {
        OutError = {
            TEXT("storage_unavailable"),
            TEXT("The target package has no verifiable persisted storage")};
        return false;
    }
    if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*PackageFilename)
        == ESymlinkResult::Symlink)
    {
        OutError = {
            TEXT("mutation_scope_denied"),
            TEXT("The target package file is a symlink")};
        return false;
    }
    if (IFileManager::Get().IsReadOnly(*PackageFilename))
    {
        OutError = {
            TEXT("write_conflict"),
            TEXT("The target package file is read-only; no permission change was attempted")};
        return false;
    }

    UObject* Target = Asset.GetAsset();
    UPackage* Package = Target != nullptr ? Target->GetOutermost() : nullptr;
    if (Target == nullptr || Package == nullptr
        || Target->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject)
        || Package == GetTransientPackage()
        || Package->HasAnyPackageFlags(PKG_CompiledIn | PKG_PlayInEditor | PKG_ContainsScript)
        || Package->ContainsMap())
    {
        OutError = {
            TEXT("unsupported_asset"),
            TEXT("The target resolved to transient, generated, script, or map content")};
        return false;
    }
    if (Package->IsDirty())
    {
        OutError = {
            TEXT("dirty_package"),
            TEXT("The target package has unsaved changes and was not modified")};
        return false;
    }
    UAssetEditorSubsystem* Editors =
        GEditor != nullptr ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (Editors != nullptr && !Editors->FindEditorsForAsset(Target).IsEmpty())
    {
        OutError = {
            TEXT("asset_editor_open"),
            TEXT("Close the exact target asset editor before deletion")};
        return false;
    }

    FUnrealMCPAssetReferenceSnapshot Final;
    bool bLiveScanComplete = false;
    if (!References.Capture(AssetPath, Final, OutError)
        || !UnrealMCPAssetDeletionPrivate::ReferenceScansSufficient(
            Final,
            bLiveScanComplete,
            OutError))
    {
        return false;
    }
    if (!Final.Records.IsEmpty())
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("reference_count"), Final.Records.Num());
        Details->SetStringField(TEXT("snapshot_id"), Final.SnapshotId);
        OutError = {
            TEXT("asset_referenced"),
            TEXT("The target gained or retained serialized or live-memory references"),
            Details};
        return false;
    }
    bool bReferenced = false;
    bool bReferencedByUndo = false;
    ObjectTools::GatherObjectReferencersForDeletion(
        Target, bReferenced, bReferencedByUndo, nullptr, false);
    if (bReferenced || bReferencedByUndo)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetBoolField(TEXT("referenced"), bReferenced);
        Details->SetBoolField(TEXT("referenced_by_undo"), bReferencedByUndo);
        OutError = {
            TEXT("asset_referenced"),
            TEXT("Unreal's deletion reference check found retained memory or Undo references"),
            Details};
        return false;
    }
    if (UnrealMCPAssetDeletionPrivate::HasUnsafeEditorWork(OutError)
        || Package->IsDirty())
    {
        if (Package->IsDirty())
        {
            OutError = {
                TEXT("dirty_package"),
                TEXT("The target package became dirty during deletion preflight")};
        }
        return false;
    }

    if (!ObjectTools::DeleteSingleObject(Target, false))
    {
        OutError = {
            TEXT("delete_failed"),
            TEXT("Unreal's supported single-object delete API refused the target")};
        return false;
    }
    ObjectTools::CleanupAfterSuccessfulDelete({Package}, false);

    const bool bRegistryAbsent =
        !Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid()
        && [&Registry, &Asset]()
        {
            TArray<FAssetData> Remaining;
            Registry.GetAssetsByPackageName(Asset.PackageName, Remaining, true);
            return Remaining.IsEmpty();
        }();
    FString RemainingFilename;
    const bool bStorageAbsent =
        !FPackageName::DoesPackageExist(PackageName, &RemainingFilename)
        && !IFileManager::Get().FileExists(*PackageFilename);
    OutResult = UnrealMCPAssetDeletionPrivate::BuildResult(
        AssetPath,
        PackageName,
        ExpectedSnapshot,
        Final.SnapshotId,
        bRegistryAbsent,
        bStorageAbsent,
        bLiveScanComplete,
        bRegistryAbsent && bStorageAbsent ? TEXT("committed") : TEXT("partial"));
    return true;
}
