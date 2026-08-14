#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPBlueprintInspector;

class FUnrealMCPWidgetTreeService
{
public:
    explicit FUnrealMCPWidgetTreeService(FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
