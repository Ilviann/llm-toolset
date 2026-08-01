#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPLevelService;

class FUnrealMCPLevelManagementService
{
public:
    FUnrealMCPLevelManagementService(
        FString InProjectHash,
        FUnrealMCPLevelService& InLevels);

    bool Manage(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FString ProjectHash;
    FUnrealMCPLevelService& Levels;
};
