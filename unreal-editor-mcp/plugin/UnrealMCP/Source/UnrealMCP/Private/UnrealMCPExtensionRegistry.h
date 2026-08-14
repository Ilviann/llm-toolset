#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPDomainModule.h"

class FUnrealMCPRecord;
class FUnrealMCPValue;
class IModuleInterface;
class UBlueprint;
class UClass;
struct FUnrealMCPError;

class FUnrealMCPExtensionRegistry final : public IUnrealMCPBlueprintExtensionProvider
{
public:
    explicit FUnrealMCPExtensionRegistry(
        TSharedPtr<FUnrealMCPAssetFamilyRegistry> InAssetFamilyRegistry = nullptr)
        : AssetFamilyRegistry(MoveTemp(InAssetFamilyRegistry)) {}

    void DiscoverAndLoad();
    void Freeze();
    void BeginShutdown();

    FUnrealMCPRegistrationResult Register(
        const FUnrealMCPCompanionRegistration& Registration,
        IModuleInterface& OwningModule);
    void Unregister(FUnrealMCPRegistrationHandle Handle, IModuleInterface& OwningModule);

    bool Execute(
        const FString& ToolFamily,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError) const;
    bool HasExtensionRequest(const TSharedPtr<FUnrealMCPRecord>& Arguments) const;
    TSharedPtr<FUnrealMCPRecord> BuildCapabilities() const;
    FString RegistrySignature() const;
    bool ClassifyBlueprintClass(
        const UClass* Class,
        FString& OutFamily,
        FString& OutNativeBaseClass) const override;
    bool AppendBlueprintInspection(
        const UBlueprint& Blueprint,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
        TArray<FString>& OutFingerprint,
        TSharedPtr<FUnrealMCPRecord>& InOutFamilyCapabilities,
        FUnrealMCPError& OutError) const override;
    TArray<TSharedPtr<FUnrealMCPValue>> BuildBlueprintFamilyCapabilities() const;
    bool HasReadyFamilyCapability(
        const FString& TargetFamily,
        EUnrealMCPExtensionAccess Access) const;

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
        const FUnrealMCPRecord& Arguments,
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
    const FUnrealMCPExtensionContribution* FindBlueprintFamilyContribution(
        const UClass* Class,
        const FAcceptedRecord*& OutOwner) const;

    TArray<FDescriptorRecord> Descriptors;
    TArray<FAcceptedRecord> Accepted;
    TArray<FString> Diagnostics;
    TSharedPtr<FUnrealMCPAssetFamilyRegistry> AssetFamilyRegistry;
    uint64 NextHandle = 1;
    bool bFrozen = false;
    bool bShuttingDown = false;
};
