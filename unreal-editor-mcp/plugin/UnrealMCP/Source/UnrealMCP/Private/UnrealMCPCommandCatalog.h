#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UnrealMCPDomainModule.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPAssetFamilyRegistry;
class FUnrealMCPExtensionRegistry;
class FUnrealMCPOperationLedger;

class FUnrealMCPCommandCatalogBuilder
{
public:
    bool Register(
        FUnrealMCPCommandDescriptor Descriptor,
        EUnrealMCPCommandSource Source,
        bool bProvidesModelSchema,
        FString& OutError);
    bool Freeze(FString& OutError);

    const FUnrealMCPCommandDescriptor* Find(const FString& Command) const;
    FUnrealMCPCommandDescriptor* FindMutable(const FString& Command);
    const TArray<FUnrealMCPCommandDescriptor>& GetDescriptors() const { return Descriptors; }
    bool IsFrozen() const { return bFrozen; }
    TArray<TSharedPtr<FUnrealMCPValue>> BuildCommandNames() const;
    TSharedRef<FUnrealMCPRecord> BuildFeatures() const;
    TSharedRef<FUnrealMCPRecord> BuildLimits() const;

private:
    TArray<FUnrealMCPCommandDescriptor> Descriptors;
    TMap<FString, int32> CommandIndexes;
    TSet<FString> FeatureNames;
    TSet<FString> LimitNames;
    bool bFrozen = false;
};

struct FUnrealMCPCommandHostHandlers
{
    FUnrealMCPCommandHandler Capabilities;
    FUnrealMCPCommandHandler EditorState;
    FUnrealMCPCommandHandler EditorShutdown;
};

class FUnrealMCPCommandCatalog
{
public:
    FUnrealMCPCommandCatalog(
        FString InProjectHash,
        FString InBridgeInstanceId,
        FUnrealMCPOperationLedger& InOperationLedger,
        TSharedRef<FUnrealMCPAssetFamilyRegistry> InAssetFamilyRegistry,
        TSharedRef<FUnrealMCPExtensionRegistry> InExtensionRegistry,
        TArray<IUnrealMCPBuiltInDomainModule*> InDomainModules,
        FUnrealMCPCommandHostHandlers InHostHandlers);
    ~FUnrealMCPCommandCatalog();

    bool IsValid() const { return InitializationError.IsEmpty(); }
    const FString& GetInitializationError() const { return InitializationError; }
    const FUnrealMCPCommandDescriptor* Find(const FString& Command) const { return Catalog.Find(Command); }
    bool Execute(
        const FString& Command,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    TArray<TSharedPtr<FUnrealMCPValue>> BuildCommandNames() const { return Catalog.BuildCommandNames(); }
    TSharedRef<FUnrealMCPRecord> BuildFeatures() const { return Catalog.BuildFeatures(); }
    TSharedRef<FUnrealMCPRecord> BuildLimits() const { return Catalog.BuildLimits(); }
    TArray<TSharedPtr<FUnrealMCPValue>> BuildBlueprintFamilyCapabilities() const;

private:
    void Build(TArray<IUnrealMCPBuiltInDomainModule*> DomainModules, FUnrealMCPCommandHostHandlers HostHandlers);
    bool Register(FUnrealMCPCommandDescriptor Descriptor);
    bool RejectConcurrentRetainedOperation(const TCHAR* Message, FUnrealMCPError& OutError) const;

    FUnrealMCPCommandCatalogBuilder Catalog;
    FString ProjectHash;
    FString BridgeInstanceId;
    FUnrealMCPOperationLedger& OperationLedger;
    TSharedRef<FUnrealMCPAssetFamilyRegistry> AssetFamilyRegistry;
    TSharedRef<FUnrealMCPExtensionRegistry> ExtensionRegistry;
    FString InitializationError;
    FUnrealMCPBlueprintFamilyProvider BlueprintFamilyProvider;
};
