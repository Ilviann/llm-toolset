#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UnrealMCPWireTypes.h"

class UObject;
class UPackage;

struct FUnrealMCPAssetCreationRequest
{
    FString OperationId;
    FString PackageName;
    FString ObjectPath;
};

struct FUnrealMCPAssetCreationHooks
{
    TFunction<bool(UPackage*, UObject*&, FUnrealMCPError&)> Create;
    TFunction<bool(UObject*, FUnrealMCPError&)> Finalize;
    TFunction<bool(UObject*, FUnrealMCPError&)> Persist;
    TFunction<bool(UObject*, FString&, FUnrealMCPError&)> ReadBack;
};

struct FUnrealMCPAssetCreationResult
{
    UObject* Asset = nullptr;
    FString ObjectPath;
    FString SnapshotId;
};

struct FUnrealMCPAssetEditRequest
{
    FString OperationId;
    FString ObjectPath;
    FString ExpectedSnapshot;
    FString TransactionLabel;
    UObject* Asset = nullptr;
    bool bPersist = true;
    bool bRequireChangedSnapshot = true;
};

struct FUnrealMCPAssetEditHooks
{
    TFunction<bool(UObject*, FUnrealMCPError&)> ValidateState;
    TFunction<bool(UObject*, FString&, FUnrealMCPError&)> ReadBack;
    TFunction<bool(UObject*, FUnrealMCPError&)> Mutate;
    TFunction<bool(UObject*, FUnrealMCPError&)> Persist;
};

struct FUnrealMCPAssetEditResult
{
    FString BeforeSnapshot;
    FString SnapshotId;
};

class FUnrealMCPAssetAuthoringKernel
{
public:
    static FString ObjectPathForPackage(const FString& PackageName);

    static bool ExecuteCreation(
        const FUnrealMCPAssetCreationRequest& Request,
        const FUnrealMCPAssetCreationHooks& Hooks,
        FUnrealMCPAssetCreationResult& OutResult,
        FUnrealMCPError& OutError);

    static bool ExecuteEdit(
        const FUnrealMCPAssetEditRequest& Request,
        const FUnrealMCPAssetEditHooks& Hooks,
        FUnrealMCPAssetEditResult& OutResult,
        FUnrealMCPError& OutError);

private:
    static bool ValidateOperationId(const FString& OperationId, FUnrealMCPError& OutError);
    static bool ValidateCanonicalTarget(
        const FString& PackageName,
        const FString& ObjectPath,
        FUnrealMCPError& OutError);
    static bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError);
    static bool ResolveWritableFilename(
        const FString& PackageName,
        FString& OutFilename,
        FUnrealMCPError& OutError);
    static bool DestinationExists(const FString& PackageName, const FString& ObjectPath);
    static void CleanupCreation(
        UPackage* Package,
        UObject* Asset,
        const FString& Filename,
        bool bPublished);
};
