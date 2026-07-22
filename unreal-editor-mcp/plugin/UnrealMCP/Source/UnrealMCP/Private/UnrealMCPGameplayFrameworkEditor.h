#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPGameplayFrameworkEditor
{
public:
    explicit FUnrealMCPGameplayFrameworkEditor(FString InProjectHash)
        : ProjectHash(MoveTemp(InProjectHash))
    {
    }

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FString ProjectHash;
};
