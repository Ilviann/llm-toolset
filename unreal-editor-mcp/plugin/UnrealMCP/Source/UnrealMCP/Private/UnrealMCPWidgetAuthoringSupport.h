#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FProperty;
class FUnrealMCPBlueprintInspector;
class UPanelSlot;
class UWidget;
class UWidgetBlueprint;

namespace UnrealMCP::WidgetAuthoring
{
bool HasOnlyAuthoringFields(
    const FUnrealMCPRecord& Arguments,
    std::initializer_list<const TCHAR*> Fields);

bool ResolveBlueprint(
    FUnrealMCPBlueprintInspector& Inspector,
    const FUnrealMCPRecord& Arguments,
    UWidgetBlueprint*& OutBlueprint,
    FString& OutObjectPath,
    FUnrealMCPError& OutError);

UWidget* FindWidget(UWidgetBlueprint* Blueprint, const FString& Id);
UPanelSlot* FindPanelSlot(UWidgetBlueprint* Blueprint, const FString& Id);

bool ResolveWidget(
    UWidgetBlueprint* Blueprint,
    const FUnrealMCPRecord& Arguments,
    UWidget*& OutWidget,
    FUnrealMCPError& OutError);

bool ApplyProperty(
    FUnrealMCPBlueprintInspector& Inspector,
    const FUnrealMCPRecord& Arguments,
    UWidgetBlueprint* Blueprint,
    const FString& ObjectPath,
    UObject* Target,
    FProperty* Property,
    const TSharedPtr<FUnrealMCPValue>& Value,
    const FString& TransactionLabel,
    TSharedPtr<FUnrealMCPRecord>& OutChanged,
    FString& OutSnapshot,
    FUnrealMCPError& OutError);

TSharedRef<FUnrealMCPRecord> EncodeProperty(UObject* Target, FProperty* Property);

TSharedRef<FUnrealMCPRecord> BuildResult(
    UWidgetBlueprint* Blueprint,
    const FString& ObjectPath,
    const FString& Operation,
    const FString& Snapshot,
    const FString& WidgetId,
    const TSharedPtr<FUnrealMCPRecord>& Changed);
}
