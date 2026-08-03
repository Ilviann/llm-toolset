#include "UnrealMCPLevelManagementService.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Factories/WorldFactory.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "WorldPartition/WorldPartition.h"

namespace UnrealMCPLevelManagementPrivate
{
const TSet<FString> AllowedWorldSettings = {
    TEXT("DefaultGameMode"), TEXT("bEnableWorldBoundsChecks"), TEXT("bEnableAISystem"),
    TEXT("bUseClientSideLevelStreamingVolumes"), TEXT("bEnableWorldOriginRebasing"),
    TEXT("bGlobalGravitySet"), TEXT("GlobalGravityZ"), TEXT("WorldToMeters"), TEXT("KillZ"),
    TEXT("KillZDamageType"), TEXT("DefaultPhysicsVolumeClass"),
    TEXT("PhysicsCollisionHandlerClass"), TEXT("DefaultColorScale"),
    TEXT("PackedLightAndShadowMapTextureSize"), TEXT("bForceNoPrecomputedLighting"),
    TEXT("DefaultMaxDistanceFieldOcclusionDistance")};

bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key)) return false;
    }
    return true;
}

bool IsLowerHex(const FString& Value, int32 Length)
{
    if (Value.Len() != Length) return false;
    for (const TCHAR Character : Value)
    {
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character)) return false;
    }
    return true;
}

FString HashText(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

bool ReadExactMapPath(
    const FJsonObject& Arguments,
    const TCHAR* Field,
    FString& OutObjectPath,
    FString& OutPackageName,
    FString& OutAssetName,
    FUnrealMCPError& OutError)
{
    if (!Arguments.TryGetStringField(Field, OutObjectPath)
        || OutObjectPath.Len() < 3 || OutObjectPath.Len() > 512
        || !OutObjectPath.StartsWith(TEXT("/")) || OutObjectPath.Contains(TEXT(".."))
        || OutObjectPath.Contains(TEXT("\\")) || OutObjectPath.Contains(TEXT(":")))
    {
        OutError = {TEXT("invalid_argument"), FString(Field) + TEXT(" must be one exact mounted World object path")};
        return false;
    }
    OutPackageName = FPackageName::ObjectPathToPackageName(OutObjectPath);
    OutAssetName = FPackageName::ObjectPathToObjectName(OutObjectPath);
    if (!FPackageName::IsValidLongPackageName(OutPackageName)
        || OutAssetName.IsEmpty()
        || FPackageName::GetLongPackageAssetName(OutPackageName) != OutAssetName
        || OutObjectPath != OutPackageName + TEXT(".") + OutAssetName)
    {
        OutError = {TEXT("invalid_argument"), FString(Field) + TEXT(" must use matching exact package and object names")};
        return false;
    }
    return true;
}

bool PathContainsSymlink(const FString& Root, const FString& Candidate)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FString Current = Root;
    FString Relative = Candidate;
    FPaths::NormalizeDirectoryName(Current);
    FPaths::NormalizeDirectoryName(Relative);
    if (PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink
        || !FPaths::MakePathRelativeTo(Relative, *(Current + TEXT("/")))) return true;
    TArray<FString> Segments;
    Relative.ParseIntoArray(Segments, TEXT("/"), true);
    for (const FString& Segment : Segments)
    {
        Current /= Segment;
        if ((PlatformFile.FileExists(*Current) || PlatformFile.DirectoryExists(*Current))
            && PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink) return true;
    }
    return false;
}

bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError)
{
    FString PhysicalTarget;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The destination mount is unavailable")};
        return false;
    }
    PhysicalTarget = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PhysicalTarget));
    FPaths::NormalizeDirectoryName(PhysicalTarget);
    if (PackageName.StartsWith(TEXT("/Game/")))
    {
        FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeDirectoryName(Root);
        if ((FPaths::IsSamePath(PhysicalTarget, Root) || FPaths::IsUnderDirectory(PhysicalTarget, Root))
            && !PathContainsSymlink(Root, PhysicalTarget)) return true;
        OutError = {TEXT("mutation_scope_denied"), TEXT("Project content must resolve inside its symlink-free mount")};
        return false;
    }
    const int32 Slash = PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    FString MountDirectory;
    FString Plugins = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
    if (Slash == INDEX_NONE
        || !FPackageName::TryConvertLongPackageNameToFilename(PackageName.Left(Slash + 1), MountDirectory))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("Only project and local project-plugin maps are mutable")};
        return false;
    }
    MountDirectory = FPaths::ConvertRelativePathToFull(MountDirectory);
    FPaths::NormalizeDirectoryName(MountDirectory);
    FPaths::NormalizeDirectoryName(Plugins);
    if (!FPaths::IsUnderDirectory(MountDirectory, Plugins)
        || !FPaths::IsUnderDirectory(PhysicalTarget, MountDirectory)
        || PathContainsSymlink(Plugins, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The plugin map destination is not in a symlink-free local project plugin")};
        return false;
    }
    FString Candidate = MountDirectory;
    while (FPaths::IsUnderDirectory(Candidate, Plugins))
    {
        TArray<FString> Descriptors;
        IFileManager::Get().FindFiles(Descriptors, *(Candidate / TEXT("*.uplugin")), true, false);
        if (!Descriptors.IsEmpty()) return true;
        const FString Parent = FPaths::GetPath(Candidate);
        if (Parent == Candidate) break;
        Candidate = Parent;
    }
    OutError = {TEXT("mutation_scope_denied"), TEXT("The destination mount is not owned by a local project plugin")};
    return false;
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
    const bool bLoading = IsAsyncLoading();
    if (!bPlaying && !bSimulating && !bSaving && !bCollecting && !bTransaction
        && !bUndoRedo && !bCompiling && !bLoading) return false;
    const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetBoolField(TEXT("is_playing"), bPlaying);
    Details->SetBoolField(TEXT("is_simulating"), bSimulating);
    Details->SetBoolField(TEXT("is_saving"), bSaving);
    Details->SetBoolField(TEXT("is_garbage_collecting"), bCollecting);
    Details->SetBoolField(TEXT("transaction_active"), bTransaction);
    Details->SetBoolField(TEXT("undo_redo_active"), bUndoRedo);
    Details->SetBoolField(TEXT("is_compiling"), bCompiling);
    Details->SetBoolField(TEXT("is_async_loading"), bLoading);
    OutError = {TEXT("busy"), TEXT("Level management refused while unsafe editor work is active"), Details, true};
    return true;
}

bool CurrentState(
    FUnrealMCPLevelService& Levels,
    FString& OutPath,
    FString& OutSnapshot,
    TSharedPtr<FJsonObject>& OutRecord,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("current"));
    TSharedPtr<FJsonObject> Result;
    if (!Levels.Inspect(Arguments, Result, OutError)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1)
    {
        OutError = {TEXT("invalid_response"), TEXT("Current-map inspection did not return one exact record")};
        return false;
    }
    OutRecord = (*Records)[0]->AsObject();
    if (!OutRecord.IsValid())
    {
        OutError = {TEXT("invalid_response"), TEXT("Current-map record is unavailable")};
        return false;
    }
    OutPath = OutRecord->GetStringField(TEXT("map_path"));
    OutSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    return true;
}

bool RequireCleanCurrent(
    FUnrealMCPLevelService& Levels,
    const FString& ExpectedSnapshot,
    FString& OutPath,
    TSharedPtr<FJsonObject>& OutRecord,
    FUnrealMCPError& OutError)
{
    FString ActualSnapshot;
    if (!CurrentState(Levels, OutPath, ActualSnapshot, OutRecord, OutError)) return false;
    if (ActualSnapshot != ExpectedSnapshot)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("expected_snapshot"), ExpectedSnapshot);
        Details->SetStringField(TEXT("actual_snapshot"), ActualSnapshot);
        OutError = {TEXT("stale_precondition"), TEXT("The current-map snapshot changed before level management"), Details};
        return false;
    }
    if (OutRecord->GetBoolField(TEXT("dirty")) || !OutRecord->GetBoolField(TEXT("dirty_state_complete")))
    {
        OutError = {TEXT("dirty_map"), TEXT("Level management requires a completely inspected clean current map")};
        return false;
    }
    return true;
}

bool ReadSettings(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonObject>>& OutSettings,
    FUnrealMCPError& OutError)
{
    if (!Arguments.HasField(TEXT("settings"))) return true;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Arguments.TryGetArrayField(TEXT("settings"), Values) || Values == nullptr
        || Values->IsEmpty() || Values->Num() > UnrealMCP::MaxLevelSetupProperties)
    {
        OutError = {TEXT("invalid_argument"), TEXT("settings must contain between 1 and 16 property assignments")};
        return false;
    }
    TSet<FString> Seen;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        const TSharedPtr<FJsonObject> Setting = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Name;
        if (!Setting.IsValid() || !HasOnlyFields(*Setting, {TEXT("property_name"), TEXT("value")})
            || !Setting->TryGetStringField(TEXT("property_name"), Name)
            || !Setting->HasField(TEXT("value")) || Seen.Contains(Name)
            || !AllowedWorldSettings.Contains(Name))
        {
            OutError = {TEXT("unsupported_property"), TEXT("settings contains a duplicate or non-allowlisted World Settings property")};
            return false;
        }
        Seen.Add(Name);
        OutSettings.Add(Setting);
    }
    return true;
}

bool ApplySettings(
    UWorld* World,
    const TArray<TSharedPtr<FJsonObject>>& Settings,
    bool bTransactional,
    TArray<TSharedPtr<FJsonValue>>& OutChanged,
    FUnrealMCPError& OutError)
{
    AWorldSettings* Target = World != nullptr ? World->GetWorldSettings() : nullptr;
    if (Target == nullptr)
    {
        OutError = {TEXT("level_setup_failed"), TEXT("The map has no writable World Settings object")};
        return false;
    }
    struct FSettingBackup
    {
        FString Name;
        TSharedPtr<FJsonValue> Value;
    };
    TArray<FSettingBackup> Backups;
    Backups.Reserve(Settings.Num());
    for (const TSharedPtr<FJsonObject>& Setting : Settings)
    {
        const FString Name = Setting->GetStringField(TEXT("property_name"));
        FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*Name));
        FString Kind;
        if (!UnrealMCP::PropertyCodec::IsSupportedEditable(Property, Kind))
        {
            OutError = {TEXT("unsupported_property"), TEXT("The allowlisted property is unavailable in this Unreal build")};
            return false;
        }
        const TSharedRef<FJsonObject> Encoded = UnrealMCP::PropertyCodec::Encode(Target, Property);
        const TSharedPtr<FJsonValue> PreviousValue = Encoded->TryGetField(TEXT("value"));
        if (!PreviousValue.IsValid())
        {
            OutError = {TEXT("property_read_failed"), TEXT("An allowlisted World Settings property could not be snapshotted before mutation")};
            return false;
        }
        Backups.Add({Name, PreviousValue});
    }
    UPackage* Package = Target->GetPackage();
    const bool bWasDirty = Package != nullptr && Package->IsDirty();
    TUniquePtr<FScopedTransaction> Transaction;
    if (bTransactional)
    {
        Transaction = MakeUnique<FScopedTransaction>(
            NSLOCTEXT("UnrealMCP", "LevelSetup", "Configure level World Settings"));
        Target->Modify();
    }
    for (const TSharedPtr<FJsonObject>& Setting : Settings)
    {
        TSharedPtr<FJsonObject> Changed;
        if (!UnrealMCP::PropertyCodec::Set(
            Target,
            Setting->GetStringField(TEXT("property_name")),
            Setting->TryGetField(TEXT("value")),
            Changed,
            OutError))
        {
            const FUnrealMCPError MutationError = OutError;
            bool bRestored = true;
            for (int32 Index = Backups.Num() - 1; Index >= 0; --Index)
            {
                TSharedPtr<FJsonObject> Ignored;
                FUnrealMCPError RestoreError;
                if (!UnrealMCP::PropertyCodec::Set(
                    Target, Backups[Index].Name, Backups[Index].Value, Ignored, RestoreError))
                {
                    bRestored = false;
                }
            }
            if (Transaction) Transaction->Cancel();
            if (Package != nullptr) Package->SetDirtyFlag(bWasDirty);
            OutChanged.Reset();
            OutError = bRestored
                ? MutationError
                : FUnrealMCPError{TEXT("rollback_failed"), TEXT("World Settings mutation failed and exact pre-mutation values could not all be restored")};
            return false;
        }
        OutChanged.Add(MakeShared<FJsonValueObject>(Changed.ToSharedRef()));
    }
    return true;
}

bool VerifySettingsReadback(
    UWorld* World,
    const TArray<TSharedPtr<FJsonValue>>& Expected,
    TArray<TSharedPtr<FJsonValue>>& OutReadback,
    FUnrealMCPError& OutError)
{
    AWorldSettings* Target = World != nullptr ? World->GetWorldSettings() : nullptr;
    if (Target == nullptr)
    {
        OutError = {TEXT("reload_failed"), TEXT("Reloaded map has no World Settings object")};
        return false;
    }
    for (const TSharedPtr<FJsonValue>& ExpectedValue : Expected)
    {
        const TSharedPtr<FJsonObject> ExpectedRecord = ExpectedValue.IsValid() ? ExpectedValue->AsObject() : nullptr;
        if (!ExpectedRecord.IsValid()) return false;
        FProperty* Property = Target->GetClass()->FindPropertyByName(
            FName(*ExpectedRecord->GetStringField(TEXT("name"))));
        const TSharedRef<FJsonObject> Actual = UnrealMCP::PropertyCodec::Encode(Target, Property);
        const TSharedPtr<FJsonValue> ExpectedPropertyValue = ExpectedRecord->TryGetField(TEXT("value"));
        const TSharedPtr<FJsonValue> ActualPropertyValue = Actual->TryGetField(TEXT("value"));
        if (!ExpectedPropertyValue.IsValid() || !ActualPropertyValue.IsValid()
            || !FJsonValue::CompareEqual(*ExpectedPropertyValue, *ActualPropertyValue))
        {
            OutError = {TEXT("reload_verification_failed"), TEXT("A World Settings property changed across save and reload")};
            return false;
        }
        OutReadback.Add(MakeShared<FJsonValueObject>(Actual));
    }
    return true;
}

TSharedRef<FJsonObject> EffectiveFacts(UWorld* World)
{
    const TSharedRef<FJsonObject> Facts = MakeShared<FJsonObject>();
    const UWorldPartition* Partition = World != nullptr ? World->GetWorldPartition() : nullptr;
    Facts->SetBoolField(TEXT("world_partition"), Partition != nullptr);
    Facts->SetBoolField(TEXT("world_partition_streaming"), Partition != nullptr && Partition->IsStreamingEnabled());
    Facts->SetBoolField(TEXT("external_actors"), World != nullptr && World->PersistentLevel != nullptr
        && World->PersistentLevel->IsUsingExternalActors());
    Facts->SetStringField(TEXT("streaming_topology"), Partition != nullptr ? TEXT("world_partition") : TEXT("persistent_level"));
    Facts->SetBoolField(TEXT("post_creation_conversion_supported"), false);
    return Facts;
}

bool SaveAndVerify(
    UWorld* World,
    const FString& PackageName,
    const FString& ObjectPath,
    TArray<TSharedPtr<FJsonValue>>& OutPersistence,
    bool& OutSaveSucceeded,
    FUnrealMCPError& OutError)
{
    const bool bRootSaved = UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
    UPackage* RootPackage = World != nullptr ? World->GetPackage() : nullptr;
    TSet<UPackage*> OwnedPackages;
    if (RootPackage != nullptr) OwnedPackages.Add(RootPackage);
    if (World != nullptr && World->PersistentLevel != nullptr)
    {
        for (UPackage* External : World->PersistentLevel->GetLoadedExternalObjectPackages())
        {
            if (External != nullptr) OwnedPackages.Add(External);
        }
    }
    TArray<UObject*> ExtraObjects{World};
    ObjectTools::AddExtraObjectsToDelete(ExtraObjects);
    for (UObject* Extra : ExtraObjects)
    {
        if (Extra != nullptr && Extra->GetOutermost() != nullptr) OwnedPackages.Add(Extra->GetOutermost());
    }
    if (OwnedPackages.Num() > UnrealMCP::MaxLevelOwnedPackages)
    {
        OutError = {TEXT("package_closure_truncated"), TEXT("The map-owned save package set exceeds the published bound")};
        OutSaveSucceeded = false;
        return false;
    }
    TArray<UPackage*> AuxiliaryPackages;
    for (UPackage* Owned : OwnedPackages)
    {
        if (Owned != nullptr && Owned != RootPackage) AuxiliaryPackages.Add(Owned);
    }
    const bool bAuxiliarySaved = AuxiliaryPackages.IsEmpty()
        || UEditorLoadingAndSavingUtils::SavePackages(AuxiliaryPackages, false);
    OutSaveSucceeded = bRootSaved && bAuxiliarySaved;
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    bool bAllRegistry = true;
    bool bAllStorage = true;
    bool bAllClean = true;
    TArray<UPackage*> SortedPackages = OwnedPackages.Array();
    SortedPackages.Sort([](const UPackage& Left, const UPackage& Right)
    {
        return Left.GetName() < Right.GetName();
    });
    for (UPackage* Owned : SortedPackages)
    {
        const FString OwnedName = Owned->GetName();
        const bool bRoot = OwnedName == PackageName;
        bool bRegistry = false;
        if (bRoot)
        {
            const FAssetData Saved = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
            bRegistry = Saved.IsValid() && Saved.GetObjectPathString() == ObjectPath;
        }
        else
        {
            TArray<FAssetData> PackageAssets;
            Registry.GetAssetsByPackageName(FName(*OwnedName), PackageAssets, true);
            bRegistry = !PackageAssets.IsEmpty();
        }
        FString Filename;
        const bool bStorage = FPackageName::DoesPackageExist(OwnedName, &Filename)
            && IFileManager::Get().FileExists(*Filename);
        const bool bClean = !Owned->IsDirty();
        bAllRegistry &= bRegistry;
        bAllStorage &= bStorage;
        bAllClean &= bClean;
        const TSharedRef<FJsonObject> PackageResult = MakeShared<FJsonObject>();
        PackageResult->SetStringField(TEXT("package_name"), OwnedName);
        PackageResult->SetBoolField(TEXT("required"), true);
        PackageResult->SetBoolField(TEXT("save_succeeded"), bRoot ? bRootSaved : bAuxiliarySaved);
        PackageResult->SetBoolField(TEXT("registry_present"), bRegistry);
        PackageResult->SetBoolField(TEXT("storage_present"), bStorage);
        PackageResult->SetBoolField(TEXT("clean"), bClean);
        OutPersistence.Add(MakeShared<FJsonValueObject>(PackageResult));
    }
    if (!OutSaveSucceeded)
    {
        OutError = {TEXT("save_failed"), TEXT("Unreal could not save every exact map-owned package")};
        return false;
    }
    if (!bAllRegistry || !bAllStorage || !bAllClean)
    {
        OutError = {TEXT("partial_persistence"), TEXT("Map save completed but registry, storage, or dirty-state verification disagreed")};
        return false;
    }
    return true;
}

void SetIdentity(
    TSharedRef<FJsonObject> Result,
    const FString& ProjectHash,
    const FString& MapPath,
    UWorld* World)
{
    const FString PackageName = FPackageName::ObjectPathToPackageName(MapPath);
    FString Persistent = World != nullptr && World->GetPackage() != nullptr
        ? World->GetPackage()->GetPersistentGuid().ToString(EGuidFormats::Digits).ToLower()
        : FString();
    const TOptional<FAssetPackageData> Data =
        FAssetRegistryModule::GetRegistry().GetAssetPackageDataCopy(FName(*PackageName));
    if (Data.IsSet() && !Data->GetPackageSavedHash().IsZero()) Persistent = LexToString(Data->GetPackageSavedHash());
    const FString Revision = HashText(PackageName + TEXT("|") + Persistent + TEXT("|clean"));
    Result->SetStringField(TEXT("map_id"), HashText(ProjectHash + TEXT("|") + MapPath));
    Result->SetStringField(TEXT("map_revision"), Revision);
    Result->SetStringField(TEXT("snapshot_id"), HashText(TEXT("managed|") + MapPath + TEXT("|") + Revision));
}

bool DiscardUnpersistedWorld(
    UWorld* World,
    const FString& MapPath,
    const FString& PackageName,
    FUnrealMCPError& OutError)
{
    if (World == nullptr) return true;
    if (World->GetPackage() != nullptr) World->GetPackage()->SetDirtyFlag(false);
    const TArray<FAssetData> Assets{FAssetData(World)};
    ObjectTools::DeleteAssets(Assets, false);
    FString Filename;
    if (FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(MapPath)).IsValid()
        || FPackageName::DoesPackageExist(PackageName, &Filename))
    {
        OutError = {TEXT("rollback_failed"), TEXT("Rejected map creation could not remove its unpersisted destination")};
        return false;
    }
    return true;
}
}

using namespace UnrealMCPLevelManagementPrivate;

FUnrealMCPLevelManagementService::FUnrealMCPLevelManagementService(
    FString InProjectHash,
    FUnrealMCPLevelService& InLevels)
    : ProjectHash(MoveTemp(InProjectHash)), Levels(InLevels)
{
}

bool FUnrealMCPLevelManagementService::Manage(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    FString Operation;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || (Operation != TEXT("create") && Operation != TEXT("configure")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_manage operation must be create or configure")};
        return false;
    }
    if (HasUnsafeEditorWork(OutError)) return false;
    FString ExpectedSnapshot;
    if (!Arguments->TryGetStringField(TEXT("expected_current_snapshot"), ExpectedSnapshot)
        || !UnrealMCPLevelManagementPrivate::IsLowerHex(ExpectedSnapshot, 40))
    {
        OutError = {TEXT("invalid_argument"), TEXT("expected_current_snapshot must be a 40-character lowercase hexadecimal snapshot")};
        return false;
    }
    FString CurrentPath;
    TSharedPtr<FJsonObject> CurrentRecord;
    if (!RequireCleanCurrent(Levels, ExpectedSnapshot, CurrentPath, CurrentRecord, OutError)) return false;
    TArray<TSharedPtr<FJsonObject>> Settings;
    if (!ReadSettings(*Arguments, Settings, OutError)) return false;

    FString MapPath;
    FString PackageName;
    FString AssetName;
    UWorld* World = nullptr;
    bool bOpened = false;
    FString SourceKind;
    FString SourcePath;
    if (Operation == TEXT("create"))
    {
        if (!UnrealMCPLevelManagementPrivate::HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("operation"), TEXT("destination_path"),
            TEXT("source"), TEXT("creation_options"), TEXT("settings"), TEXT("open_after_create"),
            TEXT("expected_current_snapshot")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("level_manage create contains unsupported fields")};
            return false;
        }
        bool bOpenAfterCreate = false;
        const TSharedPtr<FJsonObject>* Source = nullptr;
        if (!ReadExactMapPath(*Arguments, TEXT("destination_path"), MapPath, PackageName, AssetName, OutError)
            || !UnrealMCPLevelManagementPrivate::ValidateMutationScope(PackageName, OutError)
            || !Arguments->TryGetBoolField(TEXT("open_after_create"), bOpenAfterCreate)
            || !Arguments->TryGetObjectField(TEXT("source"), Source) || Source == nullptr || !Source->IsValid()
            || !(*Source)->TryGetStringField(TEXT("kind"), SourceKind)) return false;
        IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
        FString ExistingFilename;
        if (Registry.GetAssetByObjectPath(FSoftObjectPath(MapPath)).IsValid()
            || FPackageName::DoesPackageExist(PackageName, &ExistingFilename))
        {
            OutError = {TEXT("already_exists"), TEXT("The exact destination package already exists")};
            return false;
        }
        if (SourceKind == TEXT("blank"))
        {
            const TSharedPtr<FJsonObject>* Options = nullptr;
            bool bPartition = false;
            bool bStreaming = false;
            bool bExternalActors = false;
            if (!UnrealMCPLevelManagementPrivate::HasOnlyFields(**Source, {TEXT("kind")})
                || !Arguments->TryGetObjectField(TEXT("creation_options"), Options) || Options == nullptr
                || !Options->IsValid()
                || !UnrealMCPLevelManagementPrivate::HasOnlyFields(**Options, {TEXT("world_partition"), TEXT("world_partition_streaming"), TEXT("external_actors")})
                || !(*Options)->TryGetBoolField(TEXT("world_partition"), bPartition)
                || !(*Options)->TryGetBoolField(TEXT("world_partition_streaming"), bStreaming)
                || !(*Options)->TryGetBoolField(TEXT("external_actors"), bExternalActors)
                || (bStreaming && !bPartition) || (bPartition && !bExternalActors))
            {
                OutError = {TEXT("invalid_argument"), TEXT("Blank creation requires explicit compatible World Partition, streaming, and external-actor options")};
                return false;
            }
            UPackage* Package = CreatePackage(*PackageName);
            UWorldFactory* Factory = NewObject<UWorldFactory>();
            Factory->WorldType = EWorldType::Inactive;
            Factory->bInformEngineOfWorld = false;
            Factory->bCreateWorldPartition = bPartition;
            Factory->bEnableWorldPartitionStreaming = bStreaming;
            World = Cast<UWorld>(Factory->FactoryCreateNew(
                UWorld::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone, nullptr, GWarn));
            if (World != nullptr && World->PersistentLevel != nullptr)
            {
                World->PersistentLevel->SetUseExternalActors(bExternalActors);
            }
        }
        else if (SourceKind == TEXT("template"))
        {
            FString TemplatePackage;
            FString TemplateAssetName;
            if (Arguments->HasField(TEXT("creation_options"))
                || !UnrealMCPLevelManagementPrivate::HasOnlyFields(**Source, {TEXT("kind"), TEXT("map_path")})
                || !ReadExactMapPath(**Source, TEXT("map_path"), SourcePath, TemplatePackage, TemplateAssetName, OutError)) return false;
            const FAssetData TemplateAsset = Registry.GetAssetByObjectPath(FSoftObjectPath(SourcePath));
            UWorld* Template = TemplateAsset.IsValid() && TemplateAsset.AssetClassPath == UWorld::StaticClass()->GetClassPathName()
                ? Cast<UWorld>(TemplateAsset.GetAsset()) : nullptr;
            if (Template == nullptr || TemplateAsset.GetObjectPathString() != SourcePath)
            {
                OutError = {TEXT("not_found"), TEXT("The exact clean World template was not found")};
                return false;
            }
            if (Template->GetPackage() == nullptr || Template->GetPackage()->IsDirty())
            {
                OutError = {TEXT("dirty_template"), TEXT("Template creation requires an exact clean mounted map")};
                return false;
            }
            World = Cast<UWorld>(FAssetToolsModule::GetModule().Get().DuplicateAsset(
                AssetName, FPackageName::GetLongPackagePath(PackageName), Template));
        }
        else
        {
            OutError = {TEXT("invalid_argument"), TEXT("source.kind must be blank or template")};
            return false;
        }
        if (World == nullptr || World->GetPathName() != MapPath)
        {
            OutError = {TEXT("create_failed"), TEXT("Unreal could not create the exact destination World")};
            return false;
        }
        if (!Registry.GetAssetByObjectPath(FSoftObjectPath(MapPath)).IsValid())
        {
            FAssetRegistryModule::AssetCreated(World);
        }
        TArray<TSharedPtr<FJsonValue>> Changed;
        if (!ApplySettings(World, Settings, false, Changed, OutError))
        {
            const FUnrealMCPError MutationError = OutError;
            if (!DiscardUnpersistedWorld(World, MapPath, PackageName, OutError)) return false;
            OutError = MutationError;
            return false;
        }
        TArray<TSharedPtr<FJsonValue>> Persistence;
        const TSharedRef<FJsonObject> CreationFacts = EffectiveFacts(World);
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("operation"), Operation);
        Result->SetStringField(TEXT("map_path"), MapPath);
        Result->SetStringField(TEXT("package_name"), PackageName);
        Result->SetStringField(TEXT("source_kind"), SourceKind);
        Result->SetStringField(TEXT("template_path"), SourcePath);
        Result->SetArrayField(TEXT("changed_properties"), Changed);
        Result->SetObjectField(TEXT("effective_creation"), CreationFacts);
        Result->SetBoolField(TEXT("reload_verified"), false);
        Result->SetBoolField(TEXT("opened"), false);
        SetIdentity(Result, ProjectHash, MapPath, World);
        const auto CompletePartial = [&](const FUnrealMCPError& Failure)
        {
            const TSharedRef<FJsonObject> VerificationError = MakeShared<FJsonObject>();
            VerificationError->SetStringField(TEXT("code"), Failure.Code);
            VerificationError->SetStringField(TEXT("message"), Failure.Message);
            Result->SetStringField(TEXT("operation_state"), TEXT("partial"));
            Result->SetObjectField(TEXT("verification_error"), VerificationError);
            Result->SetBoolField(TEXT("current_map_preserved"), false);
            OutResult = Result;
            return true;
        };
        bool bSaveSucceeded = false;
        if (!SaveAndVerify(World, PackageName, MapPath, Persistence, bSaveSucceeded, OutError))
        {
            Result->SetArrayField(TEXT("package_persistence"), Persistence);
            Result->SetBoolField(TEXT("saved"), bSaveSucceeded);
            return CompletePartial(OutError);
        }
        Result->SetArrayField(TEXT("package_persistence"), Persistence);
        Result->SetBoolField(TEXT("saved"), true);
        bool bReloadVerified = false;
        bool bInactiveReloadUnloaded = false;
        TArray<TSharedPtr<FJsonValue>> ReloadedProperties;
        if (!bOpenAfterCreate)
        {
            UPackage* CreatedPackage = World->GetPackage();
            UPackageTools::FUnloadPackageParams Unload({CreatedPackage});
            Unload.bUnloadDirtyPackages = false;
            Unload.bResetTransBuffer = false;
            if (!UPackageTools::UnloadPackages(Unload))
            {
                OutError = {TEXT("reload_failed"), TEXT("The clean created map could not be unloaded for persistence verification")};
                return CompletePartial(OutError);
            }
            UPackage* ReloadedPackage = LoadPackage(nullptr, *PackageName, LOAD_None);
            World = ReloadedPackage != nullptr ? FindObject<UWorld>(ReloadedPackage, *AssetName) : nullptr;
            if (World == nullptr)
            {
                OutError = {TEXT("reload_failed"), TEXT("The persisted map package could not be reloaded")};
                return CompletePartial(OutError);
            }
            if (!VerifySettingsReadback(World, Changed, ReloadedProperties, OutError)) return CompletePartial(OutError);
            SetIdentity(Result, ProjectHash, MapPath, World);
            UPackageTools::FUnloadPackageParams UnloadReloaded({ReloadedPackage});
            UnloadReloaded.bUnloadDirtyPackages = false;
            UnloadReloaded.bResetTransBuffer = false;
            if (!UPackageTools::UnloadPackages(UnloadReloaded))
            {
                OutError = {
                    TEXT("reload_failed"),
                    TEXT("The verified inactive map could not be released after persistence read-back")};
                return CompletePartial(OutError);
            }
            World = nullptr;
            bInactiveReloadUnloaded = true;
            bReloadVerified = true;
        }
        Result->SetArrayField(TEXT("changed_properties"), bReloadVerified && !bOpenAfterCreate ? ReloadedProperties : Changed);
        Result->SetBoolField(TEXT("reload_verified"), bReloadVerified);
        if (bOpenAfterCreate)
        {
            const TSharedRef<FJsonObject> OpenArguments = MakeShared<FJsonObject>();
            OpenArguments->SetStringField(TEXT("operation_id"), Arguments->GetStringField(TEXT("operation_id")));
            OpenArguments->SetStringField(TEXT("map_path"), MapPath);
            TSharedPtr<FJsonObject> OpenResult;
            if (!Levels.Open(OpenArguments, OpenResult, OutError)) return CompletePartial(OutError);
            bOpened = OpenResult->GetBoolField(TEXT("opened"));
            Result->SetStringField(TEXT("snapshot_id"), OpenResult->GetStringField(TEXT("snapshot_id")));
            Result->SetObjectField(TEXT("current_map"), OpenResult->GetObjectField(TEXT("current_map")));
            World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (!VerifySettingsReadback(World, Changed, ReloadedProperties, OutError)) return CompletePartial(OutError);
            Result->SetArrayField(TEXT("changed_properties"), ReloadedProperties);
            bReloadVerified = true;
            Result->SetBoolField(TEXT("reload_verified"), true);
        }
        else
        {
            if (!bInactiveReloadUnloaded)
            {
                SetIdentity(Result, ProjectHash, MapPath, World);
            }
        }
        Result->SetBoolField(TEXT("opened"), bOpened);
        bool bCurrentPreserved = false;
        if (!bOpenAfterCreate)
        {
            FString VerifiedCurrentPath;
            FString VerifiedCurrentSnapshot;
            TSharedPtr<FJsonObject> VerifiedCurrent;
            if (!CurrentState(Levels, VerifiedCurrentPath, VerifiedCurrentSnapshot, VerifiedCurrent, OutError)
                || VerifiedCurrentPath != CurrentPath || VerifiedCurrentSnapshot != ExpectedSnapshot)
            {
                if (OutError.Code.IsEmpty())
                {
                    OutError = {TEXT("current_map_changed"), TEXT("The current map changed while creating an inactive map")};
                }
                return CompletePartial(OutError);
            }
            bCurrentPreserved = true;
        }
        Result->SetBoolField(TEXT("current_map_preserved"), bCurrentPreserved);
        OutResult = Result;
        return true;
    }

    if (!UnrealMCPLevelManagementPrivate::HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("operation"), TEXT("map_path"),
        TEXT("expected_current_snapshot"), TEXT("settings"), TEXT("reload_after_save")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_manage configure contains unsupported fields")};
        return false;
    }
    bool bReloadAfterSave = false;
    if (!Arguments->TryGetBoolField(TEXT("reload_after_save"), bReloadAfterSave)
        || !ReadExactMapPath(*Arguments, TEXT("map_path"), MapPath, PackageName, AssetName, OutError)
        || !UnrealMCPLevelManagementPrivate::ValidateMutationScope(PackageName, OutError)) return false;
    if (MapPath != CurrentPath)
    {
        OutError = {TEXT("not_current_map"), TEXT("configure applies only to the exact current map; use level_open explicitly first")};
        return false;
    }
    if (Settings.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("configure requires at least one allowlisted World Settings assignment")};
        return false;
    }
    World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    TArray<TSharedPtr<FJsonValue>> Changed;
    if (!ApplySettings(World, Settings, true, Changed, OutError)) return false;
    TArray<TSharedPtr<FJsonValue>> Persistence;
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("operation"), Operation);
    Result->SetStringField(TEXT("map_path"), MapPath);
    Result->SetStringField(TEXT("package_name"), PackageName);
    Result->SetArrayField(TEXT("changed_properties"), Changed);
    Result->SetObjectField(TEXT("effective_creation"), EffectiveFacts(World));
    Result->SetBoolField(TEXT("reload_verified"), false);
    Result->SetStringField(TEXT("precondition_snapshot"), ExpectedSnapshot);
    const auto CompletePartial = [&](const FUnrealMCPError& Failure, bool bSaved)
    {
        const TSharedRef<FJsonObject> VerificationError = MakeShared<FJsonObject>();
        VerificationError->SetStringField(TEXT("code"), Failure.Code);
        VerificationError->SetStringField(TEXT("message"), Failure.Message);
        Result->SetStringField(TEXT("operation_state"), TEXT("partial"));
        Result->SetObjectField(TEXT("verification_error"), VerificationError);
        Result->SetBoolField(TEXT("saved"), bSaved);
        FString PartialPath;
        FString PartialSnapshot;
        TSharedPtr<FJsonObject> PartialRecord;
        FUnrealMCPError InspectionError;
        if (CurrentState(Levels, PartialPath, PartialSnapshot, PartialRecord, InspectionError))
        {
            Result->SetStringField(TEXT("snapshot_id"), PartialSnapshot);
            Result->SetStringField(TEXT("map_id"), PartialRecord->GetStringField(TEXT("map_id")));
            Result->SetStringField(TEXT("map_revision"), PartialRecord->GetStringField(TEXT("map_revision")));
            Result->SetObjectField(TEXT("current_map"), PartialRecord.ToSharedRef());
        }
        OutResult = Result;
        return true;
    };
    bool bSaveSucceeded = false;
    if (!SaveAndVerify(World, PackageName, MapPath, Persistence, bSaveSucceeded, OutError))
    {
        Result->SetArrayField(TEXT("package_persistence"), Persistence);
        return CompletePartial(OutError, bSaveSucceeded);
    }
    Result->SetArrayField(TEXT("package_persistence"), Persistence);
    Result->SetBoolField(TEXT("saved"), true);
    if (bReloadAfterSave)
    {
        FString Filename;
        if (!FPackageName::DoesPackageExist(PackageName, &Filename)
            || !FEditorFileUtils::LoadMap(Filename, false, false))
        {
            OutError = {TEXT("reload_failed"), TEXT("The configured map could not be reloaded for persistence verification")};
            return CompletePartial(OutError, true);
        }
        World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
        TArray<TSharedPtr<FJsonValue>> Reloaded;
        if (!VerifySettingsReadback(World, Changed, Reloaded, OutError)) return CompletePartial(OutError, true);
        Changed = MoveTemp(Reloaded);
    }
    FString VerifiedPath;
    FString VerifiedSnapshot;
    TSharedPtr<FJsonObject> VerifiedRecord;
    if (!CurrentState(Levels, VerifiedPath, VerifiedSnapshot, VerifiedRecord, OutError)
        || VerifiedPath != MapPath || VerifiedRecord->GetBoolField(TEXT("dirty")))
    {
        OutError = {TEXT("partial_persistence"), TEXT("Configured map did not match clean current-map read-back")};
        return CompletePartial(OutError, true);
    }
    Result->SetStringField(TEXT("snapshot_id"), VerifiedSnapshot);
    Result->SetStringField(TEXT("map_id"), VerifiedRecord->GetStringField(TEXT("map_id")));
    Result->SetStringField(TEXT("map_revision"), VerifiedRecord->GetStringField(TEXT("map_revision")));
    Result->SetArrayField(TEXT("changed_properties"), Changed);
    Result->SetObjectField(TEXT("effective_creation"), EffectiveFacts(World));
    Result->SetObjectField(TEXT("current_map"), VerifiedRecord.ToSharedRef());
    Result->SetBoolField(TEXT("reload_verified"), bReloadAfterSave);
    OutResult = Result;
    return true;
}
