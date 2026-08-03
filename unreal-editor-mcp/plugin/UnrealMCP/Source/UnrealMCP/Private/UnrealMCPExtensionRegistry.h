#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPCompanionApi.h"

class FJsonObject;
class IModuleInterface;
struct FUnrealMCPError;

class FUnrealMCPExtensionRegistry
{
public:
    void DiscoverAndLoad();
    void Freeze();
    void BeginShutdown();

    FUnrealMCPRegistrationResult Register(
        const FUnrealMCPCompanionRegistration& Registration,
        IModuleInterface& OwningModule);
    void Unregister(FUnrealMCPRegistrationHandle Handle, IModuleInterface& OwningModule);

    bool Execute(
        const FString& ToolFamily,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError) const;
    bool HasExtensionRequest(const TSharedPtr<FJsonObject>& Arguments) const;
    TSharedPtr<FJsonObject> BuildCapabilities() const;
    FString RegistrySignature() const;

#if WITH_DEV_AUTOMATION_TESTS
    void AddDescriptorForTesting(
        const FUnrealMCPCompanionRegistration& Registration,
        bool bEnabled = true,
        const FString& UnavailableReason = FString());
    int32 AcceptedCountForTesting() const { return Accepted.Num(); }
#endif

private:
    struct FDescriptorRecord
    {
        FString PluginName;
        FString ExtensionId;
        FString OwningModule;
        FString SemanticVersion;
        int32 CompanionApiVersion = 0;
        int32 SchemaRevision = 0;
        TArray<FString> RequiredEnginePlugins;
        bool bEnabled = false;
        bool bRegistered = false;
        FString UnavailableReason;
    };

    struct FAcceptedRecord
    {
        FUnrealMCPRegistrationHandle Handle;
        FUnrealMCPCompanionRegistration Registration;
        IModuleInterface* OwningModule = nullptr;
    };

    static bool IsStableId(const FString& Value);
    static bool HasOnlyAllowedFields(
        const FJsonObject& Arguments,
        const FUnrealMCPExtensionContribution& Contribution);
    static FString SnapshotFor(
        const UObject& Target,
        const FString& Operation,
        const IUnrealMCPExtensionHandler& Handler,
        FUnrealMCPExtensionError& OutError);
    static bool NormalizeAssetPath(
        const FString& Input,
        FString& OutObjectPath,
        FString& OutPackageName);
    static bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError);
    static void ConvertError(const FUnrealMCPExtensionError& Input, FUnrealMCPError& Output);
    const FUnrealMCPExtensionContribution* FindContribution(
        const FString& ExtensionId,
        const FString& ToolFamily,
        const FString& Operation,
        const FAcceptedRecord*& OutOwner) const;

    TArray<FDescriptorRecord> Descriptors;
    TArray<FAcceptedRecord> Accepted;
    TArray<FString> Diagnostics;
    uint64 NextHandle = 1;
    bool bFrozen = false;
    bool bShuttingDown = false;
};
