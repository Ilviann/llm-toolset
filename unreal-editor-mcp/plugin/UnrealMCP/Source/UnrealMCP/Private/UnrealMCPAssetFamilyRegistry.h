#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UnrealMCPWireTypes.h"

class UClass;
class UObject;

enum class EUnrealMCPAssetFamilyClassPolicy : uint8
{
    Exact,
    ExactAndDerived,
};

enum class EUnrealMCPAssetFamilyCapability : uint8
{
    Inspection,
    Creation,
    Editing,
};

struct FUnrealMCPAssetFamilyLimits
{
    int32 MaxDocumentRecords = 256;
    int32 MaxDocumentBytes = 64 * 1024;
    int32 MaxValueNodes = 4096;
    int32 MaxValueDepth = 16;
    int32 MaxSelectorRoutes = 64;
    int32 MaxSelectorSegments = 8;
    int32 MaxSnapshotContributions = 128;
    int32 MaxSnapshotBytes = 64 * 1024;

    bool IsValid() const;
};

struct FUnrealMCPAssetFamilyValueRecord
{
    FString Path;
    FString Type;
    TSharedPtr<FUnrealMCPValue> Value;
};

class FUnrealMCPAssetFamilyDocumentBuilder
{
public:
    explicit FUnrealMCPAssetFamilyDocumentBuilder(FUnrealMCPAssetFamilyLimits InLimits)
        : Limits(MoveTemp(InLimits)) {}

    bool Add(FUnrealMCPAssetFamilyValueRecord Record, FUnrealMCPError& OutError);
    const TArray<FUnrealMCPAssetFamilyValueRecord>& GetRecords() const { return Records; }
    int32 GetUsedBytes() const { return UsedBytes; }

private:
    FUnrealMCPAssetFamilyLimits Limits;
    TArray<FUnrealMCPAssetFamilyValueRecord> Records;
    TSet<FString> Paths;
    int32 UsedBytes = 0;
};

struct FUnrealMCPAssetFamilySelector
{
    TArray<FString> Segments;

    bool IsRoot() const { return Segments.IsEmpty(); }
};

struct FUnrealMCPAssetFamilySelectorRoute
{
    FString Identity;
    TArray<FString> Prefix;
    bool bPageable = false;
    bool bGraph = false;
};

class FUnrealMCPAssetFamilySelectorRouter
{
public:
    explicit FUnrealMCPAssetFamilySelectorRouter(FUnrealMCPAssetFamilyLimits InLimits)
        : Limits(MoveTemp(InLimits)) {}

    bool Register(FUnrealMCPAssetFamilySelectorRoute Route, FUnrealMCPError& OutError);
    bool Freeze(FUnrealMCPError& OutError);
    const FUnrealMCPAssetFamilySelectorRoute* Resolve(
        const FUnrealMCPAssetFamilySelector& Selector,
        FUnrealMCPError& OutError) const;
    const TArray<FUnrealMCPAssetFamilySelectorRoute>& GetRoutes() const { return Routes; }
    bool IsFrozen() const { return bFrozen; }

private:
    FUnrealMCPAssetFamilyLimits Limits;
    TArray<FUnrealMCPAssetFamilySelectorRoute> Routes;
    TSet<FString> RouteIdentities;
    TSet<FString> RoutePrefixes;
    bool bFrozen = false;
};

class FUnrealMCPAssetFamilySnapshotBuilder
{
public:
    explicit FUnrealMCPAssetFamilySnapshotBuilder(FUnrealMCPAssetFamilyLimits InLimits)
        : Limits(MoveTemp(InLimits)) {}

    bool Add(const FString& Identity, const FString& Value, FUnrealMCPError& OutError);
    FString BuildSnapshotId() const;

private:
    FUnrealMCPAssetFamilyLimits Limits;
    TMap<FString, FString> Contributions;
    int32 UsedBytes = 0;
};

struct FUnrealMCPAssetFamilyIdentity
{
    FString ObjectPath;
    FString SnapshotId;
};

struct FUnrealMCPAssetFamilyInspectionContext
{
    UObject* Asset = nullptr;
    FUnrealMCPAssetFamilyIdentity Identity;
    FUnrealMCPAssetFamilySelector Selector;
    int32 PageIndex = 0;
    int32 PageSize = 10;
    bool bVerbose = false;
    bool bAllowPartialGraph = false;
    bool bHasPaging = false;
    bool bHasPartialGraphFlag = false;
};

struct FUnrealMCPAssetFamilyCreationContext
{
    UObject* Outer = nullptr;
    UClass* AssetClass = nullptr;
    FName AssetName;
    FString CanonicalObjectPath;
};

struct FUnrealMCPAssetFamilyEditContext
{
    UObject* Asset = nullptr;
    FUnrealMCPAssetFamilyIdentity Identity;
    FString Operation;
    TArray<FUnrealMCPAssetFamilyValueRecord> Values;
};

class IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    virtual ~IUnrealMCPAssetFamilyInspectionAdapter() = default;
    virtual bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) = 0;
};

class IUnrealMCPAssetFamilyCreationAdapter
{
public:
    virtual ~IUnrealMCPAssetFamilyCreationAdapter() = default;
    virtual bool Create(
        const FUnrealMCPAssetFamilyCreationContext& Context,
        UObject*& OutAsset,
        FUnrealMCPAssetFamilyDocumentBuilder& ReadBack,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) = 0;
};

class IUnrealMCPAssetFamilyEditingAdapter
{
public:
    virtual ~IUnrealMCPAssetFamilyEditingAdapter() = default;
    virtual bool Edit(
        const FUnrealMCPAssetFamilyEditContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& ReadBack,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) = 0;
};

struct FUnrealMCPAssetFamilyCapabilities
{
    bool bInspection = false;
    bool bCreation = false;
    bool bEditing = false;
};

struct FUnrealMCPAssetFamilyLimit
{
    FString Name;
    int32 Value = 0;
};

struct FUnrealMCPAssetFamilyDescriptor
{
    FString FamilyId;
    UClass* NativeClass = nullptr;
    EUnrealMCPAssetFamilyClassPolicy ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::Exact;
    int32 Priority = 0;
    TArray<FName> RequiredModules;
    FUnrealMCPAssetFamilyLimits Bounds;
    TArray<FUnrealMCPAssetFamilyLimit> Limits;
    FUnrealMCPAssetFamilyCapabilities Capabilities;
    TSharedPtr<IUnrealMCPAssetFamilyInspectionAdapter> InspectionAdapter;
    TSharedPtr<IUnrealMCPAssetFamilyCreationAdapter> CreationAdapter;
    TSharedPtr<IUnrealMCPAssetFamilyEditingAdapter> EditingAdapter;
};

struct FUnrealMCPAssetFamilySelection
{
    const FUnrealMCPAssetFamilyDescriptor* Descriptor = nullptr;
    TArray<FName> MissingModules;
};

using FUnrealMCPAssetFamilyModuleResolver = TFunction<bool(FName)>;

class FUnrealMCPAssetFamilyRegistry
{
public:
    explicit FUnrealMCPAssetFamilyRegistry(
        FUnrealMCPAssetFamilyModuleResolver InModuleResolver = FUnrealMCPAssetFamilyModuleResolver());

    bool Register(FUnrealMCPAssetFamilyDescriptor Descriptor, FUnrealMCPError& OutError);
    bool Freeze(FUnrealMCPError& OutError);
    bool Select(
        const UClass* AssetClass,
        EUnrealMCPAssetFamilyCapability Capability,
        FUnrealMCPAssetFamilySelection& OutSelection,
        FUnrealMCPError& OutError) const;

    bool IsFrozen() const { return bFrozen; }
    const TArray<FUnrealMCPAssetFamilyDescriptor>& GetDescriptors() const { return Descriptors; }
    const FString& GetFingerprint() const { return Fingerprint; }

private:
    bool ValidateDescriptor(const FUnrealMCPAssetFamilyDescriptor& Descriptor, FUnrealMCPError& OutError) const;
    bool SupportsCapability(
        const FUnrealMCPAssetFamilyDescriptor& Descriptor,
        EUnrealMCPAssetFamilyCapability Capability) const;

    FUnrealMCPAssetFamilyModuleResolver ModuleResolver;
    TArray<FUnrealMCPAssetFamilyDescriptor> Descriptors;
    TMap<FString, TArray<FName>> MissingModulesByFamily;
    FString Fingerprint;
    bool bFrozen = false;
};
