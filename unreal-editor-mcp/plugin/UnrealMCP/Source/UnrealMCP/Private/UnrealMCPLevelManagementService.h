#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPLevelService;

class FUnrealMCPLevelManagementService
{
public:
    FUnrealMCPLevelManagementService(
        FString InProjectHash,
        FUnrealMCPLevelService& InLevels);

    bool Manage(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    FString ProjectHash;
    FUnrealMCPLevelService& Levels;
};
