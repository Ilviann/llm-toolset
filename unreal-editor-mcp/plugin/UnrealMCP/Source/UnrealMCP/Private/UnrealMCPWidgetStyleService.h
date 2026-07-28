#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UWidget;

class FUnrealMCPWidgetStyleService
{
public:
    explicit FUnrealMCPWidgetStyleService(
        FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

    static TArray<FString> SupportedProperties(const UWidget* Widget);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
