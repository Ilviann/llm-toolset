#include "UnrealMCPWidgetInspectionSupport.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_ComponentBoundEvent.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
void AddProperties(TArray<FString>& Out, std::initializer_list<const TCHAR*> Names)
{
    for (const TCHAR* Name : Names)
    {
        Out.AddUnique(Name);
    }
}

bool HasConnections(const UEdGraphNode* Node)
{
    if (Node == nullptr)
    {
        return false;
    }
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && !Pin->LinkedTo.IsEmpty())
        {
            return true;
        }
    }
    return false;
}
}

namespace UnrealMCP::WidgetInspection
{
TArray<FString> SupportedStyleProperties(const UWidget* Widget)
{
    TArray<FString> Result;
    if (Widget == nullptr)
    {
        return Result;
    }
    AddProperties(Result, {
        TEXT("Visibility"), TEXT("bIsEnabled"), TEXT("RenderOpacity"),
        TEXT("ToolTipText"), TEXT("Cursor"), TEXT("Clipping"),
        TEXT("RenderTransform"), TEXT("RenderTransformPivot")});
    if (Widget->IsA<UTextBlock>())
    {
        AddProperties(Result, {
            TEXT("Text"), TEXT("ColorAndOpacity"), TEXT("Font"),
            TEXT("StrikeBrush"), TEXT("ShadowOffset"),
            TEXT("ShadowColorAndOpacity"), TEXT("MinDesiredWidth"),
            TEXT("Justification"), TEXT("AutoWrapText"), TEXT("WrapTextAt"),
            TEXT("WrappingPolicy"), TEXT("TextTransformPolicy"),
            TEXT("TextOverflowPolicy")});
    }
    if (Widget->IsA<UImage>())
    {
        AddProperties(Result, {TEXT("Brush"), TEXT("ColorAndOpacity")});
    }
    if (Widget->IsA<UButton>())
    {
        AddProperties(Result, {
            TEXT("WidgetStyle"), TEXT("ColorAndOpacity"),
            TEXT("BackgroundColor"), TEXT("ClickMethod"), TEXT("TouchMethod"),
            TEXT("PressMethod"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<UProgressBar>())
    {
        AddProperties(Result, {
            TEXT("WidgetStyle"), TEXT("Percent"), TEXT("BarFillType"),
            TEXT("BarFillStyle"), TEXT("bIsMarquee"), TEXT("BorderPadding"),
            TEXT("FillColorAndOpacity")});
    }
    if (Widget->IsA<UBorder>())
    {
        AddProperties(Result, {
            TEXT("Background"), TEXT("BrushColor"),
            TEXT("ContentColorAndOpacity"), TEXT("Padding"),
            TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"),
            TEXT("bShowEffectWhenDisabled"), TEXT("DesiredSizeScale")});
    }
    if (Widget->IsA<UCheckBox>())
    {
        AddProperties(Result, {
            TEXT("CheckedState"), TEXT("WidgetStyle"),
            TEXT("HorizontalAlignment"), TEXT("ClickMethod"),
            TEXT("TouchMethod"), TEXT("PressMethod"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<USlider>())
    {
        AddProperties(Result, {
            TEXT("Value"), TEXT("MinValue"), TEXT("MaxValue"),
            TEXT("StepSize"), TEXT("WidgetStyle"), TEXT("Orientation"),
            TEXT("SliderBarColor"), TEXT("SliderHandleColor"),
            TEXT("IndentHandle"), TEXT("Locked"), TEXT("MouseUsesStep"),
            TEXT("RequiresControllerLock"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<USpinBox>())
    {
        AddProperties(Result, {
            TEXT("Value"), TEXT("MinValue"), TEXT("MaxValue"),
            TEXT("MinSliderValue"), TEXT("MaxSliderValue"),
            TEXT("Delta"), TEXT("SliderExponent"), TEXT("MinDesiredWidth"),
            TEXT("ClearKeyboardFocusOnCommit"),
            TEXT("SelectAllTextOnCommit"), TEXT("ForegroundColor")});
    }
    if (Widget->IsA<UComboBoxString>())
    {
        AddProperties(Result, {
            TEXT("DefaultOptions"), TEXT("SelectedOption"),
            TEXT("WidgetStyle"), TEXT("ItemStyle"),
            TEXT("ForegroundColor"), TEXT("ContentPadding"),
            TEXT("MaxListHeight"), TEXT("HasDownArrow"),
            TEXT("EnableGamepadNavigationMode"), TEXT("IsFocusable")});
    }
    if (Widget->IsA<UEditableText>() || Widget->IsA<UEditableTextBox>()
        || Widget->IsA<UMultiLineEditableText>()
        || Widget->IsA<UMultiLineEditableTextBox>())
    {
        AddProperties(Result, {
            TEXT("Text"), TEXT("HintText"), TEXT("WidgetStyle"),
            TEXT("IsReadOnly"), TEXT("IsPassword"),
            TEXT("MinimumDesiredWidth"), TEXT("AllowContextMenu"),
            TEXT("Justification"), TEXT("OverflowPolicy")});
    }
    Result.RemoveAll([Widget](const FString& Name)
    {
        FProperty* Property = Widget->GetClass()->FindPropertyByName(FName(*Name));
        return Property == nullptr || Property->ArrayDim != 1
            || !Property->HasAnyPropertyFlags(CPF_Edit)
            || Property->HasAnyPropertyFlags(
                CPF_Transient | CPF_Deprecated | CPF_DisableEditOnTemplate
                | CPF_InstancedReference | CPF_ContainsInstancedReference)
            || Property->IsA<FDelegateProperty>()
            || Property->IsA<FMulticastDelegateProperty>()
            || Property->IsA<FInterfaceProperty>()
            || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>();
    });
    Result.Sort();
    return Result;
}

TArray<FString> SupportedLayoutProperties(const UPanelSlot* Slot)
{
    TArray<FString> Result;
    if (Slot == nullptr)
    {
        return Result;
    }
    if (Slot->IsA<UCanvasPanelSlot>())
    {
        AddProperties(Result, {TEXT("LayoutData"), TEXT("bAutoSize"), TEXT("ZOrder")});
    }
    else if (Slot->IsA<UGridSlot>())
    {
        AddProperties(Result, {
            TEXT("Padding"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"),
            TEXT("Row"), TEXT("RowSpan"), TEXT("Column"), TEXT("ColumnSpan"),
            TEXT("Layer"), TEXT("Nudge")});
    }
    else if (Slot->IsA<UUniformGridSlot>())
    {
        AddProperties(Result, {
            TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"),
            TEXT("Row"), TEXT("Column")});
    }
    else if (Slot->IsA<UHorizontalBoxSlot>() || Slot->IsA<UVerticalBoxSlot>()
        || Slot->IsA<UScrollBoxSlot>())
    {
        AddProperties(Result, {
            TEXT("Size"), TEXT("Padding"), TEXT("HorizontalAlignment"),
            TEXT("VerticalAlignment")});
    }
    else if (Slot->IsA<UOverlaySlot>() || Slot->IsA<USizeBoxSlot>())
    {
        AddProperties(Result, {
            TEXT("Padding"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment")});
    }
    else if (Slot->IsA<UWrapBoxSlot>())
    {
        AddProperties(Result, {
            TEXT("Padding"), TEXT("HorizontalAlignment"), TEXT("VerticalAlignment"),
            TEXT("bFillEmptySpace"), TEXT("FillSpanWhenLessThan"),
            TEXT("bForceNewLine")});
    }
    Result.RemoveAll([Slot](const FString& Name)
    {
        return Slot->GetClass()->FindPropertyByName(FName(*Name)) == nullptr;
    });
    Result.Sort();
    return Result;
}

TSharedRef<FUnrealMCPRecord> EncodeLayout(const UPanelSlot* Slot)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    if (Slot == nullptr)
    {
        return Result;
    }
    for (const FString& Name : SupportedLayoutProperties(Slot))
    {
        FProperty* Property = Slot->GetClass()->FindPropertyByName(FName(*Name));
        TSharedPtr<FUnrealMCPValue> Value;
        FUnrealMCPError Error;
        if (Property != nullptr && GameDataValueCodec::Encode(
                Property, Property->ContainerPtrToValuePtr<void>(Slot), 0, Value, Error)
            && Value.IsValid())
        {
            Result->SetField(Name, Value);
        }
    }
    return Result;
}

FString FingerprintLayout(const UPanelSlot* Slot)
{
    if (Slot == nullptr)
    {
        return FString();
    }
    TArray<FString> Parts;
    for (const FString& Name : SupportedLayoutProperties(Slot))
    {
        FProperty* Property = Slot->GetClass()->FindPropertyByName(FName(*Name));
        FString Value;
        if (Property != nullptr)
        {
            PropertyCodec::ExportValueText(Slot, Property, Value);
        }
        Parts.Add(Name + TEXT("=") + Value);
    }
    return FString::Join(Parts, TEXT(";"));
}

bool CollectBindings(UWidgetBlueprint* Blueprint, TArray<FBindingRecord>& OutRecords,
    FUnrealMCPError& OutError)
{
    using namespace WidgetTreePrivate;
    OutRecords.Reset();
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return true;
    }
    for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
    {
        UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(*Binding.ObjectName));
        const FString StableWidgetId = WidgetId(Blueprint, Widget);
        if (Widget == nullptr || StableWidgetId.IsEmpty())
        {
            continue;
        }
        const FString SourceKind = Binding.Kind == EBindingKind::Function
            ? TEXT("function") : TEXT("property");
        const FString SourceName = Binding.Kind == EBindingKind::Function
            ? Binding.FunctionName.ToString() : Binding.SourceProperty.ToString();
        FDelegateProperty* Target = FindFProperty<FDelegateProperty>(
            Widget->GetClass(), FName(*(Binding.PropertyName.ToString() + TEXT("Delegate"))));
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("section"), TEXT("widget_bindings"));
        Record->SetStringField(TEXT("record_type"), TEXT("property_binding"));
        Record->SetStringField(TEXT("id"), DeterministicId(
            TEXT("property_binding|") + StableWidgetId + TEXT("|")
            + Binding.PropertyName.ToString()));
        Record->SetStringField(TEXT("widget_id"), StableWidgetId);
        Record->SetStringField(TEXT("target_property"), Binding.PropertyName.ToString());
        Record->SetStringField(TEXT("source_kind"), SourceKind);
        Record->SetStringField(TEXT("source_name"), SourceName);
        Record->SetStringField(TEXT("cost"), TEXT("polling"));
        Record->SetBoolField(TEXT("target_valid"), Target != nullptr);
        const FString Fingerprint = TEXT("property|") + StableWidgetId + TEXT("|")
            + Binding.PropertyName.ToString() + TEXT("|") + SourceKind + TEXT("|")
            + SourceName + TEXT("|") + GuidString(Binding.MemberGuid);
        OutRecords.Add({StableWidgetId, Fingerprint, Record});
    }

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_ComponentBoundEvent* Event = Cast<UK2Node_ComponentBoundEvent>(Node);
            if (Event == nullptr)
            {
                continue;
            }
            UWidget* Widget = Blueprint->WidgetTree->FindWidget(Event->ComponentPropertyName);
            const FString StableWidgetId = WidgetId(Blueprint, Widget);
            if (Widget == nullptr || StableWidgetId.IsEmpty())
            {
                continue;
            }
            const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
            Record->SetStringField(TEXT("section"), TEXT("widget_bindings"));
            Record->SetStringField(TEXT("record_type"), TEXT("event_binding"));
            Record->SetStringField(TEXT("id"), GuidString(Event->NodeGuid));
            Record->SetStringField(TEXT("widget_id"), StableWidgetId);
            Record->SetStringField(TEXT("delegate_name"), Event->DelegatePropertyName.ToString());
            Record->SetStringField(TEXT("cost"), TEXT("event_driven"));
            Record->SetStringField(TEXT("graph_id"), GuidString(Graph->GraphGuid));
            Record->SetStringField(TEXT("node_id"), GuidString(Event->NodeGuid));
            Record->SetBoolField(TEXT("connected"), HasConnections(Event));
            const FString Fingerprint = TEXT("event|") + StableWidgetId + TEXT("|")
                + Event->DelegatePropertyName.ToString() + TEXT("|")
                + GuidString(Graph->GraphGuid) + TEXT("|") + GuidString(Event->NodeGuid);
            OutRecords.Add({StableWidgetId, Fingerprint, Record});
        }
    }
    if (OutRecords.Num() > MaxWidgetBindings)
    {
        OutError = {TEXT("response_too_large"),
            TEXT("The Widget Blueprint exceeds the supported binding limit")};
        return false;
    }
    OutRecords.Sort([](const FBindingRecord& Left, const FBindingRecord& Right)
    {
        return Left.Fingerprint < Right.Fingerprint;
    });
    return true;
}
}
