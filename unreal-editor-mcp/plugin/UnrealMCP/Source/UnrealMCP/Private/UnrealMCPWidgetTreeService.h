#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;

class FUnrealMCPWidgetTreeService
{
public:
    explicit FUnrealMCPWidgetTreeService(FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
