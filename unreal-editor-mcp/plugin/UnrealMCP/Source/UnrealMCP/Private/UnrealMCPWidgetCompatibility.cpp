#include "UnrealMCPWidgetCompatibility.h"

#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "UMGEditorModule.h"
#include "UMGEditorProjectSettings.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

namespace UnrealMCPWidgetCompatibilityPrivate
{
bool CanAddToParent(UWidgetBlueprint* Blueprint, UWidget* Parent, UWidget* ExistingChild, FText& OutError)
{
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        OutError = FText::FromString(TEXT("The Widget Blueprint has no valid widget tree"));
        return false;
    }
    if (Parent == nullptr)
    {
        if (Blueprint->WidgetTree->RootWidget != nullptr)
        {
            OutError = FText::FromString(TEXT("The Widget Blueprint already has a root widget"));
            return false;
        }
        return true;
    }
    UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
    if (Panel == nullptr)
    {
        OutError = FText::FromString(TEXT("The selected widget cannot contain panel children"));
        return false;
    }
    if (ExistingChild == nullptr || ExistingChild->GetParent() != Panel)
    {
        if (!Panel->CanAddMoreChildren())
        {
            OutError = FText::FromString(TEXT("The selected panel cannot accept another child"));
            return false;
        }
    }
    return true;
}
}

UWidgetBlueprint* FUnrealMCPWidgetCompatibility::CreateWidgetBlueprint(
    UObject* Parent,
    FName Name,
    EBlueprintType BlueprintType,
    UClass* ParentClass,
    UClass* RootWidgetClass,
    FName CallingContext,
    bool bRegisterAndCompile)
{
    if (Parent == nullptr || Name.IsNone() || ParentClass == nullptr
        || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
    {
        return nullptr;
    }
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Parent,
        Name,
        BlueprintType,
        UWidgetBlueprint::StaticClass(),
        UWidgetBlueprintGeneratedClass::StaticClass(),
        CallingContext));
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    if (Blueprint->WidgetTree->RootWidget == nullptr && RootWidgetClass != nullptr
        && RootWidgetClass->IsChildOf(UPanelWidget::StaticClass()))
    {
        UWidget* Root = Blueprint->WidgetTree->ConstructWidget<UWidget>(RootWidgetClass);
        Blueprint->WidgetTree->RootWidget = Root;
        Blueprint->OnVariableAdded(Root->GetFName());
    }
    Blueprint->bCanCallInitializedWithoutPlayerContext =
        GetDefault<UUMGEditorProjectSettings>()->bCanCallInitializedWithoutPlayerContext;
    IUMGEditorModule::FWidgetBlueprintCreatedArgs Args;
    Args.ParentClass = ParentClass;
    Args.Blueprint = Blueprint;
    FModuleManager::LoadModuleChecked<IUMGEditorModule>(TEXT("UMGEditor"))
        .OnWidgetBlueprintCreated().Broadcast(Args);
    if (bRegisterAndCompile)
    {
        Blueprint->MarkPackageDirty();
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        FAssetRegistryModule::AssetCreated(Blueprint);
    }
    return Blueprint;
}

bool FUnrealMCPWidgetCompatibility::AddWidget(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    UWidget* Parent,
    int32 ChildIndex,
    FText& OutError)
{
    if (Widget == nullptr
        || !UnrealMCPWidgetCompatibilityPrivate::CanAddToParent(
            Blueprint, Parent, nullptr, OutError))
    {
        if (Blueprint != nullptr && Widget != nullptr)
        {
            RemoveTransientWidgetFromTree(Blueprint, Widget);
        }
        return false;
    }
    Blueprint->WidgetTree->SetFlags(RF_Transactional);
    Blueprint->WidgetTree->Modify();
    if (Parent == nullptr)
    {
        Blueprint->WidgetTree->RootWidget = Widget;
    }
    else
    {
        UPanelWidget* Panel = CastChecked<UPanelWidget>(Parent);
        Panel->SetFlags(RF_Transactional);
        Panel->Modify();
        UPanelSlot* Slot = ChildIndex >= 0
            ? Panel->InsertChildAt(ChildIndex, Widget)
            : Panel->AddChild(Widget);
        if (Slot == nullptr)
        {
            OutError = FText::FromString(TEXT("Unreal could not attach the widget to the selected panel"));
            RemoveTransientWidgetFromTree(Blueprint, Widget);
            return false;
        }
    }
    Blueprint->OnVariableAdded(Widget->GetFName());
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FUnrealMCPWidgetCompatibility::IsParentChildCycleFree(
    UWidgetBlueprint* Blueprint,
    UWidget* Child,
    UWidget* Parent)
{
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr || Child == nullptr || Parent == nullptr)
    {
        return true;
    }
    bool bCycle = false;
    Blueprint->WidgetTree->ForWidgetAndChildren(Child, [&bCycle, Parent](UWidget* Candidate)
    {
        bCycle = bCycle || Candidate == Parent;
    });
    return !bCycle;
}

bool FUnrealMCPWidgetCompatibility::MoveWidget(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    UPanelWidget* NewParent,
    int32 ChildIndex,
    FText& OutError)
{
    if (Widget == nullptr || NewParent == nullptr
        || !UnrealMCPWidgetCompatibilityPrivate::CanAddToParent(
            Blueprint, NewParent, Widget, OutError))
    {
        return false;
    }
    if (!IsParentChildCycleFree(Blueprint, Widget, NewParent))
    {
        OutError = FText::FromString(TEXT("A widget cannot become a child of its own descendants"));
        return false;
    }
    if (ChildIndex >= 0 && Widget->GetParent() == NewParent
        && NewParent->GetChildIndex(Widget) < ChildIndex)
    {
        --ChildIndex;
    }
    NewParent->SetFlags(RF_Transactional);
    NewParent->Modify();
    Widget->SetFlags(RF_Transactional);
    Widget->Modify();

    if (UWidget* HostWidget = FWidgetBlueprintEditorUtils::FindNamedSlotHostWidgetForContent(
            Widget, Blueprint->WidgetTree))
    {
        TScriptInterface<INamedSlotInterface> Host(HostWidget);
        if (Host)
        {
            HostWidget->SetFlags(RF_Transactional);
            HostWidget->Modify();
            FWidgetBlueprintEditorUtils::RemoveNamedSlotHostContent(Widget, Host);
        }
    }
    else if (Blueprint->ParentClass != nullptr
        && Blueprint->ParentClass != UUserWidget::StaticClass())
    {
        TArray<FName> SlotNames;
        Blueprint->WidgetTree->GetSlotNames(SlotNames);
        for (const FName SlotName : SlotNames)
        {
            if (Blueprint->WidgetTree->GetContentForSlot(SlotName) == Widget)
            {
                Blueprint->WidgetTree->SetContentForSlot(SlotName, nullptr);
                break;
            }
        }
    }

    TMap<FName, FString> SlotProperties;
    if (Widget->Slot != nullptr)
    {
        FWidgetBlueprintEditorUtils::ExportPropertiesToText(Widget->Slot, SlotProperties);
    }
    Widget->RemoveFromParent();
    UPanelSlot* NewSlot = ChildIndex >= 0
        ? NewParent->InsertChildAt(ChildIndex, Widget)
        : NewParent->AddChild(Widget);
    if (NewSlot == nullptr)
    {
        OutError = FText::FromString(TEXT("Unreal could not move the widget to the selected panel"));
        return false;
    }
    FWidgetBlueprintEditorUtils::ImportPropertiesFromText(NewSlot, SlotProperties);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FUnrealMCPWidgetCompatibility::RemoveWidget(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    FText& OutError)
{
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr || Widget == nullptr
        || (Blueprint->WidgetTree->FindWidget(Widget->GetFName()) == nullptr
            && Widget->GetOuter() != Blueprint->WidgetTree))
    {
        OutError = FText::FromString(TEXT("The widget was not found in the Widget Blueprint"));
        return false;
    }
    const FName WidgetName = Widget->GetFName();
    FWidgetBlueprintEditorUtils::DeleteWidgets(
        Blueprint,
        {Widget},
        FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
    if (Blueprint->WidgetTree->FindWidget(WidgetName) != nullptr)
    {
        OutError = FText::FromString(TEXT("Unreal did not remove the widget from the tree"));
        return false;
    }
    return true;
}

bool FUnrealMCPWidgetCompatibility::VerifyWidgetRename(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    const FText& NewName,
    FText& OutError)
{
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr || Widget == nullptr
        || NewName.IsEmptyOrWhitespace())
    {
        OutError = FText::FromString(TEXT("A valid widget and non-empty name are required"));
        return false;
    }
    const FString NameString = NewName.ToString();
    if (NameString.Len() >= NAME_SIZE
        || !FName::IsValidXName(NameString, INVALID_OBJECTNAME_CHARACTERS))
    {
        OutError = FText::FromString(TEXT("The widget name is invalid or too long"));
        return false;
    }
    const FName Name(*NameString);
    UWidget* Existing = Blueprint->WidgetTree->FindWidget(Name);
    if (Existing != nullptr && Existing != Widget)
    {
        OutError = FText::FromString(TEXT("Another widget already uses that name"));
        return false;
    }
    if (Existing == nullptr && !Widget->Rename(*NameString, nullptr, REN_Test))
    {
        OutError = FText::FromString(TEXT("Another object already uses that name"));
        return false;
    }
    if (Blueprint->ParentClass != nullptr)
    {
        FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(
            Blueprint->ParentClass->FindPropertyByName(Name));
        if (Property != nullptr && FWidgetBlueprintEditorUtils::IsBindWidgetProperty(Property))
        {
            if (!Widget->IsA(Property->PropertyClass))
            {
                OutError = FText::FromString(TEXT("The widget does not satisfy the parent BindWidget property type"));
                return false;
            }
            return true;
        }
    }
    FKismetNameValidator Validator(Blueprint, Widget->GetFName());
    const EValidatorResult Result = Validator.IsValid(Name);
    if (Result != EValidatorResult::Ok
        && !(Existing == Widget
            && (Result == EValidatorResult::AlreadyInUse || Result == EValidatorResult::ExistingName)))
    {
        OutError = INameValidatorInterface::GetErrorText(NameString, Result);
        return false;
    }
    return true;
}

bool FUnrealMCPWidgetCompatibility::RenameWidget(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    const FString& NewName)
{
    if (Blueprint == nullptr || Widget == nullptr)
    {
        return false;
    }
    FText Error;
    if (!VerifyWidgetRename(Blueprint, Widget, FText::FromString(NewName), Error))
    {
        return false;
    }
    const FName OldName = Widget->GetFName();
    const FName NewFName(*NewName);
    if (OldName == NewFName)
    {
        Widget->SetDisplayLabel(NewName);
        return true;
    }
    const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP rename widget")));
    Blueprint->Modify();
    Widget->Modify();
    Blueprint->OnVariableRenamed(OldName, NewFName);
    Widget->SetDisplayLabel(NewName);
    if (!Widget->Rename(*NewName))
    {
        return false;
    }
    FWidgetBlueprintEditorUtils::ReplaceDesiredFocus(Blueprint, OldName, NewFName);
    for (FDelegateEditorBinding& Binding : Blueprint->Bindings)
    {
        if (Binding.ObjectName == OldName.ToString())
        {
            Binding.ObjectName = NewName;
        }
    }
    for (UWidgetAnimation* Animation : Blueprint->Animations)
    {
        for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
        {
            if (Binding.WidgetName == OldName)
            {
                Binding.WidgetName = NewFName;
                Animation->MovieScene->Modify();
                if (Binding.SlotWidgetName == NAME_None)
                {
                    if (FMovieScenePossessable* Possessable =
                            Animation->MovieScene->FindPossessable(Binding.AnimationGuid))
                    {
                        Possessable->SetName(NewName);
                    }
                }
            }
        }
    }
    Blueprint->WidgetTree->ForEachWidget([OldName, NewFName](UWidget* Candidate)
    {
        if (Candidate->Navigation != nullptr)
        {
            Candidate->Navigation->SetFlags(RF_Transactional);
            Candidate->Navigation->Modify();
            Candidate->Navigation->TryToRenameBinding(OldName, NewFName);
        }
    });
    FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, NewFName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint, OldName, NewFName);
    return true;
}

void FUnrealMCPWidgetCompatibility::ToggleWidgetAsVariable(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget,
    bool bIsVariable,
    bool bMarkBlueprintModified)
{
    if (Blueprint == nullptr || Widget == nullptr || Widget->bIsVariable == bIsVariable)
    {
        return;
    }
    Blueprint->Modify();
    Widget->Modify();
    Widget->bIsVariable = bIsVariable;
    if (bMarkBlueprintModified)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
}

bool FUnrealMCPWidgetCompatibility::BindToEventProperty(
    UWidgetBlueprint* Blueprint,
    FName EventName,
    FName PropertyName,
    UClass* PropertyClass,
    bool bShouldJumpToNode,
    FText& OutError)
{
    if (Blueprint == nullptr || EventName.IsNone() || PropertyName.IsNone()
        || PropertyClass == nullptr || Blueprint->SkeletonGeneratedClass == nullptr)
    {
        OutError = FText::FromString(TEXT("A valid Widget Blueprint, event, widget property, and class are required"));
        return false;
    }
    FObjectProperty* VariableProperty = FindFProperty<FObjectProperty>(
        Blueprint->SkeletonGeneratedClass, PropertyName);
    if (VariableProperty == nullptr)
    {
        OutError = FText::FromString(TEXT("The exposed widget variable was not found on the skeleton class"));
        return false;
    }
    if (const UK2Node_ComponentBoundEvent* Existing =
            FKismetEditorUtilities::FindBoundEventForComponent(
                Blueprint, EventName, VariableProperty->GetFName()))
    {
        if (bShouldJumpToNode)
        {
            FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Existing);
        }
        OutError = FText::FromString(TEXT("The widget event is already bound"));
        return false;
    }
    if (FindFProperty<FMulticastDelegateProperty>(PropertyClass, EventName) == nullptr)
    {
        OutError = FText::FromString(TEXT("The requested multicast delegate does not exist on the widget class"));
        return false;
    }
    FKismetEditorUtilities::CreateNewBoundEventForClass(
        PropertyClass, EventName, Blueprint, VariableProperty);
    if (FKismetEditorUtilities::FindBoundEventForComponent(
            Blueprint, EventName, VariableProperty->GetFName()) == nullptr)
    {
        OutError = FText::FromString(TEXT("Unreal did not create the requested widget event node"));
        return false;
    }
    return true;
}

void FUnrealMCPWidgetCompatibility::RemoveTransientWidgetFromTree(
    UWidgetBlueprint* Blueprint,
    UWidget* Widget)
{
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr || Widget == nullptr)
    {
        return;
    }
    Blueprint->WidgetTree->SetFlags(RF_Transactional);
    Blueprint->WidgetTree->Modify();
    Blueprint->WidgetTree->RemoveWidget(Widget);
    if (Widget->GetOutermost() != GetTransientPackage())
    {
        Widget->ClearFlags(RF_Public | RF_Standalone);
        Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
    }
}
