#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPAssetReferenceService;

class FUnrealMCPAssetDeletionService
{
public:
    explicit FUnrealMCPAssetDeletionService(FUnrealMCPAssetReferenceService& InReferences);

    bool Delete(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPAssetReferenceService& References;
};
