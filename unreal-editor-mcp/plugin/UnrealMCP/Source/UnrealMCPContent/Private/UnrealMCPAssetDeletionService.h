#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPAssetReferenceService;

class FUnrealMCPAssetDeletionService
{
public:
    explicit FUnrealMCPAssetDeletionService(FUnrealMCPAssetReferenceService& InReferences);

    bool Delete(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPAssetReferenceService& References;
};
