#pragma once

#include "CoreMinimal.h"

class FJsonObject;
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
    TSharedPtr<FJsonObject> Details;
    bool bRetryable = false;
};

class UNREALMCP_API IUnrealMCPExtensionHandler
{
public:
    virtual ~IUnrealMCPExtensionHandler() = default;

    virtual bool IsReady(FString& OutUnavailableReason) const = 0;
    virtual bool SupportsTarget(const UObject& Target) const = 0;
    virtual bool ValidateArguments(
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        FUnrealMCPExtensionError& OutError) const = 0;
    virtual bool Inspect(
        const UObject& Target,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPExtensionError& OutError) = 0;
    virtual bool AppendFingerprint(
        const UObject& Target,
        const FString& Operation,
        FString& OutFingerprint,
        FUnrealMCPExtensionError& OutError) const = 0;
    virtual bool ApplyMutation(
        UObject& Target,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutChange,
        FUnrealMCPExtensionError& OutError) = 0;
    virtual bool ReadBack(
        const UObject& Target,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPExtensionError& OutError) const = 0;
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
