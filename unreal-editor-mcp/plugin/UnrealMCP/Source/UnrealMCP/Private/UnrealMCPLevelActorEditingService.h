#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPLevelService;

class FUnrealMCPLevelActorEditingService
{
public:
    explicit FUnrealMCPLevelActorEditingService(FUnrealMCPLevelService& InLevels);

    bool Edit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool Save(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPLevelService& Levels;
};
