#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UPanelSlot;

class FUnrealMCPWidgetLayoutService
{
public:
    explicit FUnrealMCPWidgetLayoutService(
        FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

    static TArray<FString> SupportedProperties(const UPanelSlot* Slot);
    static TSharedRef<FJsonObject> Encode(const UPanelSlot* Slot);
    static FString Fingerprint(const UPanelSlot* Slot);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
