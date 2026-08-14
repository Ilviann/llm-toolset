#include "UnrealMCPAssetFamilyRegistry.h"

#include "Algo/Sort.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"

namespace
{
constexpr int32 MaxStableIdentityBytes = 128;
constexpr int32 MaxSemanticPathBytes = 512;
constexpr int32 MaxFamilyPriority = 100000;

void SetError(FUnrealMCPError& OutError, const TCHAR* Code, const FString& Message)
{
    OutError = {Code, Message};
}

int32 Utf8Bytes(const FString& Value)
{
    return FTCHARToUTF8(*Value).Length();
}

bool IsStableIdentity(const FString& Value)
{
    if (Value.IsEmpty() || Utf8Bytes(Value) > MaxStableIdentityBytes)
    {
        return false;
    }
    for (int32 Index = 0; Index < Value.Len(); ++Index)
    {
        const TCHAR Character = Value[Index];
        const bool bValid = (Character >= TEXT('a') && Character <= TEXT('z'))
            || (Index > 0 && Character >= TEXT('0') && Character <= TEXT('9'))
            || (Index > 0 && (Character == TEXT('_') || Character == TEXT('-')));
        if (!bValid)
        {
            return false;
        }
    }
    return true;
}

bool IsBoundedText(const FString& Value, int32 MaxBytes)
{
    if (Value.IsEmpty() || Utf8Bytes(Value) > MaxBytes)
    {
        return false;
    }
    for (const TCHAR Character : Value)
    {
        if (Character == 0 || Character < 0x20 || Character == 0x7f)
        {
            return false;
        }
    }
    return true;
}

bool AddBoundedCost(int32 Cost, int32 Limit, int32& InOutBytes)
{
    if (Cost < 0 || Cost > Limit - InOutBytes)
    {
        return false;
    }
    InOutBytes += Cost;
    return true;
}

bool EstimateValueBytes(
    const TSharedPtr<FUnrealMCPValue>& Value,
    const FUnrealMCPAssetFamilyLimits& Limits,
    int32 Depth,
    int32& InOutNodes,
    int32& InOutBytes)
{
    if (!Value.IsValid() || Depth > Limits.MaxValueDepth
        || ++InOutNodes > Limits.MaxValueNodes)
    {
        return false;
    }
    switch (Value->Type)
    {
    case EUnrealMCPValueType::Null:
        return AddBoundedCost(4, Limits.MaxDocumentBytes, InOutBytes);
    case EUnrealMCPValueType::Boolean:
        return AddBoundedCost(5, Limits.MaxDocumentBytes, InOutBytes);
    case EUnrealMCPValueType::Number:
        return AddBoundedCost(32, Limits.MaxDocumentBytes, InOutBytes);
    case EUnrealMCPValueType::String:
        return AddBoundedCost(Utf8Bytes(Value->AsString()) + 2, Limits.MaxDocumentBytes, InOutBytes);
    case EUnrealMCPValueType::Array:
        if (!AddBoundedCost(2, Limits.MaxDocumentBytes, InOutBytes))
        {
            return false;
        }
        for (const TSharedPtr<FUnrealMCPValue>& Item : Value->AsArray())
        {
            if (!EstimateValueBytes(Item, Limits, Depth + 1, InOutNodes, InOutBytes)
                || !AddBoundedCost(1, Limits.MaxDocumentBytes, InOutBytes))
            {
                return false;
            }
        }
        return true;
    case EUnrealMCPValueType::Record:
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Value->AsObject();
        if (!Record.IsValid())
        {
            return false;
        }
        if (!AddBoundedCost(2, Limits.MaxDocumentBytes, InOutBytes))
        {
            return false;
        }
        for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Record->Values)
        {
            if (!AddBoundedCost(Utf8Bytes(Field.Key) + 4, Limits.MaxDocumentBytes, InOutBytes)
                || !EstimateValueBytes(Field.Value, Limits, Depth + 1, InOutNodes, InOutBytes))
            {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

FString SelectorPrefixKey(const TArray<FString>& Prefix)
{
    return FString::Join(Prefix, TEXT("\x1f"));
}

bool PrefixMatches(const TArray<FString>& Prefix, const TArray<FString>& Segments)
{
    if (Prefix.Num() > Segments.Num())
    {
        return false;
    }
    for (int32 Index = 0; Index < Prefix.Num(); ++Index)
    {
        if (Prefix[Index] != Segments[Index])
        {
            return false;
        }
    }
    return true;
}

FString Sha1(const FString& Material)
{
    const FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}
}

bool FUnrealMCPAssetFamilyLimits::IsValid() const
{
    return MaxDocumentRecords > 0 && MaxDocumentRecords <= 4096
        && MaxDocumentBytes > 0 && MaxDocumentBytes <= 4 * 1024 * 1024
        && MaxValueNodes > 0 && MaxValueNodes <= 65536
        && MaxValueDepth > 0 && MaxValueDepth <= 64
        && MaxSelectorRoutes > 0 && MaxSelectorRoutes <= 1024
        && MaxSelectorSegments > 0 && MaxSelectorSegments <= 32
        && MaxSnapshotContributions > 0 && MaxSnapshotContributions <= 4096
        && MaxSnapshotBytes > 0 && MaxSnapshotBytes <= 4 * 1024 * 1024;
}

bool FUnrealMCPAssetFamilyDocumentBuilder::Add(
    FUnrealMCPAssetFamilyValueRecord Record,
    FUnrealMCPError& OutError)
{
    if (!Limits.IsValid())
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family document limits are invalid"));
        return false;
    }
    if (!IsBoundedText(Record.Path, MaxSemanticPathBytes)
        || !IsBoundedText(Record.Type, MaxSemanticPathBytes)
        || !Record.Value.IsValid())
    {
        SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family semantic value record is invalid"));
        return false;
    }
    if (Paths.Contains(Record.Path))
    {
        SetError(OutError, TEXT("conflict"), TEXT("Asset-family semantic document path collision"));
        return false;
    }
    int32 RecordBytes = Utf8Bytes(Record.Path) + Utf8Bytes(Record.Type) + 4;
    int32 ValueNodes = 0;
    if (Records.Num() >= Limits.MaxDocumentRecords
        || RecordBytes > Limits.MaxDocumentBytes - UsedBytes
        || !EstimateValueBytes(Record.Value, Limits, 0, ValueNodes, RecordBytes)
        || RecordBytes > Limits.MaxDocumentBytes - UsedBytes)
    {
        SetError(OutError, TEXT("data_limit_exceeded"), TEXT("Asset-family semantic document exceeds its declared bounds"));
        return false;
    }
    UsedBytes += RecordBytes;
    Paths.Add(Record.Path);
    Records.Add(MoveTemp(Record));
    return true;
}

bool FUnrealMCPAssetFamilySelectorRouter::Register(
    FUnrealMCPAssetFamilySelectorRoute Route,
    FUnrealMCPError& OutError)
{
    if (bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family selector router is frozen"));
        return false;
    }
    if (!Limits.IsValid() || !IsStableIdentity(Route.Identity)
        || Route.Prefix.IsEmpty() || Route.Prefix.Num() > Limits.MaxSelectorSegments
        || (Route.bPageable && Route.bGraph))
    {
        SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family selector route is invalid"));
        return false;
    }
    for (const FString& Segment : Route.Prefix)
    {
        if (!IsBoundedText(Segment, MaxSemanticPathBytes))
        {
            SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family selector segment is invalid"));
            return false;
        }
    }
    const FString PrefixKey = SelectorPrefixKey(Route.Prefix);
    if (RouteIdentities.Contains(Route.Identity) || RoutePrefixes.Contains(PrefixKey))
    {
        SetError(OutError, TEXT("conflict"), TEXT("Asset-family selector route collision"));
        return false;
    }
    if (Routes.Num() >= Limits.MaxSelectorRoutes)
    {
        SetError(OutError, TEXT("data_limit_exceeded"), TEXT("Asset-family selector route limit exceeded"));
        return false;
    }
    RouteIdentities.Add(Route.Identity);
    RoutePrefixes.Add(PrefixKey);
    Routes.Add(MoveTemp(Route));
    return true;
}

bool FUnrealMCPAssetFamilySelectorRouter::Freeze(FUnrealMCPError& OutError)
{
    if (bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family selector router is already frozen"));
        return false;
    }
    Routes.Sort([](const FUnrealMCPAssetFamilySelectorRoute& Left, const FUnrealMCPAssetFamilySelectorRoute& Right)
    {
        const FString LeftPrefix = SelectorPrefixKey(Left.Prefix);
        const FString RightPrefix = SelectorPrefixKey(Right.Prefix);
        return LeftPrefix == RightPrefix ? Left.Identity < Right.Identity : LeftPrefix < RightPrefix;
    });
    bFrozen = true;
    return true;
}

const FUnrealMCPAssetFamilySelectorRoute* FUnrealMCPAssetFamilySelectorRouter::Resolve(
    const FUnrealMCPAssetFamilySelector& Selector,
    FUnrealMCPError& OutError) const
{
    if (!bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family selector router is not frozen"));
        return nullptr;
    }
    if (Selector.Segments.Num() > Limits.MaxSelectorSegments)
    {
        SetError(OutError, TEXT("data_limit_exceeded"), TEXT("Asset-family selector depth limit exceeded"));
        return nullptr;
    }
    const FUnrealMCPAssetFamilySelectorRoute* Best = nullptr;
    for (const FUnrealMCPAssetFamilySelectorRoute& Route : Routes)
    {
        if (PrefixMatches(Route.Prefix, Selector.Segments)
            && (Best == nullptr || Route.Prefix.Num() > Best->Prefix.Num()))
        {
            Best = &Route;
        }
    }
    if (Best == nullptr)
    {
        SetError(OutError, TEXT("invalid_argument"), TEXT("Unknown asset-family selector"));
    }
    return Best;
}

bool FUnrealMCPAssetFamilySnapshotBuilder::Add(
    const FString& Identity,
    const FString& Value,
    FUnrealMCPError& OutError)
{
    if (!Limits.IsValid() || !IsStableIdentity(Identity)
        || Utf8Bytes(Value) > Limits.MaxSnapshotBytes)
    {
        SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family snapshot contribution is invalid"));
        return false;
    }
    if (Contributions.Contains(Identity))
    {
        SetError(OutError, TEXT("conflict"), TEXT("Asset-family snapshot contribution collision"));
        return false;
    }
    const int32 Cost = Utf8Bytes(Identity) + Utf8Bytes(Value);
    if (Contributions.Num() >= Limits.MaxSnapshotContributions
        || Cost > Limits.MaxSnapshotBytes - UsedBytes)
    {
        SetError(OutError, TEXT("data_limit_exceeded"), TEXT("Asset-family snapshot exceeds its declared bounds"));
        return false;
    }
    UsedBytes += Cost;
    Contributions.Add(Identity, Value);
    return true;
}

FString FUnrealMCPAssetFamilySnapshotBuilder::BuildSnapshotId() const
{
    TArray<FString> Identities;
    Contributions.GetKeys(Identities);
    Identities.Sort();
    FString Material;
    for (const FString& Identity : Identities)
    {
        const FString& Value = Contributions.FindChecked(Identity);
        Material += FString::Printf(TEXT("%d:%s=%d:%s;"), Identity.Len(), *Identity, Value.Len(), *Value);
    }
    return Sha1(Material);
}

FUnrealMCPAssetFamilyRegistry::FUnrealMCPAssetFamilyRegistry(
    FUnrealMCPAssetFamilyModuleResolver InModuleResolver)
    : ModuleResolver(MoveTemp(InModuleResolver))
{
    if (!ModuleResolver)
    {
        ModuleResolver = [](FName ModuleName)
        {
            return FModuleManager::Get().IsModuleLoaded(ModuleName);
        };
    }
}

bool FUnrealMCPAssetFamilyRegistry::ValidateDescriptor(
    const FUnrealMCPAssetFamilyDescriptor& Descriptor,
    FUnrealMCPError& OutError) const
{
    if (!IsStableIdentity(Descriptor.FamilyId) || Descriptor.NativeClass == nullptr
        || FMath::Abs(Descriptor.Priority) > MaxFamilyPriority || !Descriptor.Bounds.IsValid())
    {
        SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family descriptor identity, class, priority, or bounds are invalid"));
        return false;
    }
    if (Descriptor.Capabilities.bInspection != Descriptor.InspectionAdapter.IsValid()
        || Descriptor.Capabilities.bCreation != Descriptor.CreationAdapter.IsValid()
        || Descriptor.Capabilities.bEditing != Descriptor.EditingAdapter.IsValid()
        || (Descriptor.bComposableInspectionOverlay && !Descriptor.Capabilities.bInspection))
    {
        SetError(OutError, TEXT("capability_mismatch"), TEXT("Asset-family capability declarations disagree with their adapters"));
        return false;
    }
    TSet<FName> Modules;
    for (const FName Module : Descriptor.RequiredModules)
    {
        if (Module.IsNone() || Modules.Contains(Module))
        {
            SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family required modules must be non-empty and unique"));
            return false;
        }
        Modules.Add(Module);
    }
    TSet<FString> Limits;
    for (const FUnrealMCPAssetFamilyLimit& Limit : Descriptor.Limits)
    {
        if (!IsStableIdentity(Limit.Name) || Limit.Value <= 0 || Limits.Contains(Limit.Name))
        {
            SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family limits must have unique stable names and positive values"));
            return false;
        }
        Limits.Add(Limit.Name);
    }
    FUnrealMCPAssetFamilySelectorRouter DeclaredRoutes(Descriptor.Bounds);
    for (const FUnrealMCPAssetFamilySelectorRoute& Route : Descriptor.SelectorRoutes)
    {
        if (!DeclaredRoutes.Register(Route, OutError))
        {
            return false;
        }
    }
    if (!DeclaredRoutes.Freeze(OutError))
    {
        return false;
    }
    return true;
}

bool FUnrealMCPAssetFamilyRegistry::Register(
    FUnrealMCPAssetFamilyDescriptor Descriptor,
    FUnrealMCPError& OutError)
{
    if (bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family registry is frozen"));
        return false;
    }
    if (!ValidateDescriptor(Descriptor, OutError))
    {
        return false;
    }
    for (const FUnrealMCPAssetFamilyDescriptor& Existing : Descriptors)
    {
        if (Existing.FamilyId == Descriptor.FamilyId)
        {
            SetError(OutError, TEXT("conflict"), TEXT("Duplicate asset-family identity"));
            return false;
        }
        if (Existing.NativeClass == Descriptor.NativeClass
            && Existing.ClassPolicy == Descriptor.ClassPolicy
            && Existing.Priority == Descriptor.Priority)
        {
            SetError(OutError, TEXT("conflict"), TEXT("Asset-family classification collision"));
            return false;
        }
    }
    Descriptors.Add(MoveTemp(Descriptor));
    return true;
}

bool FUnrealMCPAssetFamilyRegistry::Freeze(FUnrealMCPError& OutError)
{
    if (bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family registry is already frozen"));
        return false;
    }
    for (const FUnrealMCPAssetFamilyDescriptor& Descriptor : Descriptors)
    {
        if (!ValidateDescriptor(Descriptor, OutError))
        {
            return false;
        }
    }
    Descriptors.Sort([](const FUnrealMCPAssetFamilyDescriptor& Left, const FUnrealMCPAssetFamilyDescriptor& Right)
    {
        if (Left.Priority != Right.Priority)
        {
            return Left.Priority > Right.Priority;
        }
        const FString LeftClass = Left.NativeClass->GetPathName();
        const FString RightClass = Right.NativeClass->GetPathName();
        return LeftClass == RightClass ? Left.FamilyId < Right.FamilyId : LeftClass < RightClass;
    });

    FString Material;
    for (FUnrealMCPAssetFamilyDescriptor& Descriptor : Descriptors)
    {
        Descriptor.RequiredModules.Sort([](const FName Left, const FName Right)
        {
            return Left.LexicalLess(Right);
        });
        Descriptor.Limits.Sort([](const FUnrealMCPAssetFamilyLimit& Left, const FUnrealMCPAssetFamilyLimit& Right)
        {
            return Left.Name < Right.Name;
        });
        Descriptor.SelectorRoutes.Sort([](
            const FUnrealMCPAssetFamilySelectorRoute& Left,
            const FUnrealMCPAssetFamilySelectorRoute& Right)
        {
            return Left.Identity < Right.Identity;
        });
        TArray<FName> MissingModules;
        for (const FName Module : Descriptor.RequiredModules)
        {
            if (!ModuleResolver(Module))
            {
                MissingModules.Add(Module);
            }
        }
        MissingModulesByFamily.Add(Descriptor.FamilyId, MoveTemp(MissingModules));

        Material += FString::Printf(
            TEXT("%s|%s|%d|%d|%d%d%d|%d|%d,%d,%d,%d,%d,%d,%d,%d|"),
            *Descriptor.FamilyId,
            *Descriptor.NativeClass->GetPathName(),
            static_cast<int32>(Descriptor.ClassPolicy),
            Descriptor.Priority,
            Descriptor.Capabilities.bInspection ? 1 : 0,
            Descriptor.Capabilities.bCreation ? 1 : 0,
            Descriptor.Capabilities.bEditing ? 1 : 0,
            Descriptor.bComposableInspectionOverlay ? 1 : 0,
            Descriptor.Bounds.MaxDocumentRecords,
            Descriptor.Bounds.MaxDocumentBytes,
            Descriptor.Bounds.MaxValueNodes,
            Descriptor.Bounds.MaxValueDepth,
            Descriptor.Bounds.MaxSelectorRoutes,
            Descriptor.Bounds.MaxSelectorSegments,
            Descriptor.Bounds.MaxSnapshotContributions,
            Descriptor.Bounds.MaxSnapshotBytes);
        for (const FName Module : Descriptor.RequiredModules)
        {
            Material += Module.ToString() + TEXT(",");
        }
        Material += TEXT("|");
        for (const FUnrealMCPAssetFamilyLimit& Limit : Descriptor.Limits)
        {
            Material += Limit.Name + TEXT("=") + LexToString(Limit.Value) + TEXT(",");
        }
        Material += TEXT("|");
        for (const FUnrealMCPAssetFamilySelectorRoute& Route : Descriptor.SelectorRoutes)
        {
            Material += Route.Identity + TEXT("=") + FString::Join(Route.Prefix, TEXT("/")) + TEXT(",");
        }
        Material += TEXT(";");
    }
    Fingerprint = Sha1(Material);
    bFrozen = true;
    return true;
}

bool FUnrealMCPAssetFamilyRegistry::SupportsCapability(
    const FUnrealMCPAssetFamilyDescriptor& Descriptor,
    EUnrealMCPAssetFamilyCapability Capability) const
{
    switch (Capability)
    {
    case EUnrealMCPAssetFamilyCapability::Inspection:
        return Descriptor.Capabilities.bInspection && Descriptor.InspectionAdapter.IsValid();
    case EUnrealMCPAssetFamilyCapability::Creation:
        return Descriptor.Capabilities.bCreation && Descriptor.CreationAdapter.IsValid();
    case EUnrealMCPAssetFamilyCapability::Editing:
        return Descriptor.Capabilities.bEditing && Descriptor.EditingAdapter.IsValid();
    }
    return false;
}

bool FUnrealMCPAssetFamilyRegistry::Select(
    const UClass* AssetClass,
    EUnrealMCPAssetFamilyCapability Capability,
    FUnrealMCPAssetFamilySelection& OutSelection,
    FUnrealMCPError& OutError) const
{
    return SelectSingle(AssetClass, Capability, false, OutSelection, OutError);
}

bool FUnrealMCPAssetFamilyRegistry::SelectPrimary(
    const UClass* AssetClass,
    EUnrealMCPAssetFamilyCapability Capability,
    FUnrealMCPAssetFamilySelection& OutSelection,
    FUnrealMCPError& OutError) const
{
    return SelectSingle(AssetClass, Capability, true, OutSelection, OutError);
}

bool FUnrealMCPAssetFamilyRegistry::SelectSingle(
    const UClass* AssetClass,
    EUnrealMCPAssetFamilyCapability Capability,
    bool bPrimaryOnly,
    FUnrealMCPAssetFamilySelection& OutSelection,
    FUnrealMCPError& OutError) const
{
    OutSelection = {};
    if (!bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family registry is not frozen"));
        return false;
    }
    if (AssetClass == nullptr)
    {
        SetError(OutError, TEXT("invalid_argument"), TEXT("Asset-family selection requires a resolved class"));
        return false;
    }

    int32 BestPriority = MIN_int32;
    TArray<const FUnrealMCPAssetFamilyDescriptor*> Best;
    for (const FUnrealMCPAssetFamilyDescriptor& Descriptor : Descriptors)
    {
        if (bPrimaryOnly && Descriptor.bComposableInspectionOverlay)
        {
            continue;
        }
        const bool bMatches = Descriptor.ClassPolicy == EUnrealMCPAssetFamilyClassPolicy::Exact
            ? AssetClass == Descriptor.NativeClass
            : AssetClass->IsChildOf(Descriptor.NativeClass);
        if (!bMatches || Descriptor.Priority < BestPriority)
        {
            continue;
        }
        if (Descriptor.Priority > BestPriority)
        {
            BestPriority = Descriptor.Priority;
            Best.Reset();
        }
        Best.Add(&Descriptor);
    }
    if (Best.IsEmpty())
    {
        SetError(OutError, TEXT("unsupported_operation"), TEXT("No built-in asset family matches the resolved class"));
        return false;
    }
    if (Best.Num() != 1)
    {
        SetError(OutError, TEXT("ambiguous_classification"), TEXT("Multiple built-in asset families match at the same priority"));
        return false;
    }

    OutSelection.Descriptor = Best[0];
    OutSelection.MissingModules = MissingModulesByFamily.FindRef(Best[0]->FamilyId);
    if (!OutSelection.MissingModules.IsEmpty())
    {
        SetError(OutError, TEXT("dependency_unavailable"), TEXT("The selected asset family has unavailable required modules"));
        return false;
    }
    if (!SupportsCapability(*Best[0], Capability))
    {
        SetError(OutError, TEXT("unsupported_operation"), TEXT("The selected asset family does not declare the requested capability"));
        return false;
    }
    return true;
}

bool FUnrealMCPAssetFamilyRegistry::SelectInspectionOverlays(
    const UClass* SemanticClass,
    TArray<const FUnrealMCPAssetFamilyDescriptor*>& OutDescriptors,
    FUnrealMCPError& OutError) const
{
    OutDescriptors.Reset();
    if (!bFrozen)
    {
        SetError(OutError, TEXT("invalid_state"), TEXT("Asset-family registry is not frozen"));
        return false;
    }
    if (SemanticClass == nullptr)
    {
        return true;
    }
    for (const FUnrealMCPAssetFamilyDescriptor& Descriptor : Descriptors)
    {
        if (!Descriptor.bComposableInspectionOverlay
            || !SupportsCapability(Descriptor, EUnrealMCPAssetFamilyCapability::Inspection))
        {
            continue;
        }
        const bool bMatches = Descriptor.ClassPolicy == EUnrealMCPAssetFamilyClassPolicy::Exact
            ? SemanticClass == Descriptor.NativeClass
            : SemanticClass->IsChildOf(Descriptor.NativeClass);
        if (!bMatches)
        {
            continue;
        }
        if (!MissingModulesByFamily.FindRef(Descriptor.FamilyId).IsEmpty())
        {
            SetError(OutError, TEXT("dependency_unavailable"),
                TEXT("A matching companion asset family has unavailable required modules"));
            OutDescriptors.Reset();
            return false;
        }
        OutDescriptors.Add(&Descriptor);
    }
    return true;
}
