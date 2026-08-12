#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPAssetDeletionService;
class FUnrealMCPAssetFamilyRegistry;
class FUnrealMCPAssetInspectionService;
class FUnrealMCPAssetReferenceService;
class FUnrealMCPBlueprintActionCatalog;
class FUnrealMCPBlueprintBlockReplacementService;
class FUnrealMCPBlueprintGraphEditor;
class FUnrealMCPBlueprintInspector;
class FUnrealMCPBlueprintMutator;
class FUnrealMCPExtensionRegistry;
class FUnrealMCPGameDataService;
class FUnrealMCPGameplayFrameworkEditor;
class FUnrealMCPLevelActorEditingService;
class FUnrealMCPLevelManagementService;
class FUnrealMCPLevelService;
class FUnrealMCPOperationLedger;
class FUnrealMCPWidgetTreeService;

enum class EUnrealMCPCommandAccess : uint8
{
    ReadOnly,
    Writable,
    Internal,
};

enum class EUnrealMCPRetainedOperationPolicy : uint8
{
    None,
    Retained,
};

enum class EUnrealMCPCommandDispatch : uint8
{
    RequestThread,
    GameThread,
};

enum class EUnrealMCPCommandSource : uint8
{
    FixedNative,
    Runtime,
};

using FUnrealMCPCommandHandler = TFunction<bool(
    const TSharedPtr<FUnrealMCPRecord>&,
    TSharedPtr<FUnrealMCPRecord>&,
    FUnrealMCPError&)>;

struct FUnrealMCPNativeFeature
{
    FString Name;
    TFunction<bool()> Resolve;
};

struct FUnrealMCPNativeLimit
{
    FString Name;
    double Value = 0.0;
};

struct FUnrealMCPCommandDescriptor
{
    FString Identity;
    EUnrealMCPCommandAccess Access = EUnrealMCPCommandAccess::ReadOnly;
    EUnrealMCPRetainedOperationPolicy RetainedOperation = EUnrealMCPRetainedOperationPolicy::None;
    EUnrealMCPCommandDispatch Dispatch = EUnrealMCPCommandDispatch::GameThread;
    bool bAllowsExtensionRequests = false;
    FUnrealMCPCommandHandler Handler;
    TArray<FUnrealMCPNativeFeature> Features;
    TArray<FUnrealMCPNativeLimit> Limits;
};

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
    void Build(FUnrealMCPCommandHostHandlers HostHandlers);
    bool Register(FUnrealMCPCommandDescriptor Descriptor);
    bool RejectConcurrentRetainedOperation(const TCHAR* Message, FUnrealMCPError& OutError) const;

    bool ExecuteAssetInspect(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteAssetReferences(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteAssetDelete(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteLevelInspect(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteLevelOpen(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteLevelManage(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteLevelActorEdit(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteLevelSave(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteBlueprintActionCatalog(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteBlueprintGraphEdit(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteBlueprintBlockReplace(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteBlueprintMutation(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteWidgetTreeEdit(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteGameplayFrameworkEdit(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteGameDataInspect(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool ExecuteGameDataEdit(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);

    FUnrealMCPCommandCatalogBuilder Catalog;
    FString ProjectHash;
    FString BridgeInstanceId;
    FUnrealMCPOperationLedger& OperationLedger;
    TSharedRef<FUnrealMCPAssetFamilyRegistry> AssetFamilyRegistry;
    TSharedRef<FUnrealMCPExtensionRegistry> ExtensionRegistry;
    FString InitializationError;
    TUniquePtr<FUnrealMCPBlueprintInspector> BlueprintInspector;
    TUniquePtr<FUnrealMCPBlueprintActionCatalog> BlueprintActionCatalog;
    TUniquePtr<FUnrealMCPBlueprintGraphEditor> BlueprintGraphEditor;
    TUniquePtr<FUnrealMCPBlueprintBlockReplacementService> BlueprintBlockReplacementService;
    TUniquePtr<FUnrealMCPBlueprintMutator> BlueprintMutator;
    TUniquePtr<FUnrealMCPWidgetTreeService> WidgetTreeService;
    TUniquePtr<FUnrealMCPGameplayFrameworkEditor> GameplayFrameworkEditor;
    TUniquePtr<FUnrealMCPGameDataService> GameDataService;
    TUniquePtr<FUnrealMCPLevelService> LevelService;
    TUniquePtr<FUnrealMCPLevelManagementService> LevelManagementService;
    TUniquePtr<FUnrealMCPLevelActorEditingService> LevelActorEditingService;
    TUniquePtr<FUnrealMCPAssetReferenceService> AssetReferenceService;
    TUniquePtr<FUnrealMCPAssetDeletionService> AssetDeletionService;
    TUniquePtr<FUnrealMCPAssetInspectionService> AssetInspectionService;
};
