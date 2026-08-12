#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UWidgetBlueprint;

struct FUnrealMCPWidgetBindingRecord
{
    FString WidgetId;
    FString Fingerprint;
    TSharedPtr<FUnrealMCPRecord> Record;
};

class FUnrealMCPWidgetBindingService
{
public:
    explicit FUnrealMCPWidgetBindingService(
        FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

    static bool Collect(
        UWidgetBlueprint* Blueprint,
        TArray<FUnrealMCPWidgetBindingRecord>& OutRecords,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
