#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPWireTypes.h"

class UObject;

enum class EUnrealMCPExtensionCategory : uint8
{
    AssetFamily,
    ComponentFamily,
    ExistingAssetContributor,
};

enum class EUnrealMCPExtensionAccess : uint8
{
    Read,
    Mutation,
};

enum class EUnrealMCPExtensionPersistence : uint8
{
    None,
    PackageSave,
    BlueprintCompileAndSave,
};

struct FUnrealMCPExtensionError
{
    FString Code;
    FString Message;
    TSharedPtr<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
    bool bRetryable = false;
};

/**
 * Typed operation seam retained for shipped companion behavior while companion
 * asset families are moved onto the common adapters. JSON and transport
 * encoding remain exclusively owned by the base plugin.
 */
class UNREALMCP_API IUnrealMCPExtensionHandler
{
public:
    virtual ~IUnrealMCPExtensionHandler() = default;

    virtual bool IsReady(FString& OutUnavailableReason) const = 0;
    virtual bool SupportsTarget(const UObject& Target) const = 0;
    virtual bool ValidateArguments(
        const FString& Operation,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        FUnrealMCPExtensionError& OutError) const = 0;
    virtual bool Inspect(
        const UObject& Target,
        const FString& Operation,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPExtensionError& OutError) = 0;
    virtual bool AppendFingerprint(
        const UObject& Target,
        const FString& Operation,
        FString& OutFingerprint,
        FUnrealMCPExtensionError& OutError) const = 0;
    virtual bool ApplyMutation(
        UObject& Target,
        const FString& Operation,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutChange,
        FUnrealMCPExtensionError& OutError) = 0;
    virtual bool ReadBack(
        const UObject& Target,
        const FString& Operation,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPExtensionError& OutError) const = 0;
};

/**
 * Complete API-v2 family contract. The base validates and freezes these
 * records, then owns exact target resolution, selector dispatch, transactions,
 * persistence, postcondition read-back, capability composition, and encoding.
 * Companions own only domain semantics behind the typed common adapters.
 */
struct FUnrealMCPCompanionAssetFamily
{
    FString FamilyId;
    FString NativeClassPath;
    EUnrealMCPAssetFamilyClassPolicy ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::Exact;
    int32 Priority = 0;
    TArray<FName> RequiredModules;
    FUnrealMCPAssetFamilyLimits Bounds;
    TArray<FUnrealMCPAssetFamilyLimit> Limits;
    FUnrealMCPAssetFamilyCapabilities Capabilities;
    TArray<FUnrealMCPAssetFamilySelectorRoute> SelectorRoutes;
    TArray<FString> StableNestedIdentityKinds;
    EUnrealMCPExtensionPersistence CreationPersistence = EUnrealMCPExtensionPersistence::None;
    EUnrealMCPExtensionPersistence EditingPersistence = EUnrealMCPExtensionPersistence::None;
    TSharedPtr<IUnrealMCPAssetFamilyInspectionAdapter> InspectionAdapter;
    TSharedPtr<IUnrealMCPAssetFamilyCreationAdapter> CreationAdapter;
    TSharedPtr<IUnrealMCPAssetFamilyEditingAdapter> EditingAdapter;
    TFunction<FString(UObject*)> SnapshotBuilder;
};

struct FUnrealMCPExtensionContribution
{
    FString ContributionId;
    EUnrealMCPExtensionCategory Category = EUnrealMCPExtensionCategory::ExistingAssetContributor;
    EUnrealMCPExtensionAccess Access = EUnrealMCPExtensionAccess::Read;
    EUnrealMCPExtensionPersistence Persistence = EUnrealMCPExtensionPersistence::None;
    FString ToolFamily;
    FString Operation;
    FString TargetFamily;
    FString TargetClassPath;
    bool bAllowDerivedTargetClasses = false;
    FString RequiredLiveCapability;
    TArray<FString> AllowedArgumentFields;
    TMap<FString, int32> StableLimits;
    TSharedPtr<IUnrealMCPExtensionHandler> Handler;
};

struct FUnrealMCPCompanionRegistration
{
    FString PluginName;
    FString ExtensionId;
    FString OwningModule;
    FString SemanticVersion;
    int32 CompanionApiVersion = 0;
    int32 ExtensionSchemaRevision = 0;
    TArray<FString> RequiredEnginePlugins;
    TArray<FString> RequiredEngineModules;
    TArray<FUnrealMCPCompanionAssetFamily> AssetFamilies;
    TArray<FUnrealMCPExtensionContribution> Contributions;
};

struct FUnrealMCPRegistrationHandle
{
    uint64 Value = 0;

    bool IsValid() const { return Value != 0; }
    bool operator==(const FUnrealMCPRegistrationHandle& Other) const { return Value == Other.Value; }
};

struct FUnrealMCPRegistrationResult
{
    bool bAccepted = false;
    FUnrealMCPRegistrationHandle Handle;
    FString Reason;
};
