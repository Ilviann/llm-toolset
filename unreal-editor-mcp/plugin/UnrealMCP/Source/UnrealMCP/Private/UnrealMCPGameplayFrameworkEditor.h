#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPGameplayFrameworkEditor
{
public:
    explicit FUnrealMCPGameplayFrameworkEditor(FString InProjectHash)
        : ProjectHash(MoveTemp(InProjectHash))
    {
    }

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    FString ProjectHash;
};
