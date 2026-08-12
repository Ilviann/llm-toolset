#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UPanelSlot;

class FUnrealMCPWidgetLayoutService
{
public:
    explicit FUnrealMCPWidgetLayoutService(
        FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

    static TArray<FString> SupportedProperties(const UPanelSlot* Slot);
    static TSharedRef<FUnrealMCPRecord> Encode(const UPanelSlot* Slot);
    static FString Fingerprint(const UPanelSlot* Slot);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
