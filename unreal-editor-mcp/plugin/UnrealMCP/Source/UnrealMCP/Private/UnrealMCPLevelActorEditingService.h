#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPLevelService;

class FUnrealMCPLevelActorEditingService
{
public:
    explicit FUnrealMCPLevelActorEditingService(FUnrealMCPLevelService& InLevels);

    bool Edit(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool Save(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPLevelService& Levels;
};
