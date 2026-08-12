#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UWidget;

class FUnrealMCPWidgetStyleService
{
public:
    explicit FUnrealMCPWidgetStyleService(
        FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

    static TArray<FString> SupportedProperties(const UWidget* Widget);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
