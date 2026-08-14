#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class UPanelSlot;
class UWidget;
class UWidgetBlueprint;

namespace UnrealMCP::WidgetInspection
{
struct FBindingRecord
{
    FString WidgetId;
    FString Fingerprint;
    TSharedPtr<FUnrealMCPRecord> Record;
};

UNREALMCPBLUEPRINT_API TArray<FString> SupportedStyleProperties(
    const UWidget* Widget);
UNREALMCPBLUEPRINT_API TArray<FString> SupportedLayoutProperties(
    const UPanelSlot* Slot);
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> EncodeLayout(
    const UPanelSlot* Slot);
UNREALMCPBLUEPRINT_API FString FingerprintLayout(const UPanelSlot* Slot);
UNREALMCPBLUEPRINT_API bool CollectBindings(
    UWidgetBlueprint* Blueprint,
    TArray<FBindingRecord>& OutRecords,
    FUnrealMCPError& OutError);
}
