#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FProperty;
class FUnrealMCPBlueprintInspector;
class UPanelSlot;
class UWidget;
class UWidgetBlueprint;

namespace UnrealMCP::WidgetAuthoring
{
bool HasOnlyAuthoringFields(
    const FJsonObject& Arguments,
    std::initializer_list<const TCHAR*> Fields);

bool ResolveBlueprint(
    FUnrealMCPBlueprintInspector& Inspector,
    const FJsonObject& Arguments,
    UWidgetBlueprint*& OutBlueprint,
    FString& OutObjectPath,
    FUnrealMCPError& OutError);

UWidget* FindWidget(UWidgetBlueprint* Blueprint, const FString& Id);
UPanelSlot* FindPanelSlot(UWidgetBlueprint* Blueprint, const FString& Id);

bool ResolveWidget(
    UWidgetBlueprint* Blueprint,
    const FJsonObject& Arguments,
    UWidget*& OutWidget,
    FUnrealMCPError& OutError);

bool ApplyProperty(
    UWidgetBlueprint* Blueprint,
    UObject* Target,
    FProperty* Property,
    const TSharedPtr<FJsonValue>& Value,
    const FString& TransactionLabel,
    TSharedPtr<FJsonObject>& OutChanged,
    FUnrealMCPError& OutError);

TSharedRef<FJsonObject> EncodeProperty(UObject* Target, FProperty* Property);

TSharedRef<FJsonObject> BuildResult(
    UWidgetBlueprint* Blueprint,
    const FString& ObjectPath,
    const FString& Operation,
    const FString& Snapshot,
    const FString& WidgetId,
    const TSharedPtr<FJsonObject>& Changed);
}
