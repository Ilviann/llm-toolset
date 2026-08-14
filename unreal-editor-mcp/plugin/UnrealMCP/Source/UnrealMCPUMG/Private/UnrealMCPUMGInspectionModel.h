#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class UPanelSlot;
class UUserWidget;
class UWidget;
class UWidgetBlueprint;
class UWidgetBlueprintGeneratedClass;

namespace UnrealMCP::UMGInspection::Private
{
struct FWidgetEntry
{
    UWidget* Widget = nullptr;
    UPanelSlot* Slot = nullptr;
    FString Name;
    FString ClassPath;
    FString Ownership;
    FString DeclaredBy;
    FString Parent;
    int32 ChildIndex = INDEX_NONE;
    bool bRoot = false;
    bool bVariable = false;
};

struct FNamedSlotEntry
{
    FString Host;
    FString Name;
    FString Content;
    FString Ownership;
    FString DeclaredBy;
    bool bAvailableToSubclasses = false;
    bool bExposedOnInstance = false;
};

struct FBindingEntry
{
    FString Kind;
    FString Widget;
    FString Target;
    FString SourceKind;
    FString SourceName;
    FString SourceType;
    FString TargetType;
    FString Signature;
    FString Graph;
    FString Event;
    FString DeclaredBy;
    FString Cost;
};

struct FModel
{
    UWidgetBlueprint* Blueprint = nullptr;
    UWidgetBlueprintGeneratedClass* GeneratedClass = nullptr;
    UUserWidget* Defaults = nullptr;
    FString RootWidget;
    int32 MaximumDepth = 0;
    TArray<FWidgetEntry> Widgets;
    TArray<FNamedSlotEntry> NamedSlots;
    TArray<FBindingEntry> Bindings;

    const FWidgetEntry* FindWidget(const FString& Name) const;
    TArray<FString> ChildrenOf(const FString& Parent) const;
    int32 BindingCountFor(const FString& Widget) const;
};

bool BuildModel(UWidgetBlueprint* Blueprint, FModel& OutModel, FUnrealMCPError& OutError);
FString BuildSnapshot(UObject* Asset);
TArray<FString> UserWidgetPropertyNames();
TArray<FString> WidgetPropertyNames(const UWidget* Widget);
TArray<FString> SlotPropertyNames(const UPanelSlot* Slot);
}
