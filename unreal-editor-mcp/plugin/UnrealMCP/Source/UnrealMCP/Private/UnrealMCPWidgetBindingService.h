#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UWidgetBlueprint;

struct FUnrealMCPWidgetBindingRecord
{
    FString WidgetId;
    FString Fingerprint;
    TSharedPtr<FJsonObject> Record;
};

class FUnrealMCPWidgetBindingService
{
public:
    explicit FUnrealMCPWidgetBindingService(
        FUnrealMCPBlueprintInspector& InInspector);

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

    static bool Collect(
        UWidgetBlueprint* Blueprint,
        TArray<FUnrealMCPWidgetBindingRecord>& OutRecords,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
};
