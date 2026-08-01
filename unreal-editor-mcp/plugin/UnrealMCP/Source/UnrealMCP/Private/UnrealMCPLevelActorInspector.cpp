#include "UnrealMCPLevelActorInspector.h"

#include "ComponentInstanceDataCache.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionHelpers.h"

namespace UnrealMCPLevelActorInspectorPrivate
{
struct FActorCandidate
{
    FGuid Guid;
    const FWorldPartitionActorDescInstance* Descriptor = nullptr;
    AActor* Actor = nullptr;
};

struct FActorFilters
{
    FString ActorId;
    FString Label;
    FString ClassPath;
    FString Tag;
    FString Folder;
    FString DataLayer;
    TOptional<bool> Loaded;
    TOptional<FBox> Region;
};

AActor* LoadedDescriptorActor(const FWorldPartitionActorDescInstance* Descriptor)
{
    AActor* Actor = Descriptor != nullptr ? Descriptor->GetActor(false, false) : nullptr;
    ULevel* Level = Actor != nullptr ? Actor->GetLevel() : nullptr;
    return IsValid(Actor) && Level != nullptr && Level->Actors.Contains(Actor)
        ? Actor
        : nullptr;
}

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

bool ReadBoundedString(
    const FJsonObject& Object,
    const TCHAR* Name,
    int32 Maximum,
    bool bRequired,
    FString& Out,
    FUnrealMCPError& OutError)
{
    if (!Object.HasField(Name))
    {
        if (!bRequired) return true;
        OutError = {TEXT("invalid_argument"), FString(Name) + TEXT(" is required")};
        return false;
    }
    if (!Object.TryGetStringField(Name, Out) || Out.IsEmpty() || Out.Len() > Maximum)
    {
        OutError = {TEXT("invalid_argument"), FString(Name) + TEXT(" must be one bounded exact string")};
        return false;
    }
    return true;
}

bool ReadVector(const FJsonObject& Object, FVector& Out)
{
    if (!HasOnlyFields(Object, {TEXT("x"), TEXT("y"), TEXT("z")}))
    {
        return false;
    }
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    if (!Object.TryGetNumberField(TEXT("x"), X)
        || !Object.TryGetNumberField(TEXT("y"), Y)
        || !Object.TryGetNumberField(TEXT("z"), Z)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
        || FMath::Abs(X) > 1000000000.0 || FMath::Abs(Y) > 1000000000.0
        || FMath::Abs(Z) > 1000000000.0)
    {
        return false;
    }
    Out = FVector(X, Y, Z);
    return true;
}

bool ParseActorId(const FString& ActorId, const FString& MapId, FGuid& OutGuid)
{
    return ActorId.Len() == 73
        && ActorId.Left(40) == MapId
        && ActorId[40] == TEXT(':')
        && IsLowerHex(ActorId.Mid(41), 32)
        && FGuid::ParseExact(ActorId.Mid(41), EGuidFormats::Digits, OutGuid);
}

FString ActorId(const FString& MapId, const FGuid& Guid)
{
    return MapId + TEXT(":") + Guid.ToString(EGuidFormats::Digits).ToLower();
}

FString StableId(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower().Left(32);
}

TSharedRef<FJsonObject> VectorRecord(const FVector& Value)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("x"), Value.X);
    Result->SetNumberField(TEXT("y"), Value.Y);
    Result->SetNumberField(TEXT("z"), Value.Z);
    return Result;
}

TSharedRef<FJsonObject> TransformRecord(const FTransform& Value)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("location"), VectorRecord(Value.GetLocation()));
    const FRotator Rotation = Value.Rotator();
    const TSharedRef<FJsonObject> RotationRecord = MakeShared<FJsonObject>();
    RotationRecord->SetNumberField(TEXT("roll"), Rotation.Roll);
    RotationRecord->SetNumberField(TEXT("pitch"), Rotation.Pitch);
    RotationRecord->SetNumberField(TEXT("yaw"), Rotation.Yaw);
    Result->SetObjectField(TEXT("rotation"), RotationRecord);
    Result->SetObjectField(TEXT("scale"), VectorRecord(Value.GetScale3D()));
    return Result;
}

void SetBounds(const TSharedRef<FJsonObject>& Record, const FBox& Bounds)
{
    Record->SetBoolField(TEXT("bounds_available"), Bounds.IsValid != 0);
    if (Bounds.IsValid)
    {
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetObjectField(TEXT("min"), VectorRecord(Bounds.Min));
        Value->SetObjectField(TEXT("max"), VectorRecord(Bounds.Max));
        Record->SetObjectField(TEXT("bounds"), Value);
    }
}

TArray<FString> CandidateTags(const FActorCandidate& Candidate)
{
    TArray<FString> Result;
    const TArray<FName>* Tags = Candidate.Descriptor != nullptr
        ? &Candidate.Descriptor->GetTags()
        : (Candidate.Actor != nullptr ? &Candidate.Actor->Tags : nullptr);
    if (Tags != nullptr)
    {
        for (int32 Index = 0; Index < FMath::Min(Tags->Num(), UnrealMCP::MaxLevelActorTags); ++Index)
        {
            Result.Add((*Tags)[Index].ToString().Left(128));
        }
    }
    Result.Sort();
    return Result;
}

TArray<FString> CandidateDataLayers(const FActorCandidate& Candidate)
{
    TArray<FString> Result;
    const TArray<FName> Names = Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetDataLayers()
        : (Candidate.Actor != nullptr ? Candidate.Actor->GetDataLayerInstanceNames() : TArray<FName>());
    for (int32 Index = 0; Index < FMath::Min(Names.Num(), UnrealMCP::MaxLevelDataLayers); ++Index)
    {
        Result.Add(Names[Index].ToString().Left(512));
    }
    Result.Sort();
    return Result;
}

FString CandidateLabel(const FActorCandidate& Candidate)
{
    return Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetActorLabelOrName().ToString()
        : (Candidate.Actor != nullptr ? Candidate.Actor->GetActorLabel() : FString());
}

FString CandidateClassPath(const FActorCandidate& Candidate)
{
    if (Candidate.Descriptor != nullptr)
    {
        return Candidate.Descriptor->GetBaseClass().ToString();
    }
    return Candidate.Actor != nullptr && Candidate.Actor->GetClass() != nullptr
        ? Candidate.Actor->GetClass()->GetPathName()
        : FString();
}

FString CandidateFolder(const FActorCandidate& Candidate)
{
    return Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetFolderPath().ToString()
        : (Candidate.Actor != nullptr ? Candidate.Actor->GetFolderPath().ToString() : FString());
}

FBox CandidateBounds(const FActorCandidate& Candidate)
{
    if (Candidate.Descriptor != nullptr) return Candidate.Descriptor->GetEditorBounds();
    if (Candidate.Actor == nullptr) return FBox(ForceInit);
    FVector Origin;
    FVector Extent;
    Candidate.Actor->GetActorBounds(false, Origin, Extent, true);
    return FBox::BuildAABB(Origin, Extent);
}

bool Matches(const FActorCandidate& Candidate, const FActorFilters& Filters, const FString& MapId)
{
    const bool bLoaded = Candidate.Actor != nullptr;
    if (!Filters.ActorId.IsEmpty() && ActorId(MapId, Candidate.Guid) != Filters.ActorId) return false;
    if (!Filters.Label.IsEmpty() && CandidateLabel(Candidate) != Filters.Label) return false;
    if (!Filters.ClassPath.IsEmpty() && CandidateClassPath(Candidate) != Filters.ClassPath) return false;
    if (!Filters.Tag.IsEmpty() && !CandidateTags(Candidate).Contains(Filters.Tag)) return false;
    if (!Filters.Folder.IsEmpty() && CandidateFolder(Candidate) != Filters.Folder) return false;
    if (!Filters.DataLayer.IsEmpty() && !CandidateDataLayers(Candidate).Contains(Filters.DataLayer)) return false;
    if (Filters.Loaded.IsSet() && bLoaded != Filters.Loaded.GetValue()) return false;
    if (Filters.Region.IsSet())
    {
        const FBox Bounds = CandidateBounds(Candidate);
        if (!Bounds.IsValid || !Bounds.Intersect(Filters.Region.GetValue())) return false;
    }
    return true;
}

TSharedRef<FJsonObject> ActorRecord(const FActorCandidate& Candidate, const FString& MapId)
{
    const bool bLoaded = Candidate.Actor != nullptr;
    const FString PackageName = Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetActorPackage().ToString()
        : (Candidate.Actor != nullptr && Candidate.Actor->GetPackage() != nullptr
            ? Candidate.Actor->GetPackage()->GetName() : FString());
    const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("section"), TEXT("actor"));
    Record->SetStringField(TEXT("actor_id"), ActorId(MapId, Candidate.Guid));
    Record->SetStringField(TEXT("actor_guid"), Candidate.Guid.ToString(EGuidFormats::Digits).ToLower());
    Record->SetStringField(TEXT("label"), CandidateLabel(Candidate).Left(128));
    Record->SetStringField(TEXT("class_path"), CandidateClassPath(Candidate).Left(512));
    Record->SetStringField(
        TEXT("native_class_path"),
        Candidate.Descriptor != nullptr
            ? Candidate.Descriptor->GetNativeClass().ToString().Left(512)
            : (Candidate.Actor != nullptr && Candidate.Actor->GetClass() != nullptr
                ? Candidate.Actor->GetClass()->GetPathName().Left(512) : FString()));
    Record->SetStringField(TEXT("folder"), CandidateFolder(Candidate).Left(512));
    Record->SetBoolField(TEXT("loaded"), bLoaded);
    Record->SetBoolField(TEXT("descriptor_available"), Candidate.Descriptor != nullptr);
    Record->SetStringField(TEXT("external_package"), PackageName.Left(512));
    UPackage* Package = PackageName.IsEmpty() ? nullptr : FindPackage(nullptr, *PackageName);
    Record->SetBoolField(TEXT("package_dirty"), Package != nullptr && Package->IsDirty());
    Record->SetBoolField(TEXT("spatially_loaded"), Candidate.Descriptor != nullptr
        && Candidate.Descriptor->GetIsSpatiallyLoaded());
    Record->SetStringField(TEXT("runtime_grid"), Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetRuntimeGrid().ToString().Left(128) : FString());
    Record->SetObjectField(
        TEXT("transform"),
        TransformRecord(Candidate.Descriptor != nullptr
            ? Candidate.Descriptor->GetActorTransform()
            : (Candidate.Actor != nullptr ? Candidate.Actor->GetActorTransform() : FTransform::Identity)));
    SetBounds(Record, CandidateBounds(Candidate));

    TArray<TSharedPtr<FJsonValue>> Tags;
    for (const FString& Tag : CandidateTags(Candidate)) Tags.Add(MakeShared<FJsonValueString>(Tag));
    Record->SetArrayField(TEXT("tags"), Tags);
    Record->SetBoolField(TEXT("tags_truncated"), (Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetTags().Num()
        : (Candidate.Actor != nullptr ? Candidate.Actor->Tags.Num() : 0)) > UnrealMCP::MaxLevelActorTags);
    TArray<TSharedPtr<FJsonValue>> DataLayers;
    for (const FString& Name : CandidateDataLayers(Candidate)) DataLayers.Add(MakeShared<FJsonValueString>(Name));
    Record->SetArrayField(TEXT("data_layers"), DataLayers);
    const int32 DataLayerCount = Candidate.Descriptor != nullptr
        ? Candidate.Descriptor->GetDataLayers().Num()
        : (Candidate.Actor != nullptr ? Candidate.Actor->GetDataLayerInstanceNames().Num() : 0);
    Record->SetBoolField(
        TEXT("data_layers_truncated"),
        DataLayerCount > UnrealMCP::MaxLevelDataLayers);

    FGuid ParentGuid;
    if (Candidate.Descriptor != nullptr) ParentGuid = Candidate.Descriptor->GetParentActor();
    else if (Candidate.Actor != nullptr && Candidate.Actor->GetAttachParentActor() != nullptr)
        ParentGuid = Candidate.Actor->GetAttachParentActor()->GetActorGuid();
    Record->SetStringField(
        TEXT("attachment_parent_actor_id"),
        ParentGuid.IsValid() ? ActorId(MapId, ParentGuid) : FString());
    return Record;
}

FString CreationMethodName(EComponentCreationMethod Method)
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

FString ComponentId(const FString& OwnerActorId, const UActorComponent* Component)
{
    return StableId(
        OwnerActorId + TEXT("|")
        + (Component != nullptr ? Component->GetName() : FString()) + TEXT("|")
        + (Component != nullptr && Component->GetClass() != nullptr
            ? Component->GetClass()->GetPathName() : FString()) + TEXT("|")
        + (Component != nullptr ? CreationMethodName(Component->CreationMethod) : FString()));
}

TSharedRef<FJsonObject> ComponentRecord(
    UActorComponent* Component,
    const FString& OwnerActorId)
{
    const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("section"), TEXT("component"));
    Record->SetStringField(TEXT("actor_id"), OwnerActorId);
    Record->SetStringField(TEXT("component_id"), ComponentId(OwnerActorId, Component));
    Record->SetStringField(TEXT("name"), Component != nullptr ? Component->GetName().Left(128) : FString());
    Record->SetStringField(TEXT("class_path"), Component != nullptr && Component->GetClass() != nullptr
        ? Component->GetClass()->GetPathName().Left(512) : FString());
    Record->SetStringField(TEXT("creation_method"), Component != nullptr
        ? CreationMethodName(Component->CreationMethod) : TEXT("unknown"));
    Record->SetBoolField(TEXT("registered"), Component != nullptr && Component->IsRegistered());
    Record->SetBoolField(TEXT("active"), Component != nullptr && Component->IsActive());
    const USceneComponent* Scene = Cast<USceneComponent>(Component);
    Record->SetBoolField(TEXT("scene_component"), Scene != nullptr);
    if (Scene != nullptr)
    {
        Record->SetObjectField(TEXT("world_transform"), TransformRecord(Scene->GetComponentTransform()));
        Record->SetObjectField(TEXT("relative_transform"), TransformRecord(Scene->GetRelativeTransform()));
    }
    return Record;
}

bool ReadPropertyNames(
    const FJsonObject& Arguments,
    TArray<FString>& OutNames,
    FUnrealMCPError& OutError)
{
    if (!Arguments.HasField(TEXT("property_names"))) return true;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Arguments.TryGetArrayField(TEXT("property_names"), Values)
        || Values == nullptr || Values->IsEmpty() || Values->Num() > UnrealMCP::MaxPropertyNames)
    {
        OutError = {TEXT("invalid_argument"), TEXT("property_names must contain one to 32 exact names")};
        return false;
    }
    TSet<FString> Seen;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Name;
        if (!Value.IsValid() || !Value->TryGetString(Name) || Name.IsEmpty()
            || Name.Len() > 128 || Name.Contains(TEXT(".")) || Name.Contains(TEXT("\\"))
            || Seen.Contains(Name))
        {
            OutError = {TEXT("invalid_argument"), TEXT("property_names contains an invalid or duplicate exact name")};
            return false;
        }
        Seen.Add(Name);
        OutNames.Add(Name);
    }
    return true;
}

bool AddProperties(
    UObject* Object,
    const FString& OwnerKind,
    const FString& OwnerId,
    const TArray<FString>& Names,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FUnrealMCPError& OutError)
{
    for (const FString& Name : Names)
    {
        FProperty* Property = Object != nullptr && Object->GetClass() != nullptr
            ? Object->GetClass()->FindPropertyByName(FName(*Name)) : nullptr;
        const bool bVisible = Property != nullptr
            && Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)
            && !Property->HasAnyPropertyFlags(
                CPF_Transient | CPF_Deprecated | CPF_EditorOnly | CPF_InstancedReference
                | CPF_ContainsInstancedReference | CPF_ExportObject)
            && Property->ArrayDim == 1
            && !Property->IsA<FDelegateProperty>()
            && !Property->IsA<FMulticastDelegateProperty>()
            && !Property->IsA<FInterfaceProperty>();
        TSharedPtr<FJsonValue> Encoded;
        FUnrealMCPError CodecError;
        if (!bVisible || !UnrealMCP::GameDataValueCodec::Encode(
                Property,
                Property != nullptr && Object != nullptr
                    ? Property->ContainerPtrToValuePtr<void>(Object) : nullptr,
                0,
                Encoded,
                CodecError))
        {
            const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("property_name"), Name);
            if (!CodecError.Message.IsEmpty()) Details->SetStringField(TEXT("reason"), CodecError.Message.Left(512));
            OutError = {
                TEXT("unsupported_property"),
                TEXT("The requested property is unavailable, unsafe, hidden, or outside the bounded reflected-value policy"),
                Details};
            return false;
        }
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("section"), TEXT("property"));
        Record->SetStringField(TEXT("owner_kind"), OwnerKind);
        Record->SetStringField(TEXT("owner_id"), OwnerId);
        Record->SetStringField(TEXT("property_id"), StableId(OwnerId + TEXT("|") + Name));
        Record->SetStringField(TEXT("name"), Name);
        Record->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
        Record->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
        Record->SetObjectField(TEXT("type"), UnrealMCP::GameDataValueCodec::EncodeType(Property));
        Record->SetField(TEXT("value"), Encoded);
        OutRecords.Add(MakeShared<FJsonValueObject>(Record));
    }
    return true;
}

bool ReadFilters(
    const FJsonObject& Arguments,
    const FString& MapId,
    FActorFilters& Out,
    FUnrealMCPError& OutError)
{
    if (!Arguments.HasField(TEXT("filters"))) return true;
    const TSharedPtr<FJsonObject>* Filters = nullptr;
    if (!Arguments.TryGetObjectField(TEXT("filters"), Filters)
        || Filters == nullptr || !(*Filters).IsValid()
        || !HasOnlyFields(**Filters, {
            TEXT("actor_id"), TEXT("label"), TEXT("class_path"), TEXT("tag"), TEXT("folder"),
            TEXT("data_layer"), TEXT("loaded"), TEXT("region")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("filters must be one exact bounded actor-filter object")};
        return false;
    }
    if ((*Filters)->HasField(TEXT("actor_id")))
    {
        FGuid Ignored;
        if (!ReadBoundedString(**Filters, TEXT("actor_id"), 73, true, Out.ActorId, OutError)
            || !ParseActorId(Out.ActorId, MapId, Ignored))
        {
            OutError = {TEXT("invalid_argument"), TEXT("filters.actor_id must be a GUID qualified by the exact current map")};
            return false;
        }
    }
    if (((*Filters)->HasField(TEXT("label"))
            && !ReadBoundedString(**Filters, TEXT("label"), 128, true, Out.Label, OutError))
        || ((*Filters)->HasField(TEXT("class_path"))
            && !ReadBoundedString(**Filters, TEXT("class_path"), 512, true, Out.ClassPath, OutError))
        || ((*Filters)->HasField(TEXT("tag"))
            && !ReadBoundedString(**Filters, TEXT("tag"), 128, true, Out.Tag, OutError))
        || ((*Filters)->HasField(TEXT("folder"))
            && !ReadBoundedString(**Filters, TEXT("folder"), 512, true, Out.Folder, OutError))
        || ((*Filters)->HasField(TEXT("data_layer"))
            && !ReadBoundedString(**Filters, TEXT("data_layer"), 512, true, Out.DataLayer, OutError)))
    {
        return false;
    }
    if ((*Filters)->HasField(TEXT("class_path"))
        && (!Out.ClassPath.StartsWith(TEXT("/")) || Out.ClassPath.Contains(TEXT(".."))
            || Out.ClassPath.Contains(TEXT("\\"))))
    {
        OutError = {TEXT("invalid_argument"), TEXT("filters.class_path must be one exact mounted class path")};
        return false;
    }
    if ((*Filters)->HasField(TEXT("loaded")))
    {
        bool bLoaded = false;
        if (!(*Filters)->TryGetBoolField(TEXT("loaded"), bLoaded))
        {
            OutError = {TEXT("invalid_argument"), TEXT("filters.loaded must be Boolean")};
            return false;
        }
        Out.Loaded = bLoaded;
    }
    if ((*Filters)->HasField(TEXT("region")))
    {
        const TSharedPtr<FJsonObject>* Region = nullptr;
        const TSharedPtr<FJsonObject>* Min = nullptr;
        const TSharedPtr<FJsonObject>* Max = nullptr;
        FVector MinValue;
        FVector MaxValue;
        if (!(*Filters)->TryGetObjectField(TEXT("region"), Region) || Region == nullptr || !(*Region).IsValid()
            || !HasOnlyFields(**Region, {TEXT("min"), TEXT("max")})
            || !(*Region)->TryGetObjectField(TEXT("min"), Min) || Min == nullptr || !(*Min).IsValid()
            || !(*Region)->TryGetObjectField(TEXT("max"), Max) || Max == nullptr || !(*Max).IsValid()
            || !ReadVector(**Min, MinValue) || !ReadVector(**Max, MaxValue)
            || MinValue.X > MaxValue.X || MinValue.Y > MaxValue.Y || MinValue.Z > MaxValue.Z)
        {
            OutError = {TEXT("invalid_argument"), TEXT("filters.region must contain finite ordered min and max vectors")};
            return false;
        }
        Out.Region = FBox(MinValue, MaxValue);
    }
    return true;
}

bool CollectCandidates(
    UWorld* World,
    TArray<FActorCandidate>& Out,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError)
{
    if (World == nullptr || World->PersistentLevel == nullptr)
    {
        OutError = {TEXT("editor_unavailable"), TEXT("No current persistent editor level is available"), MakeShared<FJsonObject>(), true};
        return false;
    }
    int32 Scanned = 0;
    if (UWorldPartition* WorldPartition = World->GetWorldPartition())
    {
        FWorldPartitionHelpers::ForEachActorDescInstance(
            WorldPartition,
            [&Out, &Scanned, &OutScanTruncated](const FWorldPartitionActorDescInstance* Descriptor)
            {
                if (Scanned >= UnrealMCP::MaxLevelActorScan)
                {
                    OutScanTruncated = true;
                    return false;
                }
                ++Scanned;
                if (Descriptor != nullptr && Descriptor->IsValid())
                {
                    Out.Add(FActorCandidate{
                        Descriptor->GetGuid(),
                        Descriptor,
                        LoadedDescriptorActor(Descriptor)});
                }
                return true;
            });
    }
    else
    {
        for (AActor* Actor : World->PersistentLevel->Actors)
        {
            if (Actor == nullptr || !IsValid(Actor)) continue;
            if (Scanned >= UnrealMCP::MaxLevelActorScan)
            {
                OutScanTruncated = true;
                break;
            }
            ++Scanned;
            Out.Add(FActorCandidate{Actor->GetActorGuid(), nullptr, Actor});
        }
    }
    return true;
}

bool ResolveTarget(
    UWorld* World,
    const FString& MapId,
    const FString& RequestedActorId,
    FActorCandidate& Out,
    TUniquePtr<FWorldPartitionReference>& OutTemporaryReference,
    FUnrealMCPError& OutError)
{
    FGuid Guid;
    if (!ParseActorId(RequestedActorId, MapId, Guid))
    {
        OutError = {TEXT("invalid_argument"), TEXT("actor_id must be a GUID qualified by the exact current map")};
        return false;
    }
    if (UWorldPartition* WorldPartition = World != nullptr ? World->GetWorldPartition() : nullptr)
    {
        FWorldPartitionActorDescInstance* Descriptor = WorldPartition->GetActorDescInstance(Guid);
        if (Descriptor == nullptr || !Descriptor->IsValid())
        {
            OutError = {TEXT("not_found"), TEXT("The requested actor descriptor was not found in the current map")};
            return false;
        }
        AActor* Actor = LoadedDescriptorActor(Descriptor);
        if (Actor == nullptr)
        {
            OutTemporaryReference = MakeUnique<FWorldPartitionReference>(WorldPartition, Guid);
            Actor = OutTemporaryReference->GetActor();
        }
        if (Actor == nullptr)
        {
            const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("actor_id"), RequestedActorId);
            Details->SetStringField(TEXT("unloaded_reason"), Descriptor->GetUnloadedReason().ToString().Left(512));
            OutError = {
                TEXT("actor_unavailable"),
                TEXT("The exact World Partition actor could not be loaded for live inspection"),
                Details,
                true};
            return false;
        }
        Out = FActorCandidate{Guid, Descriptor, Actor};
        return true;
    }
    if (World != nullptr && World->PersistentLevel != nullptr)
    {
        for (AActor* Actor : World->PersistentLevel->Actors)
        {
            if (Actor != nullptr && IsValid(Actor) && Actor->GetActorGuid() == Guid)
            {
                Out = FActorCandidate{Guid, nullptr, Actor};
                return true;
            }
        }
    }
    OutError = {TEXT("not_found"), TEXT("The requested actor was not found in the current map")};
    return false;
}
}

using namespace UnrealMCPLevelActorInspectorPrivate;

bool FUnrealMCPLevelActorInspector::BuildRecords(
    const FJsonObject& Arguments,
    UWorld* World,
    const FString& MapId,
    const FString& SnapshotId,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError)
{
    OutScanTruncated = false;
    FString Mode;
    FString RequestedMapId;
    FString ExpectedSnapshot;
    if (!Arguments.TryGetStringField(TEXT("mode"), Mode)
        || !ReadBoundedString(Arguments, TEXT("map_id"), 40, true, RequestedMapId, OutError)
        || !ReadBoundedString(Arguments, TEXT("expected_snapshot"), 40, true, ExpectedSnapshot, OutError)
        || !IsLowerHex(RequestedMapId, 40) || !IsLowerHex(ExpectedSnapshot, 40))
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("invalid_argument"), TEXT("map_id and expected_snapshot must be exact lowercase 40-hex values")};
        return false;
    }
    if (RequestedMapId != MapId || ExpectedSnapshot != SnapshotId)
    {
        const TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("current_map_id"), MapId);
        Details->SetStringField(TEXT("current_snapshot"), SnapshotId);
        OutError = {TEXT("stale_precondition"), TEXT("The current map identity or snapshot does not match the request"), Details};
        return false;
    }

    if (Mode == TEXT("actors"))
    {
        if (!UnrealMCPLevelActorInspectorPrivate::HasOnlyFields(Arguments, {
                TEXT("mode"), TEXT("map_id"), TEXT("expected_snapshot"), TEXT("filters"), TEXT("page_size")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Actor-list inspection contains an unknown field")};
            return false;
        }
        FActorFilters Filters;
        if (!ReadFilters(Arguments, MapId, Filters, OutError)) return false;
        TArray<FActorCandidate> Candidates;
        if (!CollectCandidates(World, Candidates, OutScanTruncated, OutError)) return false;
        Candidates.Sort([&MapId](const FActorCandidate& Left, const FActorCandidate& Right)
        {
            return ActorId(MapId, Left.Guid) < ActorId(MapId, Right.Guid);
        });
        for (const FActorCandidate& Candidate : Candidates)
        {
            if (!Matches(Candidate, Filters, MapId)) continue;
            if (OutRecords.Num() >= UnrealMCP::MaxLevelActorRecords)
            {
                OutScanTruncated = true;
                break;
            }
            OutRecords.Add(MakeShared<FJsonValueObject>(ActorRecord(Candidate, MapId)));
        }
        return true;
    }

    const bool bActorMode = Mode == TEXT("actor");
    const bool bComponentMode = Mode == TEXT("component");
    if ((!bActorMode && !bComponentMode)
        || !UnrealMCPLevelActorInspectorPrivate::HasOnlyFields(Arguments, bActorMode
            ? std::initializer_list<const TCHAR*>{
                TEXT("mode"), TEXT("map_id"), TEXT("expected_snapshot"), TEXT("actor_id"),
                TEXT("property_names"), TEXT("page_size")}
            : std::initializer_list<const TCHAR*>{
                TEXT("mode"), TEXT("map_id"), TEXT("expected_snapshot"), TEXT("actor_id"),
                TEXT("component_id"), TEXT("property_names"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Exact actor/component inspection contains an unknown field or mode")};
        return false;
    }
    FString RequestedActorId;
    if (!ReadBoundedString(Arguments, TEXT("actor_id"), 73, true, RequestedActorId, OutError)) return false;
    TArray<FString> PropertyNames;
    if (!ReadPropertyNames(Arguments, PropertyNames, OutError)) return false;

    FActorCandidate Candidate;
    TUniquePtr<FWorldPartitionReference> TemporaryReference;
    if (!ResolveTarget(World, MapId, RequestedActorId, Candidate, TemporaryReference, OutError)) return false;
    OutRecords.Add(MakeShared<FJsonValueObject>(ActorRecord(Candidate, MapId)));

    TArray<UActorComponent*> Components;
    Candidate.Actor->GetComponents(Components);
    Components.Sort([&RequestedActorId](const UActorComponent& Left, const UActorComponent& Right)
    {
        return ComponentId(RequestedActorId, &Left) < ComponentId(RequestedActorId, &Right);
    });
    if (Components.Num() > UnrealMCP::MaxLevelComponents)
    {
        OutError = {
            TEXT("inspection_limit_exceeded"),
            TEXT("The selected actor exceeds the bounded component inspection limit")};
        return false;
    }

    if (bActorMode)
    {
        for (UActorComponent* Component : Components)
        {
            OutRecords.Add(MakeShared<FJsonValueObject>(
                UnrealMCPLevelActorInspectorPrivate::ComponentRecord(Component, RequestedActorId)));
        }
        return AddProperties(
            Candidate.Actor,
            TEXT("actor"),
            RequestedActorId,
            PropertyNames,
            OutRecords,
            OutError);
    }

    FString RequestedComponentId;
    if (!ReadBoundedString(
            Arguments, TEXT("component_id"), 32, true, RequestedComponentId, OutError)
        || !IsLowerHex(RequestedComponentId, 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("component_id must be one exact 32-character actor-scoped identity")};
        return false;
    }
    UActorComponent* Selected = nullptr;
    for (UActorComponent* Component : Components)
    {
        if (ComponentId(RequestedActorId, Component) == RequestedComponentId)
        {
            Selected = Component;
            break;
        }
    }
    if (Selected == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The requested actor-scoped component was not found")};
        return false;
    }
    OutRecords.Add(MakeShared<FJsonValueObject>(
        UnrealMCPLevelActorInspectorPrivate::ComponentRecord(Selected, RequestedActorId)));
    return AddProperties(
        Selected,
        TEXT("component"),
        RequestedComponentId,
        PropertyNames,
        OutRecords,
        OutError);
}
