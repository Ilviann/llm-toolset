#include "UnrealMCPWidgetStyleService.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPWidgetAuthoringSupport.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
void AddStyleProperties(
    TArray<FString>& Out,
    std::initializer_list<const TCHAR*> Names)
{
    for (const TCHAR* Name : Names)
    {
        Out.AddUnique(Name);
    }
}
}

FUnrealMCPWidgetStyleService::FUnrealMCPWidgetStyleService(
    FUnrealMCPBlueprintInspector& InInspector)
    : Inspector(InInspector)
{
}

TArray<FString> FUnrealMCPWidgetStyleService::SupportedProperties(
    const UWidget* Widget)
{
    TArray<FString> Result;
    if (Widget == nullptr)
    {
        return Result;
    }
    AddStyleProperties(Result, {
        TEXT("Visibility"), TEXT("bIsEnabled"), TEXT("RenderOpacity"),
        TEXT("ToolTipText"), TEXT("Cursor"), TEXT("Clipping"),
        TEXT("RenderTransform"), TEXT("RenderTransformPivot")});
    if (Widget->IsA<UTextBlock>())
    {
        AddStyleProperties(Result, {
            TEXT("Text"), TEXT("ColorAndOpacity"), TEXT("Font"),
            TEXT("StrikeBrush"), TEXT("ShadowOffset"),
            TEXT("ShadowColorAndOpacity"), TEXT("MinDesiredWidth"),
            TEXT("Justification"), TEXT("AutoWrapText"), TEXT("WrapTextAt"),
            TEXT("WrappingPolicy"), TEXT("TextTransformPolicy"),
            TEXT("TextOverflowPolicy")});
    }
    if (Widget->IsA<UImage>())
    {
        AddStyleProperties(Result, {TEXT("Brush"), TEXT("ColorAndOpacity")});
    }
    if (Widget->IsA<UButton>())
    {
        AddStyleProperties(Result, {
            TEXT("WidgetStyle"), TEXT("ColorAndOpacity"),
            TEXT("BackgroundColor"), TEXT("ClickMethod"), TEXT("TouchMethod"),
            TEXT("PressMethod"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<UProgressBar>())
    {
        AddStyleProperties(Result, {
            TEXT("WidgetStyle"), TEXT("Percent"), TEXT("BarFillType"),
            TEXT("BarFillStyle"), TEXT("bIsMarquee"), TEXT("BorderPadding"),
            TEXT("FillColorAndOpacity")});
    }
    if (Widget->IsA<UBorder>())
    {
        AddStyleProperties(Result, {
            TEXT("Background"), TEXT("BrushColor"),
            TEXT("ContentColorAndOpacity"), TEXT("Padding"),
            TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"),
            TEXT("bShowEffectWhenDisabled"), TEXT("DesiredSizeScale")});
    }
    if (Widget->IsA<UCheckBox>())
    {
        AddStyleProperties(Result, {
            TEXT("CheckedState"), TEXT("WidgetStyle"),
            TEXT("HorizontalAlignment"), TEXT("ClickMethod"),
            TEXT("TouchMethod"), TEXT("PressMethod"),
            TEXT("IsFocusable")});
    }
    if (Widget->IsA<USlider>())
    {
        AddStyleProperties(Result, {
            TEXT("Value"), TEXT("MinValue"), TEXT("MaxValue"),
            TEXT("StepSize"), TEXT("WidgetStyle"), TEXT("Orientation"),
            TEXT("SliderBarColor"), TEXT("SliderHandleColor"),
            TEXT("IndentHandle"), TEXT("Locked"), TEXT("MouseUsesStep"),
            TEXT("RequiresControllerLock"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<USpinBox>())
    {
        AddStyleProperties(Result, {
            TEXT("Value"), TEXT("MinValue"), TEXT("MaxValue"),
            TEXT("MinSliderValue"), TEXT("MaxSliderValue"),
            TEXT("Delta"), TEXT("SliderExponent"), TEXT("MinDesiredWidth"),
            TEXT("ClearKeyboardFocusOnCommit"),
            TEXT("SelectAllTextOnCommit"), TEXT("ForegroundColor")});
    }
    if (Widget->IsA<UComboBoxString>())
    {
        AddStyleProperties(Result, {
            TEXT("DefaultOptions"), TEXT("SelectedOption"),
            TEXT("WidgetStyle"), TEXT("ItemStyle"),
            TEXT("ForegroundColor"), TEXT("ContentPadding"),
            TEXT("MaxListHeight"), TEXT("HasDownArrow"),
            TEXT("EnableGamepadNavigationMode"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<UEditableText>()
        || Widget->IsA<UEditableTextBox>()
        || Widget->IsA<UMultiLineEditableText>()
        || Widget->IsA<UMultiLineEditableTextBox>())
    {
        AddStyleProperties(Result, {
            TEXT("Text"), TEXT("HintText"), TEXT("WidgetStyle"),
            TEXT("IsReadOnly"), TEXT("IsPassword"),
            TEXT("MinimumDesiredWidth"), TEXT("AllowContextMenu"),
            TEXT("Justification"), TEXT("OverflowPolicy")});
    }
    Result.RemoveAll([Widget](const FString& Name)
    {
        FProperty* Property =
            Widget->GetClass()->FindPropertyByName(FName(*Name));
        return Property == nullptr || Property->ArrayDim != 1
            || !Property->HasAnyPropertyFlags(CPF_Edit)
            || Property->HasAnyPropertyFlags(
                CPF_Transient | CPF_Deprecated | CPF_DisableEditOnTemplate
                | CPF_InstancedReference | CPF_ContainsInstancedReference)
            || Property->IsA<FDelegateProperty>()
            || Property->IsA<FMulticastDelegateProperty>()
            || Property->IsA<FInterfaceProperty>()
            || Property->IsA<FSetProperty>()
            || Property->IsA<FMapProperty>();
    });
    Result.Sort();
    return Result;
}

bool FUnrealMCPWidgetStyleService::Execute(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    using namespace UnrealMCP::WidgetAuthoring;
    using UnrealMCP::WidgetTreePrivate::WidgetId;

    check(IsInGameThread());
    if (!Arguments.IsValid()
        || !HasOnlyAuthoringFields(*Arguments, {
            TEXT("operation_id"), TEXT("asset_path"),
            TEXT("expected_snapshot"), TEXT("operation"), TEXT("widget_id"),
            TEXT("property_name"), TEXT("value")}))
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("set_style accepts only one supported presentation property")};
        return false;
    }
    FString Operation;
    FString PropertyName;
    const TSharedPtr<FUnrealMCPValue>* Value = Arguments->Values.Find(TEXT("value"));
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation)
        || Operation != TEXT("set_style")
        || !Arguments->TryGetStringField(TEXT("property_name"), PropertyName)
        || PropertyName.IsEmpty() || PropertyName.Len() > 128
        || Value == nullptr || !Value->IsValid())
    {
        OutError = {
            TEXT("invalid_argument"),
            TEXT("set_style requires one bounded property_name and value")};
        return false;
    }

    UWidgetBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    UWidget* Widget = nullptr;
    if (!ResolveBlueprint(
            Inspector, *Arguments, Blueprint, ObjectPath, OutError)
        || !ResolveWidget(Blueprint, *Arguments, Widget, OutError))
    {
        return false;
    }
    if (!SupportedProperties(Widget).Contains(PropertyName))
    {
        OutError = {
            TEXT("unsupported_style"),
            TEXT("The property is outside the exact style allowlist for this widget class")};
        return false;
    }
    FProperty* Property =
        Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
    TSharedPtr<FUnrealMCPRecord> Changed;
    if (!ApplyProperty(
            Blueprint, Widget, Property, *Value,
            TEXT("Unreal MCP set widget presentation"), Changed, OutError))
    {
        return false;
    }

    const FString Id = WidgetId(Blueprint, Widget);
    FString Snapshot;
    if (Id.IsEmpty()
        || !ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        if (OutError.Code.IsEmpty())
        {
            OutError = {
                TEXT("internal_error"),
                TEXT("The styled widget lost its stable identity")};
        }
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildResult(
        Blueprint, ObjectPath, Operation, Snapshot, Id, Changed);
    OutResult->SetStringField(TEXT("changed_style_property"), PropertyName);
    return true;
}
