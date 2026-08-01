#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"

class UPanelWidget;
class UUserWidget;
class UWidget;
class UWidgetBlueprint;

class FUnrealMCPWidgetCompatibility
{
public:
    static UWidgetBlueprint* CreateWidgetBlueprint(
        UObject* Parent,
        FName Name,
        EBlueprintType BlueprintType,
        UClass* ParentClass,
        UClass* RootWidgetClass,
        FName CallingContext,
        bool bRegisterAndCompile);

    static bool AddWidget(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget,
        UWidget* Parent,
        int32 ChildIndex,
        FText& OutError);
    static bool MoveWidget(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget,
        UPanelWidget* NewParent,
        int32 ChildIndex,
        FText& OutError);
    static bool RemoveWidget(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget,
        FText& OutError);
    static bool VerifyWidgetRename(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget,
        const FText& NewName,
        FText& OutError);
    static bool RenameWidget(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget,
        const FString& NewName);
    static void ToggleWidgetAsVariable(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget,
        bool bIsVariable,
        bool bMarkBlueprintModified);
    static bool BindToEventProperty(
        UWidgetBlueprint* Blueprint,
        FName EventName,
        FName PropertyName,
        UClass* PropertyClass,
        bool bShouldJumpToNode,
        FText& OutError);
    static bool IsParentChildCycleFree(
        UWidgetBlueprint* Blueprint,
        UWidget* Child,
        UWidget* Parent);
    static void RemoveTransientWidgetFromTree(
        UWidgetBlueprint* Blueprint,
        UWidget* Widget);
};
