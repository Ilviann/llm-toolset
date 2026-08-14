#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/Function.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPAssetFamilyRegistry;
class UBlueprint;
class UClass;

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
    int32 Order = INDEX_NONE;
    EUnrealMCPCommandAccess Access = EUnrealMCPCommandAccess::ReadOnly;
    EUnrealMCPRetainedOperationPolicy RetainedOperation = EUnrealMCPRetainedOperationPolicy::None;
    EUnrealMCPCommandDispatch Dispatch = EUnrealMCPCommandDispatch::GameThread;
    bool bAllowsExtensionRequests = false;
    FUnrealMCPCommandHandler Handler;
    TArray<FUnrealMCPNativeFeature> Features;
    TArray<FUnrealMCPNativeLimit> Limits;
};

using FUnrealMCPBlueprintFamilyProvider = TFunction<TArray<TSharedPtr<FUnrealMCPValue>>() >;

class IUnrealMCPBlueprintExtensionProvider
{
public:
    virtual ~IUnrealMCPBlueprintExtensionProvider() = default;
    virtual bool ClassifyBlueprintClass(
        const UClass* Class,
        FString& OutFamily,
        FString& OutNativeBaseClass) const = 0;
    virtual bool AppendBlueprintInspection(
        const UBlueprint& Blueprint,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
        TArray<FString>& OutFingerprint,
        TSharedPtr<FUnrealMCPRecord>& InOutFamilyCapabilities,
        FUnrealMCPError& OutError) const = 0;
};

class FUnrealMCPDomainRegistrar
{
public:
    using FRegisterCommand = TFunction<bool(FUnrealMCPCommandDescriptor, FString&)>;
    using FRegisterFeature = TFunction<bool(FUnrealMCPNativeFeature, FString&)>;
    using FRegisterLimit = TFunction<bool(FUnrealMCPNativeLimit, FString&)>;
    using FRegisterBlueprintFamilies = TFunction<bool(FUnrealMCPBlueprintFamilyProvider, FString&)>;

    FUnrealMCPDomainRegistrar(
        FRegisterCommand InRegisterCommand,
        FRegisterFeature InRegisterFeature,
        FRegisterLimit InRegisterLimit,
        FRegisterBlueprintFamilies InRegisterBlueprintFamilies)
        : RegisterCommandDelegate(MoveTemp(InRegisterCommand))
        , RegisterFeatureDelegate(MoveTemp(InRegisterFeature))
        , RegisterLimitDelegate(MoveTemp(InRegisterLimit))
        , RegisterBlueprintFamiliesDelegate(MoveTemp(InRegisterBlueprintFamilies))
    {
    }

    bool RegisterCommand(FUnrealMCPCommandDescriptor Descriptor, FString& OutError) const
    {
        return RegisterCommandDelegate(MoveTemp(Descriptor), OutError);
    }
    bool RegisterFeature(FUnrealMCPNativeFeature Feature, FString& OutError) const
    {
        return RegisterFeatureDelegate(MoveTemp(Feature), OutError);
    }
    bool RegisterLimit(FUnrealMCPNativeLimit Limit, FString& OutError) const
    {
        return RegisterLimitDelegate(MoveTemp(Limit), OutError);
    }
    bool RegisterBlueprintFamilies(FUnrealMCPBlueprintFamilyProvider Provider, FString& OutError) const
    {
        return RegisterBlueprintFamiliesDelegate(MoveTemp(Provider), OutError);
    }

private:
    FRegisterCommand RegisterCommandDelegate;
    FRegisterFeature RegisterFeatureDelegate;
    FRegisterLimit RegisterLimitDelegate;
    FRegisterBlueprintFamilies RegisterBlueprintFamiliesDelegate;
};

struct FUnrealMCPDomainContext
{
    FString ProjectHash;
    FString BridgeInstanceId;
    TSharedRef<FUnrealMCPAssetFamilyRegistry> AssetFamilyRegistry;
    IUnrealMCPBlueprintExtensionProvider& BlueprintExtensions;
    TFunction<bool(const TCHAR*, FUnrealMCPError&)> RejectConcurrentRetainedOperation;
};

class IUnrealMCPBuiltInDomainModule : public IModuleInterface
{
public:
    virtual FName GetDomainName() const = 0;
    virtual bool RegisterAssetFamilies(
        FUnrealMCPAssetFamilyRegistry& Registry,
        FUnrealMCPError& OutError) = 0;
    virtual bool RegisterCommands(
        const FUnrealMCPDomainRegistrar& Registrar,
        const FUnrealMCPDomainContext& Context,
        FString& OutError) = 0;
};
