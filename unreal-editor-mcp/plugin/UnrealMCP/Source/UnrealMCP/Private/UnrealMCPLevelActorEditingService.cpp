#include "UnrealMCPLevelActorEditingService.h"

#include "AssetCompilingManager.h"
#include "Components/ActorComponent.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "UnrealMCPWireTypes.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"
#include "UnrealMCPLevelService.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/GarbageCollection.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHandle.h"

namespace UnrealMCPLevelActorEditingPrivate
{
bool EditHasOnlyFields(const FUnrealMCPRecord& Object, std::initializer_list<const TCHAR*> Allowed)
{
    if (Object.Values.Num() != static_cast<int32>(Allowed.size())) return false;
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Pair : Object.Values)
    {
        bool bFound = false;
        for (const TCHAR* Field : Allowed) bFound |= Pair.Key == Field;
        if (!bFound) return false;
    }
    return true;
}

bool EditHasAllowedFields(const FUnrealMCPRecord& Object, std::initializer_list<const TCHAR*> Allowed)
{
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Pair : Object.Values)
    {
        bool bFound = false;
        for (const TCHAR* Field : Allowed) bFound |= Pair.Key == Field;
        if (!bFound) return false;
    }
    return true;
}

bool EditPathContainsSymlink(const FString& Root, const FString& Candidate)
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

bool EditValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError)
{
    FString PhysicalTarget;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The current map mount is unavailable")};
        return false;
    }
    PhysicalTarget = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PhysicalTarget));
    FPaths::NormalizeDirectoryName(PhysicalTarget);
    if (PackageName.StartsWith(TEXT("/Game/")))
    {
        FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeDirectoryName(Root);
        if ((FPaths::IsSamePath(PhysicalTarget, Root) || FPaths::IsUnderDirectory(PhysicalTarget, Root))
            && !EditPathContainsSymlink(Root, PhysicalTarget)) return true;
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
        || EditPathContainsSymlink(Plugins, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The current map is not in a symlink-free local project plugin")};
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
    OutError = {TEXT("mutation_scope_denied"), TEXT("The current map mount is not owned by a local project plugin")};
    return false;
}

bool EditIsLowerHex(const FString& Value, int32 Length)
{
    if (Value.Len() != Length) return false;
    for (const TCHAR Character : Value)
    {
        if (!((Character >= TEXT('0') && Character <= TEXT('9'))
            || (Character >= TEXT('a') && Character <= TEXT('f')))) return false;
    }
    return true;
}

FString EditActorId(const FString& MapId, const FGuid& Guid)
{
    return MapId + TEXT(":") + Guid.ToString(EGuidFormats::Digits).ToLower();
}

bool ParseEditActorId(const FString& Value, const FString& MapId, FGuid& OutGuid)
{
    return Value.Len() == 73 && Value.Left(40) == MapId && Value[40] == TEXT(':')
        && EditIsLowerHex(Value.Mid(41), 32)
        && FGuid::ParseExact(Value.Mid(41), EGuidFormats::Digits, OutGuid);
}

FString EditStableId(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower().Left(32);
}

FString EditCreationMethodName(EComponentCreationMethod Method)
{
    switch (Method)
    {
    case EComponentCreationMethod::Native: return TEXT("native_default");
    case EComponentCreationMethod::SimpleConstructionScript: return TEXT("blueprint_created");
    case EComponentCreationMethod::UserConstructionScript: return TEXT("construction_script");
    case EComponentCreationMethod::Instance: return TEXT("instance");
    default: return TEXT("unknown");
    }
}

FString EditComponentId(const FString& OwnerEditActorId, const UActorComponent* Component)
{
    return EditStableId(
        OwnerEditActorId + TEXT("|") + (Component != nullptr ? Component->GetName() : FString())
        + TEXT("|") + (Component != nullptr && Component->GetClass() != nullptr
            ? Component->GetClass()->GetPathName() : FString())
        + TEXT("|") + (Component != nullptr ? EditCreationMethodName(Component->CreationMethod) : FString()));
}

TSharedRef<FUnrealMCPRecord> EditVectorRecord(const FVector& Value)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("x"), Value.X);
    Result->SetNumberField(TEXT("y"), Value.Y);
    Result->SetNumberField(TEXT("z"), Value.Z);
    return Result;
}

TSharedRef<FUnrealMCPRecord> EditTransformRecord(const FTransform& Value)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetObjectField(TEXT("location"), EditVectorRecord(Value.GetLocation()));
    const FRotator Rotation = Value.Rotator();
    const TSharedRef<FUnrealMCPRecord> RotationRecord = MakeShared<FUnrealMCPRecord>();
    RotationRecord->SetNumberField(TEXT("pitch"), Rotation.Pitch);
    RotationRecord->SetNumberField(TEXT("yaw"), Rotation.Yaw);
    RotationRecord->SetNumberField(TEXT("roll"), Rotation.Roll);
    Result->SetObjectField(TEXT("rotation"), RotationRecord);
    Result->SetObjectField(TEXT("scale"), EditVectorRecord(Value.GetScale3D()));
    return Result;
}

bool EditReadVector(const TSharedPtr<FUnrealMCPRecord>& Object, FVector& Out, double Bound)
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    if (!Object.IsValid() || !EditHasOnlyFields(*Object, {TEXT("x"), TEXT("y"), TEXT("z")})
        || !Object->TryGetNumberField(TEXT("x"), X) || !Object->TryGetNumberField(TEXT("y"), Y)
        || !Object->TryGetNumberField(TEXT("z"), Z) || !FMath::IsFinite(X) || !FMath::IsFinite(Y)
        || !FMath::IsFinite(Z) || FMath::Abs(X) > Bound || FMath::Abs(Y) > Bound || FMath::Abs(Z) > Bound)
    {
        return false;
    }
    Out = FVector(X, Y, Z);
    return true;
}

bool EditReadTransform(const FUnrealMCPRecord& Object, FTransform& Out, FUnrealMCPError& OutError)
{
    const TSharedPtr<FUnrealMCPRecord>* Location = nullptr;
    const TSharedPtr<FUnrealMCPRecord>* Rotation = nullptr;
    const TSharedPtr<FUnrealMCPRecord>* Scale = nullptr;
    FVector LocationValue;
    FVector ScaleValue;
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
    if (!EditHasOnlyFields(Object, {TEXT("location"), TEXT("rotation"), TEXT("scale")})
        || !Object.TryGetObjectField(TEXT("location"), Location) || Location == nullptr
        || !Object.TryGetObjectField(TEXT("rotation"), Rotation) || Rotation == nullptr
        || !Object.TryGetObjectField(TEXT("scale"), Scale) || Scale == nullptr
        || !EditReadVector(*Location, LocationValue, 1000000000.0)
        || !EditReadVector(*Scale, ScaleValue, 1000000.0)
        || !EditHasOnlyFields(**Rotation, {TEXT("pitch"), TEXT("yaw"), TEXT("roll")})
        || !(*Rotation)->TryGetNumberField(TEXT("pitch"), Pitch)
        || !(*Rotation)->TryGetNumberField(TEXT("yaw"), Yaw)
        || !(*Rotation)->TryGetNumberField(TEXT("roll"), Roll)
        || !FMath::IsFinite(Pitch) || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Roll)
        || FMath::Abs(Pitch) > 1000000000.0 || FMath::Abs(Yaw) > 1000000000.0
        || FMath::Abs(Roll) > 1000000000.0 || ScaleValue.IsNearlyZero())
    {
        OutError = {TEXT("invalid_argument"), TEXT("transform must contain finite bounded location, rotation, and nonzero scale")};
        return false;
    }
    Out = FTransform(FRotator(Pitch, Yaw, Roll), LocationValue, ScaleValue);
    return true;
}

TArray<FString> EditSortedNames(const TArray<FName>& Names)
{
    TArray<FString> Result;
    Result.Reserve(Names.Num());
    for (const FName Name : Names) Result.Add(Name.ToString());
    Result.Sort();
    return Result;
}

TArray<FString> EditDataLayerNames(const TArray<UDataLayerInstance*>& Instances)
{
    TArray<FString> Result;
    Result.Reserve(Instances.Num());
    for (const UDataLayerInstance* Instance : Instances)
    {
        if (Instance != nullptr) Result.Add(Instance->GetFName().ToString());
    }
    Result.Sort();
    return Result;
}

TArray<TSharedPtr<FUnrealMCPValue>> EditStringValues(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FUnrealMCPValue>> Result;
    Result.Reserve(Values.Num());
    for (const FString& Value : Values) Result.Add(MakeShared<FUnrealMCPValueString>(Value));
    return Result;
}

bool EditUnsafeEditorState(FUnrealMCPError& OutError)
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
    const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
    Details->SetBoolField(TEXT("is_playing"), bPlaying);
    Details->SetBoolField(TEXT("is_simulating"), bSimulating);
    Details->SetBoolField(TEXT("is_saving"), bSaving);
    Details->SetBoolField(TEXT("is_garbage_collecting"), bCollecting);
    Details->SetBoolField(TEXT("transaction_active"), bTransaction);
    Details->SetBoolField(TEXT("undo_redo_active"), bUndoRedo);
    Details->SetBoolField(TEXT("is_compiling"), bCompiling);
    Details->SetBoolField(TEXT("is_async_loading"), bLoading);
    OutError = {TEXT("busy"), TEXT("Level editing refused while unsafe editor work is active"), Details, true};
    return true;
}

bool EditCurrentState(
    FUnrealMCPLevelService& Levels,
    const FString& ExpectedMapId,
    const FString& ExpectedSnapshot,
    UWorld*& OutWorld,
    TSharedPtr<FUnrealMCPRecord>& OutRecord,
    FString& OutActualSnapshot,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("mode"), TEXT("current"));
    TSharedPtr<FUnrealMCPRecord> Result;
    if (!Levels.Inspect(Arguments, Result, OutError)) return false;
    const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1
        || !(*Records)[0].IsValid() || !(*Records)[0]->AsObject().IsValid())
    {
        OutError = {TEXT("invalid_response"), TEXT("Current-map inspection did not return one exact record")};
        return false;
    }
    OutRecord = (*Records)[0]->AsObject();
    OutActualSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString ActualMapId = OutRecord->GetStringField(TEXT("map_id"));
    if (ActualMapId != ExpectedMapId || OutActualSnapshot != ExpectedSnapshot)
    {
        const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
        Details->SetStringField(TEXT("expected_map_id"), ExpectedMapId);
        Details->SetStringField(TEXT("actual_map_id"), ActualMapId);
        Details->SetStringField(TEXT("expected_snapshot"), ExpectedSnapshot);
        Details->SetStringField(TEXT("actual_snapshot"), OutActualSnapshot);
        OutError = {TEXT("stale_precondition"), TEXT("The exact current-map snapshot changed before level mutation"), Details};
        return false;
    }
    OutWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (OutWorld == nullptr || OutWorld->PersistentLevel == nullptr)
    {
        OutError = {TEXT("editor_unavailable"), TEXT("No current persistent editor level is available"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    return true;
}

struct FResolvedActor
{
    FString Id;
    FGuid Guid;
    AActor* Actor = nullptr;
    FWorldPartitionActorDescInstance* Descriptor = nullptr;
    TUniquePtr<FWorldPartitionReference> Reference;
};

bool EditResolveActor(UWorld* World, const FString& MapId, const FString& Id, FResolvedActor& Out, FUnrealMCPError& OutError)
{
    FGuid Guid;
    if (!ParseEditActorId(Id, MapId, Guid))
    {
        OutError = {TEXT("invalid_argument"), TEXT("actor_id must be a GUID qualified by the exact current map")};
        return false;
    }
    Out.Id = Id;
    Out.Guid = Guid;
    if (UWorldPartition* Partition = World != nullptr ? World->GetWorldPartition() : nullptr)
    {
        int32 ScannedActors = 0;
        for (ULevel* Level : World->GetLevels())
        {
            if (Level == nullptr) continue;
            for (AActor* Actor : Level->Actors)
            {
                if (++ScannedActors > UnrealMCP::MaxLevelActorScan)
                {
                    OutError = {TEXT("data_limit_exceeded"), TEXT("Loaded actor resolution exceeds the published scan bound")};
                    return false;
                }
                if (Actor != nullptr && IsValid(Actor) && Actor->GetActorGuid() == Guid)
                {
                    Out.Actor = Actor;
                    Out.Descriptor = Partition->GetActorDescInstance(Guid);
                    return true;
                }
            }
        }
        FWorldPartitionActorDescInstance* Descriptor = Partition->GetActorDescInstance(Guid);
        if (Descriptor == nullptr || !Descriptor->IsValid())
        {
            OutError = {TEXT("not_found"), TEXT("The requested actor descriptor was not found in the current map")};
            return false;
        }
        Out.Descriptor = Descriptor;
        AActor* Actor = Descriptor->GetActor(false, false);
        if (Actor == nullptr || !IsValid(Actor) || Actor->GetLevel() == nullptr
            || !Actor->GetLevel()->Actors.Contains(Actor))
        {
            Out.Reference = MakeUnique<FWorldPartitionReference>(Partition, Guid);
            Actor = Out.Reference->GetActor();
        }
        if (Actor == nullptr || !IsValid(Actor))
        {
            OutError = {TEXT("actor_unavailable"), TEXT("The exact World Partition actor could not be loaded for editing"), MakeShared<FUnrealMCPRecord>(), true};
            return false;
        }
        Out.Actor = Actor;
        return true;
    }
    if (World != nullptr)
    {
        for (ULevel* Level : World->GetLevels())
        {
            if (Level == nullptr) continue;
            for (AActor* Actor : Level->Actors)
            {
                if (Actor != nullptr && IsValid(Actor) && Actor->GetActorGuid() == Guid)
                {
                    Out.Actor = Actor;
                    return true;
                }
            }
        }
    }
    OutError = {TEXT("not_found"), TEXT("The requested actor was not found in the current map")};
    return false;
}

UActorComponent* EditResolveComponent(AActor* Actor, const FString& EditActorIdentity, const FString& RequestedId)
{
    if (!EditIsLowerHex(RequestedId, 32) || Actor == nullptr) return nullptr;
    TInlineComponentArray<UActorComponent*> Components(Actor);
    if (Components.Num() > UnrealMCP::MaxLevelComponents) return nullptr;
    for (UActorComponent* Component : Components)
    {
        if (Component != nullptr && EditComponentId(EditActorIdentity, Component) == RequestedId) return Component;
    }
    return nullptr;
}

bool EditResolveDataLayers(
    UWorld* World,
    const TArray<TSharedPtr<FUnrealMCPValue>>& Values,
    TArray<UDataLayerInstance*>& Out,
    FUnrealMCPError& OutError)
{
    if (Values.Num() > UnrealMCP::MaxLevelDataLayers)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("data_layers exceeds the published bound")};
        return false;
    }
    UDataLayerManager* Manager = World != nullptr ? World->GetDataLayerManager() : nullptr;
    TSet<FName> Seen;
    for (const TSharedPtr<FUnrealMCPValue>& Value : Values)
    {
        FString Name;
        if (!Value.IsValid() || !Value->TryGetString(Name) || Name.IsEmpty() || Name.Len() > 512 || Seen.Contains(FName(*Name)))
        {
            OutError = {TEXT("invalid_argument"), TEXT("data_layers must contain unique exact instance names")};
            return false;
        }
        Seen.Add(FName(*Name));
        UDataLayerInstance* Instance = Manager != nullptr
            ? const_cast<UDataLayerInstance*>(Manager->GetDataLayerInstanceFromName(FName(*Name))) : nullptr;
        if (Instance == nullptr)
        {
            OutError = {TEXT("not_found"), TEXT("A requested data layer instance was not found in the current map")};
            return false;
        }
        for (const UDataLayerInstance* Cursor = Instance; Cursor != nullptr; Cursor = Cursor->GetParent())
        {
            const FBoolProperty* Locked = CastField<FBoolProperty>(
                Cursor->GetClass()->FindPropertyByName(TEXT("bIsLocked")));
            if (Locked != nullptr && Locked->GetPropertyValue_InContainer(Cursor))
            {
                OutError = {TEXT("locked_data_layer"), TEXT("A requested data layer or one of its parents is locked")};
                return false;
            }
        }
        Out.Add(Instance);
    }
    return true;
}

ULevel* EditResolveLevel(UWorld* World, const FString& PackageName)
{
    if (World == nullptr || !FPackageName::IsValidLongPackageName(PackageName)) return nullptr;
    for (ULevel* Level : World->GetLevels())
    {
        if (Level != nullptr && Level->GetPackage() != nullptr && Level->GetPackage()->GetName() == PackageName) return Level;
    }
    return nullptr;
}

bool EditValidateClassPath(const FString& Path, UClass*& OutClass, FUnrealMCPError& OutError)
{
    if (Path.IsEmpty() || Path.Len() > 512 || !Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("class_path must be one exact mounted native or Blueprint generated-class path")};
        return false;
    }
    OutClass = LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    // Native UClass objects normally carry RF_Transient even though actors spawned from
    // them are packageable. The unsafe case is a dynamically constructed class whose
    // outer is the transient package.
    const bool bTransientClass = OutClass != nullptr && OutClass->GetOutermost() == GetTransientPackage();
    if (OutClass == nullptr || !OutClass->IsChildOf(AActor::StaticClass())
        || OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || bTransientClass || IsEditorOnlyObject(OutClass))
    {
        const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
        Details->SetStringField(TEXT("class_path"), Path);
        Details->SetBoolField(TEXT("found"), OutClass != nullptr);
        Details->SetBoolField(TEXT("actor_class"), OutClass != nullptr && OutClass->IsChildOf(AActor::StaticClass()));
        Details->SetBoolField(TEXT("abstract"), OutClass != nullptr && OutClass->HasAnyClassFlags(CLASS_Abstract));
        Details->SetBoolField(TEXT("deprecated"), OutClass != nullptr && OutClass->HasAnyClassFlags(CLASS_Deprecated));
        Details->SetBoolField(TEXT("superseded"), OutClass != nullptr && OutClass->HasAnyClassFlags(CLASS_NewerVersionExists));
        Details->SetBoolField(TEXT("transient"), bTransientClass);
        Details->SetBoolField(TEXT("editor_only"), OutClass != nullptr && IsEditorOnlyObject(OutClass));
        OutError = {TEXT("unsafe_class"), TEXT("The spawn class is missing, incompatible, abstract, deprecated, transient, or editor-only"), Details};
        return false;
    }
    return true;
}

bool EditValidateEditableProperty(UObject* Object, const FString& Name, FUnrealMCPError& OutError)
{
    FProperty* Property = Object != nullptr ? Object->GetClass()->FindPropertyByName(FName(*Name)) : nullptr;
    FString Kind;
    if (Object == nullptr || Name.IsEmpty() || Name.Len() > 128 || Name.Contains(TEXT("."))
        || !UnrealMCP::PropertyCodec::IsSupportedEditable(Property, Kind)
        || Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance)
        || (!Object->HasAnyFlags(RF_ClassDefaultObject) && !Object->CanEditChange(Property)))
    {
        OutError = {TEXT("unsupported_property"), TEXT("The property is missing, unsafe, or not editable on this instance")};
        return false;
    }
    return true;
}

bool EditRestoreActorGuid(AActor* Actor, const FGuid& Guid)
{
    FStructProperty* Property = Actor != nullptr
        ? CastField<FStructProperty>(AActor::StaticClass()->FindPropertyByName(TEXT("ActorGuid")))
        : nullptr;
    if (Property == nullptr || Property->Struct != TBaseStructure<FGuid>::Get()) return false;
    Actor->Modify();
    *Property->ContainerPtrToValuePtr<FGuid>(Actor) = Guid;
    return Actor->GetActorGuid() == Guid;
}

bool EditReadStringArray(
    const FUnrealMCPRecord& Object,
    const TCHAR* Field,
    int32 Maximum,
    int32 CharacterLimit,
    TArray<FString>& Out,
    FUnrealMCPError& OutError)
{
    const TArray<TSharedPtr<FUnrealMCPValue>>* Values = nullptr;
    if (!Object.TryGetArrayField(Field, Values) || Values == nullptr || Values->Num() > Maximum)
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must be a bounded array"), Field)};
        return false;
    }
    TSet<FString> Seen;
    for (const TSharedPtr<FUnrealMCPValue>& Value : *Values)
    {
        FString Text;
        if (!Value.IsValid() || !Value->TryGetString(Text) || Text.IsEmpty() || Text.Len() > CharacterLimit || Seen.Contains(Text))
        {
            OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must contain unique bounded strings"), Field)};
            return false;
        }
        Seen.Add(Text);
        Out.Add(Text);
    }
    return true;
}

bool EditReadCurrentResult(
    FUnrealMCPLevelService& Levels,
    TSharedPtr<FUnrealMCPRecord>& OutRecord,
    FString& OutSnapshot,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("mode"), TEXT("current"));
    TSharedPtr<FUnrealMCPRecord> Result;
    if (!Levels.Inspect(Arguments, Result, OutError)) return false;
    const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr || Records->Num() != 1
        || !(*Records)[0].IsValid() || !(*Records)[0]->AsObject().IsValid())
    {
        OutError = {TEXT("invalid_response"), TEXT("Current-map inspection did not return one exact record")};
        return false;
    }
    OutRecord = (*Records)[0]->AsObject();
    OutSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    return true;
}

TSharedPtr<FUnrealMCPValue> EditEncodedPropertyValue(UObject* Object, const FString& Name)
{
    FProperty* Property = Object != nullptr ? Object->GetClass()->FindPropertyByName(FName(*Name)) : nullptr;
    return Property != nullptr ? UnrealMCP::PropertyCodec::Encode(Object, Property)->TryGetField(TEXT("value")) : nullptr;
}

void EditAddAffectedPackage(TSet<FString>& Packages, const UObject* Object)
{
    const UPackage* Package = Object != nullptr ? Object->GetPackage() : nullptr;
    if (Package != nullptr && FPackageName::IsValidLongPackageName(Package->GetName())) Packages.Add(Package->GetName());
}

bool EditJsonEqual(const TSharedPtr<FUnrealMCPValue>& Left, const TSharedPtr<FUnrealMCPValue>& Right)
{
    return Left.IsValid() && Right.IsValid() && FUnrealMCPValue::CompareEqual(*Left, *Right);
}

struct FPlannedOperation
{
    FString Kind;
    TSharedPtr<FUnrealMCPRecord> Request;
    FResolvedActor* Target = nullptr;
    FResolvedActor* Parent = nullptr;
    UActorComponent* Component = nullptr;
    UClass* SpawnClass = nullptr;
    ULevel* TargetLevel = nullptr;
    FTransform Transform;
    TArray<UDataLayerInstance*> DataLayers;
};

struct FActorJournal
{
    FGuid Guid;
    TWeakObjectPtr<UPackage> Package;
    bool bPackageWasDirty = false;
};

bool EditRollbackAndVerify(
    TUniquePtr<FScopedTransaction>& Transaction,
    UWorld* World,
    const TArray<FActorJournal>& Journal,
    const TArray<FGuid>& Created,
    FUnrealMCPError& OutError)
{
    Transaction.Reset();
    const bool bUndone = GEditor != nullptr && GEditor->UndoTransaction(false);
    bool bVerified = bUndone;
    for (const FActorJournal& Entry : Journal)
    {
        bool bFound = false;
        if (World != nullptr)
        {
            for (ULevel* Level : World->GetLevels())
            {
                if (Level == nullptr) continue;
                for (AActor* Actor : Level->Actors)
                {
                    bFound |= Actor != nullptr && IsValid(Actor) && Actor->GetActorGuid() == Entry.Guid;
                }
            }
        }
        bVerified &= bFound;
        if (Entry.Package.IsValid()) Entry.Package->SetDirtyFlag(Entry.bPackageWasDirty);
    }
    for (const FGuid& Guid : Created)
    {
        if (World == nullptr) continue;
        for (ULevel* Level : World->GetLevels())
        {
            if (Level == nullptr) continue;
            for (AActor* Actor : Level->Actors)
            {
                if (Actor != nullptr && IsValid(Actor) && Actor->GetActorGuid() == Guid) bVerified = false;
            }
        }
    }
    if (!bVerified)
    {
        OutError = {TEXT("rollback_failed"), TEXT("The level batch failed and Unreal could not verify exact in-memory rollback")};
    }
    return bVerified;
}

bool EditVerifyExpectedActors(
    UWorld* World,
    const FString& MapId,
    const TArray<TSharedPtr<FUnrealMCPValue>>& ExpectedActors,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FUnrealMCPError& OutError)
{
    for (const TSharedPtr<FUnrealMCPValue>& ExpectedValue : ExpectedActors)
    {
        const TSharedPtr<FUnrealMCPRecord> Expected = ExpectedValue.IsValid() ? ExpectedValue->AsObject() : nullptr;
        FString Id;
        if (!Expected.IsValid()
            || !EditHasAllowedFields(*Expected, {TEXT("actor_id"), TEXT("label"), TEXT("transform"), TEXT("tags"), TEXT("folder"), TEXT("actor_properties"), TEXT("components")})
            || !Expected->TryGetStringField(TEXT("actor_id"), Id))
        {
            OutError = {TEXT("invalid_argument"), TEXT("verification actors contain an invalid exact shape")};
            return false;
        }
        FResolvedActor Resolved;
        if (!EditResolveActor(World, MapId, Id, Resolved, OutError)) return false;
        AActor* Actor = Resolved.Actor;
        FString ExpectedText;
        if (Expected->TryGetStringField(TEXT("label"), ExpectedText) && Actor->GetActorLabel() != ExpectedText)
        {
            OutError = {TEXT("verification_failed"), TEXT("An actor label differs from its expected value")};
            return false;
        }
        if (Expected->TryGetStringField(TEXT("folder"), ExpectedText))
        {
            const FString ActualFolder = Resolved.Descriptor != nullptr
                ? Resolved.Descriptor->GetFolderPath().ToString()
                : Actor->GetFolderPath().ToString();
            if (ActualFolder != ExpectedText)
            {
                OutError = {TEXT("verification_failed"), FString::Printf(
                    TEXT("An actor folder differs from its expected value (expected '%s', actual '%s')"),
                    *ExpectedText.Left(128), *ActualFolder.Left(128))};
                return false;
            }
        }
        if (Expected->HasField(TEXT("transform")))
        {
            const TSharedPtr<FUnrealMCPRecord>* ExpectedTransform = nullptr;
            FTransform Transform;
            if (!Expected->TryGetObjectField(TEXT("transform"), ExpectedTransform) || ExpectedTransform == nullptr
                || !EditReadTransform(**ExpectedTransform, Transform, OutError)
                || !Actor->GetActorTransform().Equals(Transform, 0.0001f))
            {
                if (OutError.Code.IsEmpty()) OutError = {TEXT("verification_failed"), TEXT("An actor transform differs from its expected value")};
                return false;
            }
        }
        if (Expected->HasField(TEXT("tags")))
        {
            TArray<FString> ExpectedTags;
            if (!EditReadStringArray(*Expected, TEXT("tags"), UnrealMCP::MaxLevelActorTags, 128, ExpectedTags, OutError)) return false;
            ExpectedTags.Sort();
            if (EditSortedNames(Actor->Tags) != ExpectedTags)
            {
                OutError = {TEXT("verification_failed"), TEXT("An actor tag set differs from its expected value")};
                return false;
            }
        }
        if (Expected->HasField(TEXT("actor_properties")))
        {
            const TArray<TSharedPtr<FUnrealMCPValue>>& Properties = Expected->GetArrayField(TEXT("actor_properties"));
            for (const TSharedPtr<FUnrealMCPValue>& PropertyValue : Properties)
            {
                const TSharedPtr<FUnrealMCPRecord> Property = PropertyValue.IsValid() ? PropertyValue->AsObject() : nullptr;
                FString Name;
                if (!Property.IsValid() || !Property->TryGetStringField(TEXT("property_name"), Name)
                    || !EditValidateEditableProperty(Actor, Name, OutError)
                    || !EditJsonEqual(EditEncodedPropertyValue(Actor, Name), Property->TryGetField(TEXT("value"))))
                {
                    if (OutError.Code.IsEmpty()) OutError = {TEXT("verification_failed"), TEXT("An actor property differs from its expected value")};
                    return false;
                }
            }
        }
        if (Expected->HasField(TEXT("components")))
        {
            const TArray<TSharedPtr<FUnrealMCPValue>>& Components = Expected->GetArrayField(TEXT("components"));
            for (const TSharedPtr<FUnrealMCPValue>& ComponentValue : Components)
            {
                const TSharedPtr<FUnrealMCPRecord> ExpectedComponent = ComponentValue.IsValid() ? ComponentValue->AsObject() : nullptr;
                FString IdValue;
                if (!ExpectedComponent.IsValid() || !ExpectedComponent->TryGetStringField(TEXT("component_id"), IdValue))
                {
                    OutError = {TEXT("invalid_argument"), TEXT("verification component identity is invalid")};
                    return false;
                }
                UActorComponent* Component = EditResolveComponent(Actor, Id, IdValue);
                if (Component == nullptr)
                {
                    OutError = {TEXT("verification_failed"), TEXT("An expected component identity is unavailable")};
                    return false;
                }
                const TArray<TSharedPtr<FUnrealMCPValue>>& Properties = ExpectedComponent->GetArrayField(TEXT("properties"));
                for (const TSharedPtr<FUnrealMCPValue>& PropertyValue : Properties)
                {
                    const TSharedPtr<FUnrealMCPRecord> Property = PropertyValue.IsValid() ? PropertyValue->AsObject() : nullptr;
                    FString Name;
                    if (!Property.IsValid() || !Property->TryGetStringField(TEXT("property_name"), Name)
                        || !EditValidateEditableProperty(Component, Name, OutError)
                        || !EditJsonEqual(EditEncodedPropertyValue(Component, Name), Property->TryGetField(TEXT("value"))))
                    {
                        if (OutError.Code.IsEmpty()) OutError = {TEXT("verification_failed"), TEXT("A component property differs from its expected value")};
                        return false;
                    }
                }
            }
        }
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("actor_id"), Id);
        Record->SetBoolField(TEXT("verified"), true);
        Record->SetStringField(TEXT("package_name"), Actor->GetPackage()->GetName());
        OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    return true;
}
}

using namespace UnrealMCPLevelActorEditingPrivate;

FUnrealMCPLevelActorEditingService::FUnrealMCPLevelActorEditingService(FUnrealMCPLevelService& InLevels)
    : Levels(InLevels)
{
}

bool FUnrealMCPLevelActorEditingService::Edit(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid()
        || !EditHasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("map_id"), TEXT("expected_snapshot"), TEXT("operations")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_actor_edit requires only operation_id, map_id, expected_snapshot, and operations")};
        return false;
    }
    FString OperationId;
    FString MapId;
    FString ExpectedSnapshot;
    const TArray<TSharedPtr<FUnrealMCPValue>>* OperationValues = nullptr;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !EditIsLowerHex(OperationId, 32)
        || !Arguments->TryGetStringField(TEXT("map_id"), MapId) || !EditIsLowerHex(MapId, 40)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot) || !EditIsLowerHex(ExpectedSnapshot, 40)
        || !Arguments->TryGetArrayField(TEXT("operations"), OperationValues) || OperationValues == nullptr
        || OperationValues->IsEmpty() || OperationValues->Num() > UnrealMCP::MaxLevelEditOperations)
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_actor_edit identities or bounded operations are invalid")};
        return false;
    }
    if (EditUnsafeEditorState(OutError)) return false;

    UWorld* World = nullptr;
    TSharedPtr<FUnrealMCPRecord> CurrentRecord;
    FString ActualSnapshot;
    if (!EditCurrentState(Levels, MapId, ExpectedSnapshot, World, CurrentRecord, ActualSnapshot, OutError)) return false;
    if (World->GetPackage() == nullptr
        || !EditValidateMutationScope(World->GetPackage()->GetName(), OutError)) return false;

    TArray<FResolvedActor> Resolved;
    Resolved.Reserve(UnrealMCP::MaxLevelEditActors);
    TMap<FString, int32> ResolvedIndexes;
    const auto GetActor = [&](const FString& Id, FResolvedActor*& Out) -> bool
    {
        if (const int32* Existing = ResolvedIndexes.Find(Id))
        {
            Out = &Resolved[*Existing];
            return true;
        }
        if (Resolved.Num() >= UnrealMCP::MaxLevelEditActors)
        {
            OutError = {TEXT("data_limit_exceeded"), TEXT("The batch requires more actors than the published edit bound")};
            return false;
        }
        const int32 Index = Resolved.AddDefaulted();
        if (!EditResolveActor(World, MapId, Id, Resolved[Index], OutError))
        {
            Resolved.RemoveAt(Index);
            return false;
        }
        ResolvedIndexes.Add(Id, Index);
        Out = &Resolved[Index];
        return true;
    };

    TArray<FPlannedOperation> Plan;
    Plan.Reserve(OperationValues->Num());
    TSet<FString> MutationKeys;
    TSet<FString> DeletedActors;
    TSet<FString> OtherActors;
    TMap<FGuid, FGuid> DesiredParents;
    for (const TSharedPtr<FUnrealMCPValue>& Value : *OperationValues)
    {
        const TSharedPtr<FUnrealMCPRecord> Request = Value.IsValid() ? Value->AsObject() : nullptr;
        FString Kind;
        if (!Request.IsValid() || !Request->TryGetStringField(TEXT("operation"), Kind))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Every level actor operation requires an exact discriminator")};
            return false;
        }
        FPlannedOperation Item;
        Item.Kind = Kind;
        Item.Request = Request;
        FString EditActorIdentity;
        if (Kind != TEXT("spawn"))
        {
            if (!Request->TryGetStringField(TEXT("actor_id"), EditActorIdentity) || !GetActor(EditActorIdentity, Item.Target)) return false;
            if (Kind == TEXT("delete")) DeletedActors.Add(EditActorIdentity);
            else OtherActors.Add(EditActorIdentity);
        }

        FString MutationKey = EditActorIdentity + TEXT("|") + Kind;
        if (Kind == TEXT("spawn"))
        {
            if (!EditHasAllowedFields(*Request, {TEXT("operation"), TEXT("class_path"), TEXT("transform"), TEXT("label"), TEXT("tags"), TEXT("folder"), TEXT("data_layers"), TEXT("actor_properties")}))
            {
                OutError = {TEXT("invalid_argument"), TEXT("spawn contains unsupported fields")};
                return false;
            }
            FString ClassPath;
            const TSharedPtr<FUnrealMCPRecord>* Transform = nullptr;
            if (!Request->TryGetStringField(TEXT("class_path"), ClassPath)
                || !Request->TryGetObjectField(TEXT("transform"), Transform) || Transform == nullptr
                || !EditValidateClassPath(ClassPath, Item.SpawnClass, OutError)
                || !EditReadTransform(**Transform, Item.Transform, OutError)) return false;
            FString Label;
            FString Folder;
            if ((Request->HasField(TEXT("label")) && (!Request->TryGetStringField(TEXT("label"), Label) || Label.IsEmpty() || Label.Len() > 128))
                || (Request->HasField(TEXT("folder")) && (!Request->TryGetStringField(TEXT("folder"), Folder) || Folder.Len() > 512)))
            {
                OutError = {TEXT("invalid_argument"), TEXT("spawn label or folder is invalid")};
                return false;
            }
            TArray<FString> IgnoredStrings;
            if (Request->HasField(TEXT("tags")) && !EditReadStringArray(*Request, TEXT("tags"), UnrealMCP::MaxLevelActorTags, 128, IgnoredStrings, OutError)) return false;
            if (Request->HasField(TEXT("data_layers")))
            {
                const TArray<TSharedPtr<FUnrealMCPValue>>* DataLayerValues = nullptr;
                if (!Request->TryGetArrayField(TEXT("data_layers"), DataLayerValues) || DataLayerValues == nullptr
                    || !EditResolveDataLayers(World, *DataLayerValues, Item.DataLayers, OutError)) return false;
            }
            if (Request->HasField(TEXT("actor_properties")))
            {
                const TArray<TSharedPtr<FUnrealMCPValue>>* Properties = nullptr;
                if (!Request->TryGetArrayField(TEXT("actor_properties"), Properties) || Properties == nullptr
                    || Properties->Num() > UnrealMCP::MaxPropertyNames)
                {
                    OutError = {TEXT("invalid_argument"), TEXT("actor_properties exceeds the published bound")};
                    return false;
                }
                TSet<FString> Seen;
                UObject* Defaults = Item.SpawnClass->GetDefaultObject();
                for (const TSharedPtr<FUnrealMCPValue>& PropertyValue : *Properties)
                {
                    const TSharedPtr<FUnrealMCPRecord> Property = PropertyValue.IsValid() ? PropertyValue->AsObject() : nullptr;
                    FString Name;
                    if (!Property.IsValid() || !EditHasOnlyFields(*Property, {TEXT("property_name"), TEXT("value")})
                        || !Property->TryGetStringField(TEXT("property_name"), Name) || !Property->HasField(TEXT("value"))
                        || Seen.Contains(Name) || !EditValidateEditableProperty(Defaults, Name, OutError)) return false;
                    Seen.Add(Name);
                }
            }
        }
        else if (Kind == TEXT("transform"))
        {
            const TSharedPtr<FUnrealMCPRecord>* Transform = nullptr;
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("transform")})
                || !Request->TryGetObjectField(TEXT("transform"), Transform) || Transform == nullptr
                || !EditReadTransform(**Transform, Item.Transform, OutError)) return false;
        }
        else if (Kind == TEXT("label"))
        {
            FString Label;
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("label")})
                || !Request->TryGetStringField(TEXT("label"), Label) || Label.IsEmpty() || Label.Len() > 128)
            {
                OutError = {TEXT("invalid_argument"), TEXT("label must be a nonempty bounded string")};
                return false;
            }
        }
        else if (Kind == TEXT("folder"))
        {
            FString Folder;
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("folder")})
                || !Request->TryGetStringField(TEXT("folder"), Folder) || Folder.Len() > 512)
            {
                OutError = {TEXT("invalid_argument"), TEXT("folder must be a bounded path string")};
                return false;
            }
        }
        else if (Kind == TEXT("tags"))
        {
            TArray<FString> Ignored;
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("tags")})
                || !EditReadStringArray(*Request, TEXT("tags"), UnrealMCP::MaxLevelActorTags, 128, Ignored, OutError)) return false;
        }
        else if (Kind == TEXT("data_layers"))
        {
            const TArray<TSharedPtr<FUnrealMCPValue>>* Values = nullptr;
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("data_layers")})
                || !Request->TryGetArrayField(TEXT("data_layers"), Values) || Values == nullptr
                || !EditResolveDataLayers(World, *Values, Item.DataLayers, OutError)) return false;
        }
        else if (Kind == TEXT("attach"))
        {
            FString ParentIdentity;
            FString Socket;
            if (!EditHasAllowedFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("parent_actor_id"), TEXT("socket_name")})
                || !Request->TryGetStringField(TEXT("parent_actor_id"), ParentIdentity) || ParentIdentity == EditActorIdentity
                || (Request->HasField(TEXT("socket_name")) && (!Request->TryGetStringField(TEXT("socket_name"), Socket) || Socket.Len() > 128))
                || !GetActor(ParentIdentity, Item.Parent))
            {
                if (OutError.Code.IsEmpty()) OutError = {TEXT("attachment_cycle"), TEXT("An actor cannot attach to itself")};
                return false;
            }
            DesiredParents.Add(Item.Target->Guid, Item.Parent->Guid);
        }
        else if (Kind == TEXT("detach"))
        {
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id")}))
            {
                OutError = {TEXT("invalid_argument"), TEXT("detach contains unsupported fields")};
                return false;
            }
            DesiredParents.Add(Item.Target->Guid, FGuid());
        }
        else if (Kind == TEXT("actor_property") || Kind == TEXT("component_property"))
        {
            FString Name;
            const bool bComponent = Kind == TEXT("component_property");
            if ((bComponent && !EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("component_id"), TEXT("property_name"), TEXT("value")}))
                || (!bComponent && !EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("property_name"), TEXT("value")}))
                || !Request->TryGetStringField(TEXT("property_name"), Name) || !Request->HasField(TEXT("value")))
            {
                OutError = {TEXT("invalid_argument"), TEXT("property operation has an invalid exact shape")};
                return false;
            }
            UObject* TargetObject = Item.Target->Actor;
            if (bComponent)
            {
                FString RequestedComponent;
                if (!Request->TryGetStringField(TEXT("component_id"), RequestedComponent)
                    || (Item.Component = EditResolveComponent(Item.Target->Actor, EditActorIdentity, RequestedComponent)) == nullptr)
                {
                    OutError = {TEXT("not_found"), TEXT("The exact bounded component identity was not found")};
                    return false;
                }
                TargetObject = Item.Component;
                MutationKey += TEXT("|") + RequestedComponent;
            }
            MutationKey += TEXT("|") + Name;
            if (!EditValidateEditableProperty(TargetObject, Name, OutError)) return false;
        }
        else if (Kind == TEXT("move"))
        {
            FString TargetPackage;
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id"), TEXT("target_level")})
                || !Request->TryGetStringField(TEXT("target_level"), TargetPackage)
                || (Item.TargetLevel = EditResolveLevel(World, TargetPackage)) == nullptr
                || World->GetWorldPartition() != nullptr)
            {
                OutError = {TEXT("invalid_argument"), TEXT("move requires one loaded target level in a non-World-Partition current world")};
                return false;
            }
        }
        else if (Kind == TEXT("delete"))
        {
            if (!EditHasOnlyFields(*Request, {TEXT("operation"), TEXT("actor_id")})
                || Item.Target->Actor->IsA<AWorldSettings>() || Item.Target->Actor->IsActorBeingDestroyed())
            {
                OutError = {TEXT("invalid_argument"), TEXT("delete targets an unsupported or already-destroyed actor")};
                return false;
            }
        }
        else
        {
            OutError = {TEXT("invalid_argument"), TEXT("Unknown level actor operation")};
            return false;
        }
        if (Kind != TEXT("spawn") && MutationKeys.Contains(MutationKey))
        {
            OutError = {TEXT("operation_conflict"), TEXT("The batch contains duplicate writes to the same actor field")};
            return false;
        }
        MutationKeys.Add(MutationKey);
        Plan.Add(MoveTemp(Item));
    }
    for (const FString& Deleted : DeletedActors)
    {
        if (OtherActors.Contains(Deleted))
        {
            OutError = {TEXT("operation_conflict"), TEXT("A deleted actor cannot receive another operation in the same batch")};
            return false;
        }
    }
    for (const TPair<FGuid, FGuid>& Pair : DesiredParents)
    {
        FGuid Cursor = Pair.Value;
        TSet<FGuid> Seen{Pair.Key};
        for (int32 Depth = 0; Cursor.IsValid() && Depth <= UnrealMCP::MaxLevelEditActors; ++Depth)
        {
            if (Seen.Contains(Cursor))
            {
                OutError = {TEXT("attachment_cycle"), TEXT("The requested attachment graph contains a cycle")};
                return false;
            }
            Seen.Add(Cursor);
            if (const FGuid* Desired = DesiredParents.Find(Cursor)) Cursor = *Desired;
            else
            {
                AActor* Actor = nullptr;
                for (FResolvedActor& Entry : Resolved) if (Entry.Guid == Cursor) Actor = Entry.Actor;
                Cursor = Actor != nullptr && Actor->GetAttachParentActor() != nullptr
                    ? Actor->GetAttachParentActor()->GetActorGuid() : FGuid();
            }
        }
        if (Cursor.IsValid())
        {
            OutError = {TEXT("attachment_cycle"), TEXT("The requested attachment chain exceeds the bounded actor set")};
            return false;
        }
    }

    TArray<FActorJournal> Journal;
    Journal.Reserve(Resolved.Num());
    TSet<FString> AffectedPackages;
    EditAddAffectedPackage(AffectedPackages, World);
    for (const FResolvedActor& Entry : Resolved)
    {
        EditAddAffectedPackage(AffectedPackages, Entry.Actor);
        UPackage* Package = Entry.Actor != nullptr ? Entry.Actor->GetPackage() : nullptr;
        Journal.Add({Entry.Guid, Package, Package != nullptr && Package->IsDirty()});
    }
    TArray<FGuid> Created;
    TArray<TSharedPtr<FUnrealMCPValue>> Readback;
    TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(
        NSLOCTEXT("UnrealMCP", "LevelActorEdit", "Edit level actors"));
    const auto FailMutation = [&](const FUnrealMCPError& Failure) -> bool
    {
        OutError = Failure;
        EditRollbackAndVerify(Transaction, World, Journal, Created, OutError);
        return false;
    };

    for (int32 Index = 0; Index < Plan.Num(); ++Index)
    {
        FPlannedOperation& Item = Plan[Index];
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetNumberField(TEXT("operation_index"), Index);
        Record->SetStringField(TEXT("operation"), Item.Kind);
        AActor* Actor = Item.Target != nullptr ? Item.Target->Actor : nullptr;
        if (Item.Kind == TEXT("spawn"))
        {
            FActorSpawnParameters Parameters;
            Parameters.OverrideLevel = World->PersistentLevel;
            Parameters.ObjectFlags |= RF_Transactional;
            Parameters.bCreateActorPackage = true;
            Actor = World->SpawnActor<AActor>(Item.SpawnClass, Item.Transform, Parameters);
            if (Actor == nullptr || !Actor->GetActorGuid().IsValid())
            {
                return FailMutation({TEXT("spawn_failed"), TEXT("Unreal could not spawn the requested actor class")});
            }
            Created.Add(Actor->GetActorGuid());
            Actor->Modify();
            FString Label;
            FString Folder;
            if (Item.Request->TryGetStringField(TEXT("label"), Label)) Actor->SetActorLabel(Label);
            if (Item.Request->TryGetStringField(TEXT("folder"), Folder)) Actor->SetFolderPath(FName(*Folder));
            if (Item.Request->HasField(TEXT("tags")))
            {
                TArray<FString> Tags;
                FUnrealMCPError Ignored;
                EditReadStringArray(*Item.Request, TEXT("tags"), UnrealMCP::MaxLevelActorTags, 128, Tags, Ignored);
                Actor->Tags.Reset();
                for (const FString& Tag : Tags) Actor->Tags.Add(FName(*Tag));
            }
            if (Item.Request->HasField(TEXT("data_layers")))
            {
                UDataLayerEditorSubsystem* DataLayers = GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
                if (DataLayers == nullptr || !DataLayers->IsActorValidForDataLayerInstances(Actor, Item.DataLayers))
                {
                    return FailMutation({TEXT("data_layer_failed"), TEXT("Unreal could not assign the requested data layers to the new actor")});
                }
                DataLayers->AddActorToDataLayers(Actor, Item.DataLayers);
                if (EditSortedNames(Actor->GetDataLayerInstanceNames()) != EditDataLayerNames(Item.DataLayers))
                    return FailMutation({TEXT("data_layer_failed"), TEXT("The new actor data layer read-back differs from the request")});
            }
            if (Item.Request->HasField(TEXT("actor_properties")))
            {
                const TArray<TSharedPtr<FUnrealMCPValue>>& Properties = Item.Request->GetArrayField(TEXT("actor_properties"));
                for (const TSharedPtr<FUnrealMCPValue>& PropertyValue : Properties)
                {
                    const TSharedPtr<FUnrealMCPRecord> Property = PropertyValue->AsObject();
                    TSharedPtr<FUnrealMCPRecord> Changed;
                    FUnrealMCPError Failure;
                    if (!UnrealMCP::PropertyCodec::Set(Actor, Property->GetStringField(TEXT("property_name")), Property->TryGetField(TEXT("value")), Changed, Failure))
                    {
                        return FailMutation(Failure);
                    }
                }
            }
            if (!Actor->GetActorTransform().Equals(Item.Transform, 0.0001f)
                || (Item.Request->HasField(TEXT("label")) && Actor->GetActorLabel() != Label)
                || (Item.Request->HasField(TEXT("folder")) && Actor->GetFolderPath().ToString() != Folder))
            {
                return FailMutation({TEXT("postcondition_failed"), TEXT("The spawned actor transform or metadata differs from the request")});
            }
            if (Item.Request->HasField(TEXT("tags")))
            {
                TArray<FString> ExpectedTags;
                FUnrealMCPError Ignored;
                EditReadStringArray(*Item.Request, TEXT("tags"), UnrealMCP::MaxLevelActorTags, 128, ExpectedTags, Ignored);
                ExpectedTags.Sort();
                if (EditSortedNames(Actor->Tags) != ExpectedTags)
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The spawned actor tag set differs from the request")});
            }
            EditAddAffectedPackage(AffectedPackages, Actor);
            Record->SetStringField(TEXT("actor_id"), EditActorId(MapId, Actor->GetActorGuid()));
            Record->SetStringField(TEXT("class_path"), Actor->GetClass()->GetPathName());
            Record->SetObjectField(TEXT("transform"), EditTransformRecord(Actor->GetActorTransform()));
            Record->SetStringField(TEXT("package_name"), Actor->GetPackage()->GetName());
        }
        else
        {
            if (Actor == nullptr || !IsValid(Actor)) return FailMutation({TEXT("actor_unavailable"), TEXT("An actor became unavailable during the prevalidated batch")});
            Actor->Modify();
            Record->SetStringField(TEXT("actor_id"), Item.Target->Id);
            if (Item.Kind == TEXT("transform"))
            {
                if (!Actor->SetActorTransform(Item.Transform, false, nullptr, ETeleportType::TeleportPhysics))
                    return FailMutation({TEXT("postcondition_failed"), TEXT("Unreal rejected the requested actor transform")});
                Actor->PostEditMove(true);
                if (!Actor->GetActorTransform().Equals(Item.Transform, 0.0001f))
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor transform read-back differs from the request")});
                Record->SetObjectField(TEXT("transform"), EditTransformRecord(Actor->GetActorTransform()));
            }
            else if (Item.Kind == TEXT("label"))
            {
                Actor->SetActorLabel(Item.Request->GetStringField(TEXT("label")));
                if (Actor->GetActorLabel() != Item.Request->GetStringField(TEXT("label")))
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor label read-back differs from the request")});
                Record->SetStringField(TEXT("label"), Actor->GetActorLabel());
            }
            else if (Item.Kind == TEXT("tags"))
            {
                TArray<FString> Tags;
                FUnrealMCPError Ignored;
                EditReadStringArray(*Item.Request, TEXT("tags"), UnrealMCP::MaxLevelActorTags, 128, Tags, Ignored);
                Actor->Tags.Reset();
                for (const FString& Tag : Tags) Actor->Tags.Add(FName(*Tag));
                Tags.Sort();
                if (EditSortedNames(Actor->Tags) != Tags)
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor tag read-back differs from the request")});
                Record->SetArrayField(TEXT("tags"), EditStringValues(EditSortedNames(Actor->Tags)));
            }
            else if (Item.Kind == TEXT("folder"))
            {
                Actor->SetFolderPath(FName(*Item.Request->GetStringField(TEXT("folder"))));
                if (Actor->GetFolderPath().ToString() != Item.Request->GetStringField(TEXT("folder")))
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor folder read-back differs from the request")});
                Record->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());
            }
            else if (Item.Kind == TEXT("data_layers"))
            {
                UDataLayerEditorSubsystem* DataLayers = GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
                if (DataLayers == nullptr || !DataLayers->IsActorValidForDataLayerInstances(Actor, Item.DataLayers))
                {
                    return FailMutation({TEXT("data_layer_failed"), TEXT("Unreal could not apply the exact data layer set")});
                }
                DataLayers->RemoveActorFromAllDataLayers(Actor);
                DataLayers->AddActorToDataLayers(Actor, Item.DataLayers);
                if (EditSortedNames(Actor->GetDataLayerInstanceNames()) != EditDataLayerNames(Item.DataLayers))
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor Data Layer read-back differs from the request")});
                Record->SetArrayField(TEXT("data_layers"), EditStringValues(EditSortedNames(Actor->GetDataLayerInstanceNames())));
            }
            else if (Item.Kind == TEXT("attach"))
            {
                FString SocketText;
                Item.Request->TryGetStringField(TEXT("socket_name"), SocketText);
                const FName Socket(*SocketText);
                if (!Actor->AttachToActor(Item.Parent->Actor, FAttachmentTransformRules::KeepWorldTransform, Socket))
                    return FailMutation({TEXT("attachment_failed"), TEXT("Unreal rejected the prevalidated actor attachment")});
                if (Actor->GetAttachParentActor() != Item.Parent->Actor || Actor->GetAttachParentSocketName() != Socket)
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor attachment read-back differs from the request")});
                Record->SetStringField(TEXT("parent_actor_id"), Item.Parent->Id);
                Record->SetStringField(TEXT("socket_name"), Actor->GetAttachParentSocketName().ToString());
            }
            else if (Item.Kind == TEXT("detach"))
            {
                Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
                if (Actor->GetAttachParentActor() != nullptr)
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The actor detach read-back differs from the request")});
                Record->SetStringField(TEXT("parent_actor_id"), FString());
            }
            else if (Item.Kind == TEXT("actor_property") || Item.Kind == TEXT("component_property"))
            {
                UObject* TargetObject = Item.Kind == TEXT("component_property")
                    ? static_cast<UObject*>(Item.Component) : static_cast<UObject*>(Actor);
                TargetObject->Modify();
                TSharedPtr<FUnrealMCPRecord> Changed;
                FUnrealMCPError Failure;
                if (!UnrealMCP::PropertyCodec::Set(
                    TargetObject,
                    Item.Request->GetStringField(TEXT("property_name")),
                    Item.Request->TryGetField(TEXT("value")),
                    Changed,
                    Failure)) return FailMutation(Failure);
                Record->SetObjectField(TEXT("property"), Changed.ToSharedRef());
                if (Item.Component != nullptr)
                    Record->SetStringField(TEXT("component_id"), Item.Request->GetStringField(TEXT("component_id")));
            }
            else if (Item.Kind == TEXT("move"))
            {
                TArray<AActor*> Moved;
                const int32 MoveCount = UEditorLevelUtils::MoveActorsToLevel(
                    {Actor}, Item.TargetLevel, false, false, true, &Moved);
                if (MoveCount != 1 || Moved.Num() != 1 || Moved[0] == nullptr)
                {
                    return FailMutation({TEXT("move_failed"), FString::Printf(
                        TEXT("Unreal did not return exactly one moved actor (count %d, read-back %d)"),
                        MoveCount, Moved.Num())});
                }
                if (Moved[0]->GetActorGuid() != Item.Target->Guid)
                {
                    if (!EditRestoreActorGuid(Moved[0], Item.Target->Guid))
                        return FailMutation({TEXT("move_failed"), TEXT("Unreal changed the Actor GUID and exact restoration failed")});
                }
                Item.Target->Actor = Moved[0];
                Actor = Moved[0];
                if (Actor->GetLevel() != Item.TargetLevel)
                    return FailMutation({TEXT("postcondition_failed"), TEXT("The moved actor level read-back differs from the request")});
                EditAddAffectedPackage(AffectedPackages, Actor);
                Record->SetStringField(TEXT("target_level"), Actor->GetLevel()->GetPackage()->GetName());
            }
            else if (Item.Kind == TEXT("delete"))
            {
                EditAddAffectedPackage(AffectedPackages, Actor);
                if (!World->EditorDestroyActor(Actor, true))
                    return FailMutation({TEXT("delete_failed"), TEXT("Unreal rejected deletion of the prevalidated actor")});
                Item.Target->Actor = nullptr;
                Record->SetBoolField(TEXT("deleted"), true);
            }
            if (Actor != nullptr && Item.Kind != TEXT("delete"))
            {
                Actor->MarkPackageDirty();
                EditAddAffectedPackage(AffectedPackages, Actor);
            }
        }
        Readback.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    Transaction.Reset();

    TSharedPtr<FUnrealMCPRecord> UpdatedCurrent;
    FString UpdatedSnapshot;
    if (!EditReadCurrentResult(Levels, UpdatedCurrent, UpdatedSnapshot, OutError)) return false;
    // World Partition folder edits may dirty an external actor-folder object package
    // in addition to the actor package. Return every loaded dirty package owned by
    // the current world's levels so the caller gets a complete explicit save set.
    TSet<FString> OwnedRootPackages;
    TArray<FString> OwnedExternalPrefixes;
    for (ULevel* Level : World->GetLevels())
    {
        if (Level == nullptr || Level->GetPackage() == nullptr) continue;
        const FString LevelPackage = Level->GetPackage()->GetName();
        OwnedRootPackages.Add(LevelPackage);
        OwnedExternalPrefixes.Append(ULevel::GetExternalActorsPaths(LevelPackage));
        OwnedExternalPrefixes.Append(ULevel::GetExternalObjectsPaths(LevelPackage));
    }
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (Package == nullptr || !Package->IsDirty()) continue;
        const FString PackageName = Package->GetName();
        bool bOwned = OwnedRootPackages.Contains(PackageName);
        for (const FString& Prefix : OwnedExternalPrefixes)
            bOwned |= PackageName == Prefix || PackageName.StartsWith(Prefix + TEXT("/"));
        if (bOwned) AffectedPackages.Add(PackageName);
    }
    TArray<FString> SortedPackages = AffectedPackages.Array();
    SortedPackages.Sort();
    if (SortedPackages.Num() > UnrealMCP::MaxLevelSavePackages)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("The affected package set exceeds the published save bound")};
        return false;
    }
    OutResult = MakeShared<FUnrealMCPRecord>();
    OutResult->SetStringField(TEXT("map_id"), MapId);
    OutResult->SetStringField(TEXT("snapshot_id"), UpdatedSnapshot);
    OutResult->SetNumberField(TEXT("operation_count"), Plan.Num());
    OutResult->SetArrayField(TEXT("operations"), Readback);
    OutResult->SetArrayField(TEXT("affected_packages"), EditStringValues(SortedPackages));
    OutResult->SetObjectField(TEXT("current_map"), UpdatedCurrent.ToSharedRef());
    OutResult->SetBoolField(TEXT("rollback_verified"), true);
    OutResult->SetBoolField(TEXT("saved"), false);
    return true;
}

bool FUnrealMCPLevelActorEditingService::Save(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid()
        || !EditHasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("map_id"), TEXT("expected_snapshot"), TEXT("affected_packages"), TEXT("verification")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_save requires only operation_id, map_id, expected_snapshot, affected_packages, and verification")};
        return false;
    }
    FString OperationId;
    FString MapId;
    FString ExpectedSnapshot;
    const TArray<TSharedPtr<FUnrealMCPValue>>* PackageValues = nullptr;
    const TSharedPtr<FUnrealMCPRecord>* Verification = nullptr;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !EditIsLowerHex(OperationId, 32)
        || !Arguments->TryGetStringField(TEXT("map_id"), MapId) || !EditIsLowerHex(MapId, 40)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot) || !EditIsLowerHex(ExpectedSnapshot, 40)
        || !Arguments->TryGetArrayField(TEXT("affected_packages"), PackageValues) || PackageValues == nullptr
        || PackageValues->IsEmpty() || PackageValues->Num() > UnrealMCP::MaxLevelSavePackages
        || !Arguments->TryGetObjectField(TEXT("verification"), Verification) || Verification == nullptr
        || !EditHasOnlyFields(**Verification, {TEXT("mode"), TEXT("actors")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("level_save identities, package set, or verification shape is invalid")};
        return false;
    }
    FString VerificationMode;
    const TArray<TSharedPtr<FUnrealMCPValue>>* ExpectedActors = nullptr;
    if (!(*Verification)->TryGetStringField(TEXT("mode"), VerificationMode)
        || (VerificationMode != TEXT("inspect") && VerificationMode != TEXT("reload"))
        || !(*Verification)->TryGetArrayField(TEXT("actors"), ExpectedActors) || ExpectedActors == nullptr
        || ExpectedActors->IsEmpty() || ExpectedActors->Num() > UnrealMCP::MaxLevelEditActors)
    {
        OutError = {TEXT("invalid_argument"), TEXT("verification requires inspect or reload mode and a bounded actor expectation set")};
        return false;
    }
    if (EditUnsafeEditorState(OutError)) return false;

    UWorld* World = nullptr;
    TSharedPtr<FUnrealMCPRecord> CurrentRecord;
    FString ActualSnapshot;
    if (!EditCurrentState(Levels, MapId, ExpectedSnapshot, World, CurrentRecord, ActualSnapshot, OutError)) return false;
    const FString RootPackageName = World->GetPackage()->GetName();
    if (!EditValidateMutationScope(RootPackageName, OutError)) return false;
    TSet<FString> AllowedRootPackages;
    TArray<FString> AllowedExternalPrefixes;
    for (ULevel* Level : World->GetLevels())
    {
        if (Level == nullptr || Level->GetPackage() == nullptr) continue;
        const FString LevelPackage = Level->GetPackage()->GetName();
        AllowedRootPackages.Add(LevelPackage);
        AllowedExternalPrefixes.Append(ULevel::GetExternalActorsPaths(LevelPackage));
        AllowedExternalPrefixes.Append(ULevel::GetExternalObjectsPaths(LevelPackage));
    }
    TSet<FString> SeenPackages;
    TArray<UPackage*> Packages;
    Packages.Reserve(PackageValues->Num());
    for (const TSharedPtr<FUnrealMCPValue>& PackageValue : *PackageValues)
    {
        FString PackageName;
        if (!PackageValue.IsValid() || !PackageValue->TryGetString(PackageName)
            || !FPackageName::IsValidLongPackageName(PackageName) || SeenPackages.Contains(PackageName))
        {
            OutError = {TEXT("invalid_argument"), TEXT("affected_packages must contain unique exact long package names")};
            return false;
        }
        SeenPackages.Add(PackageName);
        if (!EditValidateMutationScope(PackageName, OutError)) return false;
        bool bAllowed = AllowedRootPackages.Contains(PackageName);
        for (const FString& Prefix : AllowedExternalPrefixes)
        {
            bAllowed |= PackageName == Prefix || PackageName.StartsWith(Prefix + TEXT("/"));
        }
        UPackage* Package = bAllowed ? FindPackage(nullptr, *PackageName) : nullptr;
        if (!bAllowed || Package == nullptr || Package == GetTransientPackage() || Package->HasAnyPackageFlags(PKG_CompiledIn))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Every affected package must be a loaded package owned by the exact current world")};
            return false;
        }
        FString ExistingFilename;
        if (FPackageName::DoesPackageExist(PackageName, &ExistingFilename)
            && IFileManager::Get().IsReadOnly(*ExistingFilename))
        {
            OutError = {TEXT("package_not_writable"), TEXT("An affected package is read-only")};
            return false;
        }
        Packages.Add(Package);
    }
    if (!SeenPackages.Contains(RootPackageName))
    {
        OutError = {TEXT("invalid_argument"), TEXT("affected_packages must explicitly include the current root map package")};
        return false;
    }

    TArray<TSharedPtr<FUnrealMCPValue>> PackageResults;
    TArray<FString> SavedPackages;
    TArray<FString> FailedPackages;
    for (UPackage* Package : Packages)
    {
        const FString PackageName = Package->GetName();
        bool bSaveSucceeded = false;
        if (PackageName == RootPackageName)
        {
            const FString Filename = FPackageName::LongPackageNameToFilename(
                PackageName, FPackageName::GetMapPackageExtension());
            bSaveSucceeded = FEditorFileUtils::SaveMap(World, Filename);
        }
        else
        {
            bSaveSucceeded = UEditorLoadingAndSavingUtils::SavePackages({Package}, false);
        }
        FString StoredFilename;
        const bool bStoragePresent = FPackageName::DoesPackageExist(PackageName, &StoredFilename)
            && IFileManager::Get().FileExists(*StoredFilename);
        const bool bClean = !Package->IsDirty();
        const bool bDeletedFromStorage = !bStoragePresent && UPackage::IsEmptyPackage(Package);
        const bool bVerified = bSaveSucceeded && bClean && (bStoragePresent || bDeletedFromStorage);
        if (bVerified) SavedPackages.Add(PackageName);
        else FailedPackages.Add(PackageName);
        const TSharedRef<FUnrealMCPRecord> PackageResult = MakeShared<FUnrealMCPRecord>();
        PackageResult->SetStringField(TEXT("package_name"), PackageName);
        PackageResult->SetBoolField(TEXT("save_succeeded"), bSaveSucceeded);
        PackageResult->SetBoolField(TEXT("storage_present"), bStoragePresent);
        PackageResult->SetBoolField(TEXT("deleted_from_storage"), bDeletedFromStorage);
        PackageResult->SetBoolField(TEXT("clean"), bClean);
        PackageResult->SetBoolField(TEXT("verified"), bVerified);
        PackageResults.Add(MakeShared<FUnrealMCPValueObject>(PackageResult));
    }

    bool bReloaded = false;
    FUnrealMCPError VerificationError;
    TArray<TSharedPtr<FUnrealMCPValue>> VerificationRecords;
    bool bVerificationSucceeded = FailedPackages.IsEmpty();
    if (bVerificationSucceeded && VerificationMode == TEXT("reload"))
    {
        const FString RootFilename = FPackageName::LongPackageNameToFilename(
            RootPackageName, FPackageName::GetMapPackageExtension());
        bReloaded = FPackageName::DoesPackageExist(RootPackageName)
            && FEditorFileUtils::LoadMap(RootFilename, false, false);
        World = bReloaded && GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
        bVerificationSucceeded = bReloaded && World != nullptr;
        if (!bVerificationSucceeded)
            VerificationError = {TEXT("reload_failed"), TEXT("Saved packages could not be reloaded for verification")};
    }
    if (bVerificationSucceeded)
    {
        bVerificationSucceeded = EditVerifyExpectedActors(
            World, MapId, *ExpectedActors, VerificationRecords, VerificationError);
    }
    if (!bVerificationSucceeded && VerificationError.Code.IsEmpty())
    {
        VerificationError = {TEXT("partial_persistence"), TEXT("One or more affected packages failed save verification")};
    }

    TSharedPtr<FUnrealMCPRecord> UpdatedCurrent;
    FString UpdatedSnapshot;
    FUnrealMCPError CurrentError;
    const bool bCurrentAvailable = EditReadCurrentResult(Levels, UpdatedCurrent, UpdatedSnapshot, CurrentError);
    SavedPackages.Sort();
    FailedPackages.Sort();
    OutResult = MakeShared<FUnrealMCPRecord>();
    OutResult->SetStringField(TEXT("map_id"), MapId);
    OutResult->SetStringField(TEXT("snapshot_id"), bCurrentAvailable ? UpdatedSnapshot : FString());
    OutResult->SetBoolField(TEXT("saved"), FailedPackages.IsEmpty());
    OutResult->SetBoolField(TEXT("reload_performed"), bReloaded);
    OutResult->SetBoolField(TEXT("verification_succeeded"), bVerificationSucceeded);
    OutResult->SetArrayField(TEXT("package_results"), PackageResults);
    OutResult->SetArrayField(TEXT("saved_packages"), EditStringValues(SavedPackages));
    OutResult->SetArrayField(TEXT("failed_packages"), EditStringValues(FailedPackages));
    OutResult->SetArrayField(TEXT("verified_actors"), VerificationRecords);
    if (UpdatedCurrent.IsValid()) OutResult->SetObjectField(TEXT("current_map"), UpdatedCurrent.ToSharedRef());
    if (!bVerificationSucceeded)
    {
        OutResult->SetStringField(TEXT("operation_state"), TEXT("partial"));
        const TSharedRef<FUnrealMCPRecord> ErrorRecord = MakeShared<FUnrealMCPRecord>();
        ErrorRecord->SetStringField(TEXT("code"), VerificationError.Code.Left(64));
        ErrorRecord->SetStringField(TEXT("message"), VerificationError.Message.Left(512));
        OutResult->SetObjectField(TEXT("verification_error"), ErrorRecord);
    }
    return true;
}
