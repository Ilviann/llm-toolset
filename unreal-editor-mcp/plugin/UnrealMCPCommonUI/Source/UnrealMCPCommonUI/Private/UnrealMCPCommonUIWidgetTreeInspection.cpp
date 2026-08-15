#include "UnrealMCPCommonUIWidgetTreeInspection.h"

#include "UnrealMCPCommonUIVersion.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UnrealMCP::CommonUIWidgetTreeInspection
{
namespace
{
struct FPropertySpec
{
    FString PropertyName;
    FString FieldName;
};

struct FFamilyDefinition
{
    FString ClassPath;
    FString Family;
    TArray<FPropertySpec> Properties;
};

struct FWidgetEntry
{
    UWidget* Widget = nullptr;
    UWidget* ParentTemplate = nullptr;
    FString WidgetId;
    FString Family;
    FString Ownership;
    TArray<FPropertySpec> Properties;
};

struct FModel
{
    UWidgetBlueprint* Blueprint = nullptr;
    TArray<UWidget*> AllWidgets;
    TArray<FWidgetEntry> CommonUIWidgets;
};

FString StableIdentity(const FString& Seed)
{
    return FMD5::HashAnsiString(*Seed).ToLower();
}

const TArray<FFamilyDefinition>& FamilyDefinitions()
{
    static const TArray<FFamilyDefinition> Definitions = {
        {TEXT("/Script/CommonUI.CommonDateTimeTextBlock"), TEXT("date_time_text"), {
            {TEXT("CustomTimespanFormat"), TEXT("custom_timespan_format")},
            {TEXT("bCustomTimespanLeadingZeros"), TEXT("custom_timespan_leading_zeros")}}},
        {TEXT("/Script/CommonUI.CommonNumericTextBlock"), TEXT("numeric_text"), {
            {TEXT("CurrentNumericValue"), TEXT("numeric_value")},
            {TEXT("NumericType"), TEXT("numeric_type")},
            {TEXT("FormattingSpecification"), TEXT("formatting")},
            {TEXT("EaseOutInterpolationExponent"), TEXT("ease_out_exponent")},
            {TEXT("InterpolationUpdateInterval"), TEXT("interpolation_update_interval")},
            {TEXT("PostInterpolationShrinkDuration"), TEXT("post_interpolation_shrink_duration")},
            {TEXT("PerformSizeInterpolation"), TEXT("perform_size_interpolation")}}},
        {TEXT("/Script/CommonUI.CommonTextBlock"), TEXT("text"), {
            {TEXT("MobileFontSizeMultiplier"), TEXT("mobile_font_size_multiplier")},
            {TEXT("bIsScrollingEnabled"), TEXT("scrolling_enabled")},
            {TEXT("bAutoCollapseWithEmptyText"), TEXT("auto_collapse_with_empty_text")},
            {TEXT("Style"), TEXT("style")},
            {TEXT("ScrollStyle"), TEXT("scroll_style")},
            {TEXT("ScrollOrientation"), TEXT("scroll_orientation")}}},
        {TEXT("/Script/CommonUI.CommonRichTextBlock"), TEXT("rich_text"), {
            {TEXT("InlineIconDisplayMode"), TEXT("inline_icon_display_mode")},
            {TEXT("bTintInlineIcon"), TEXT("tint_inline_icon")},
            {TEXT("MobileTextBlockScale"), TEXT("mobile_text_block_scale")},
            {TEXT("DefaultTextStyleOverrideClass"), TEXT("default_text_style")},
            {TEXT("ScrollStyle"), TEXT("scroll_style")},
            {TEXT("ScrollOrientation"), TEXT("scroll_orientation")},
            {TEXT("bIsScrollingEnabled"), TEXT("scrolling_enabled")},
            {TEXT("bAutoCollapseWithEmptyText"), TEXT("auto_collapse_with_empty_text")}}},
        {TEXT("/Script/CommonUI.CommonButtonBase"), TEXT("button"), {
            {TEXT("Style"), TEXT("style")},
            {TEXT("bHideInputAction"), TEXT("hide_input_action")},
            {TEXT("bLocked"), TEXT("locked")},
            {TEXT("bSelectable"), TEXT("selectable")},
            {TEXT("bShouldSelectUponReceivingFocus"), TEXT("select_on_focus")},
            {TEXT("bInteractableWhenSelected"), TEXT("interactable_when_selected")},
            {TEXT("bToggleable"), TEXT("toggleable")},
            {TEXT("bDisplayInputActionWhenNotInteractable"), TEXT("display_action_when_not_interactable")},
            {TEXT("bHideInputActionWithKeyboard"), TEXT("hide_action_with_keyboard")},
            {TEXT("bShouldUseFallbackDefaultInputAction"), TEXT("use_fallback_input_action")},
            {TEXT("bRequiresHold"), TEXT("requires_hold")},
            {TEXT("HoldData"), TEXT("hold_data")},
            {TEXT("ClickMethod"), TEXT("click_method")},
            {TEXT("TouchMethod"), TEXT("touch_method")},
            {TEXT("PressMethod"), TEXT("press_method")},
            {TEXT("InputPriority"), TEXT("input_priority")},
            {TEXT("TriggeringInputAction"), TEXT("triggering_input_action")},
            {TEXT("TriggeringEnhancedInputAction"), TEXT("triggering_enhanced_input_action")},
            {TEXT("bNavigateToNextWidgetOnDisable"), TEXT("navigate_on_disable")}}},
        {TEXT("/Script/CommonUI.CommonActivatableWidgetSwitcher"), TEXT("activatable_switcher"), {
            {TEXT("bClearFocusRestorationTargetOfDeactivatedWidgets"), TEXT("clear_deactivated_focus_restoration")}}},
        {TEXT("/Script/CommonUI.CommonAnimatedSwitcher"), TEXT("animated_switcher"), {
            {TEXT("TransitionType"), TEXT("transition_type")},
            {TEXT("TransitionCurveType"), TEXT("transition_curve")},
            {TEXT("TransitionDuration"), TEXT("transition_duration")},
            {TEXT("TransitionFallbackStrategy"), TEXT("transition_fallback")}}},
        {TEXT("/Script/CommonUI.CommonActivatableWidgetStack"), TEXT("activatable_stack"), {
            {TEXT("RootContentWidgetClass"), TEXT("root_content_widget_class")}}},
        {TEXT("/Script/CommonUI.CommonActivatableWidgetQueue"), TEXT("activatable_queue"), {}},
        {TEXT("/Script/CommonUI.CommonActivatableWidgetContainerBase"), TEXT("activatable_container"), {
            {TEXT("TransitionType"), TEXT("transition_type")},
            {TEXT("TransitionCurveType"), TEXT("transition_curve")},
            {TEXT("TransitionDuration"), TEXT("transition_duration")},
            {TEXT("bResetPoolWhenReleasingSlateResources"), TEXT("reset_pool_on_release")},
            {TEXT("TransitionFallbackStrategy"), TEXT("transition_fallback")}}},
        {TEXT("/Script/CommonUI.CommonActionWidget"), TEXT("action_display"), {
            {TEXT("InputActions"), TEXT("input_actions")},
            {TEXT("EnhancedInputAction"), TEXT("enhanced_input_action")},
            {TEXT("DesignTimeKey"), TEXT("design_time_key")},
            {TEXT("ProgressMaterialParam"), TEXT("progress_material_parameter")}}},
        {TEXT("/Script/CommonUI.CommonLazyImage"), TEXT("lazy_image"), {
            {TEXT("LoadingBackgroundBrush"), TEXT("loading_background_brush")},
            {TEXT("LoadingThrobberBrush"), TEXT("loading_throbber_brush")},
            {TEXT("MaterialTextureParamName"), TEXT("material_texture_parameter")}}},
        {TEXT("/Script/CommonUI.CommonLazyWidget"), TEXT("lazy_widget"), {
            {TEXT("WidgetClass"), TEXT("widget_class")},
            {TEXT("LoadingThrobberBrush"), TEXT("loading_throbber_brush")},
            {TEXT("LoadingBackgroundBrush"), TEXT("loading_background_brush")}}},
        {TEXT("/Script/CommonUI.CommonTabListWidgetBase"), TEXT("tab_list"), {
            {TEXT("NextTabInputActionData"), TEXT("next_tab_input_action")},
            {TEXT("PreviousTabInputActionData"), TEXT("previous_tab_input_action")},
            {TEXT("NextTabEnhancedInputAction"), TEXT("next_tab_enhanced_input_action")},
            {TEXT("PreviousTabEnhancedInputAction"), TEXT("previous_tab_enhanced_input_action")},
            {TEXT("bAutoListenForInput"), TEXT("auto_listen_for_input")},
            {TEXT("bShouldWrapNavigation"), TEXT("wrap_navigation")},
            {TEXT("bDeferRebuildingTabList"), TEXT("defer_rebuild")},
            {TEXT("LinkedSwitcher"), TEXT("linked_switcher")}}},
        {TEXT("/Script/CommonUI.CommonListView"), TEXT("list"), {
            {TEXT("EntryWidgetClass"), TEXT("entry_widget_class")},
            {TEXT("SelectionMode"), TEXT("selection_mode")}}},
        {TEXT("/Script/CommonUI.CommonTileView"), TEXT("tile_list"), {
            {TEXT("EntryWidgetClass"), TEXT("entry_widget_class")},
            {TEXT("SelectionMode"), TEXT("selection_mode")},
            {TEXT("EntryHeight"), TEXT("entry_height")},
            {TEXT("EntryWidth"), TEXT("entry_width")}}},
        {TEXT("/Script/CommonUI.CommonTreeView"), TEXT("tree_list"), {
            {TEXT("EntryWidgetClass"), TEXT("entry_widget_class")},
            {TEXT("SelectionMode"), TEXT("selection_mode")}}},
        {TEXT("/Script/CommonUI.CommonWidgetCarouselNavBar"), TEXT("carousel_navigation"), {
            {TEXT("ButtonWidgetType"), TEXT("button_widget_type")},
            {TEXT("ButtonPadding"), TEXT("button_padding")},
            {TEXT("LinkedCarousel"), TEXT("linked_carousel")}}},
        {TEXT("/Script/CommonUI.CommonWidgetCarousel"), TEXT("carousel"), {
            {TEXT("ActiveWidgetIndex"), TEXT("active_widget_index")},
            {TEXT("MoveSpeed"), TEXT("move_speed")},
            {TEXT("bCacheChildren"), TEXT("cache_children")}}},
        {TEXT("/Script/CommonUI.CommonActivatableWidget"), TEXT("activatable_widget"), {
            {TEXT("bIsBackHandler"), TEXT("is_back_handler")},
            {TEXT("bIsBackActionDisplayedInActionBar"), TEXT("display_back_action")},
            {TEXT("OverrideBackActionDisplayName"), TEXT("back_action_display_name")},
            {TEXT("bAutoActivate"), TEXT("auto_activate")},
            {TEXT("bSupportsActivationFocus"), TEXT("supports_activation_focus")},
            {TEXT("bIsModal"), TEXT("modal")},
            {TEXT("bAutoRestoreFocus"), TEXT("auto_restore_focus")},
            {TEXT("bOverrideActionDomain"), TEXT("override_action_domain")},
            {TEXT("InputMapping"), TEXT("input_mapping")},
            {TEXT("InputMappingPriority"), TEXT("input_mapping_priority")},
            {TEXT("ActionDomainOverride"), TEXT("action_domain_override")},
            {TEXT("bSetVisibilityOnActivated"), TEXT("set_visibility_on_activated")},
            {TEXT("ActivatedVisibility"), TEXT("activated_visibility")},
            {TEXT("bSetVisibilityOnDeactivated"), TEXT("set_visibility_on_deactivated")},
            {TEXT("DeactivatedVisibility"), TEXT("deactivated_visibility")}}},
        {TEXT("/Script/CommonUI.CommonUserWidget"), TEXT("user_widget"), {
            {TEXT("bDisplayInActionBar"), TEXT("display_in_action_bar")},
            {TEXT("bConsumePointerInput"), TEXT("consume_pointer_input")}}},
    };
    return Definitions;
}

UWidgetTree* EffectiveWidgetTree(UWidgetBlueprint* Blueprint)
{
    if (Blueprint == nullptr) return nullptr;
    if (Blueprint->WidgetTree != nullptr && Blueprint->WidgetTree->RootWidget != nullptr)
        return Blueprint->WidgetTree;
    UWidgetBlueprintGeneratedClass* GeneratedClass =
        Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
    UWidgetBlueprintGeneratedClass* Owner = GeneratedClass != nullptr
        ? GeneratedClass->FindWidgetTreeOwningClass() : nullptr;
    return Owner != nullptr ? Owner->GetWidgetTreeArchetype() : Blueprint->WidgetTree.Get();
}

UWidget* ParentTemplate(UWidgetBlueprint* Blueprint, const UWidget& Widget)
{
    UClass* GeneratedClass = Blueprint != nullptr ? Blueprint->GeneratedClass : nullptr;
    UWidgetBlueprintGeneratedClass* ParentClass = GeneratedClass != nullptr
        ? Cast<UWidgetBlueprintGeneratedClass>(GeneratedClass->GetSuperClass()) : nullptr;
    UWidgetTree* ParentTree = ParentClass != nullptr ? ParentClass->GetWidgetTreeArchetype() : nullptr;
    return ParentTree != nullptr ? ParentTree->FindWidget(Widget.GetFName()) : nullptr;
}

bool BuildModel(UObject* Asset, FModel& OutModel, FUnrealMCPError* OutError)
{
    OutModel = {};
    OutModel.Blueprint = Cast<UWidgetBlueprint>(Asset);
    UWidgetTree* Tree = EffectiveWidgetTree(OutModel.Blueprint);
    if (OutModel.Blueprint == nullptr || Tree == nullptr)
    {
        if (OutError != nullptr)
            *OutError = {TEXT("busy"), TEXT("The effective Widget Blueprint tree is unavailable"),
                MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    Tree->GetAllWidgets(OutModel.AllWidgets);
    if (OutModel.AllWidgets.Num() > UnrealMCPCommonUI::MaxWidgetTreeWidgets)
    {
        if (OutError != nullptr)
            *OutError = {TEXT("response_too_large"), TEXT("The Widget Blueprint exceeds the CommonUI widget-tree limit")};
        return false;
    }
    for (UWidget* Widget : OutModel.AllWidgets)
    {
        if (Widget == nullptr) continue;
        FWidgetEntry Entry;
        Entry.Widget = Widget;
        Entry.ParentTemplate = ParentTemplate(OutModel.Blueprint, *Widget);
        Entry.WidgetId = StableIdentity(
            OutModel.Blueprint->GetPathName() + TEXT("|") + Widget->GetName()
            + TEXT("|") + Widget->GetClass()->GetPathName());
        for (const FFamilyDefinition& Definition : FamilyDefinitions())
        {
            UClass* SupportedClass = FindObject<UClass>(nullptr, *Definition.ClassPath);
            if (SupportedClass == nullptr || !Widget->IsA(SupportedClass)) continue;
            if (Entry.Family.IsEmpty()) Entry.Family = Definition.Family;
            for (const FPropertySpec& Spec : Definition.Properties)
            {
                if (!Entry.Properties.ContainsByPredicate([&Spec](const FPropertySpec& Existing)
                    { return Existing.FieldName == Spec.FieldName; }))
                    Entry.Properties.Add(Spec);
            }
        }
        if (Entry.Family.IsEmpty()) continue;
        Entry.Ownership = Entry.ParentTemplate != nullptr ? TEXT("inherited") : TEXT("local");
        OutModel.CommonUIWidgets.Add(MoveTemp(Entry));
    }
    OutModel.CommonUIWidgets.Sort([](const FWidgetEntry& Left, const FWidgetEntry& Right)
    {
        const int32 NameOrder = Left.Widget->GetName().Compare(Right.Widget->GetName());
        return NameOrder == 0
            ? Left.Widget->GetClass()->GetPathName() < Right.Widget->GetClass()->GetPathName()
            : NameOrder < 0;
    });
    return true;
}

FString PropertySource(const FWidgetEntry& Entry, const FProperty* Property)
{
    return Property != nullptr && Entry.ParentTemplate != nullptr
        && Entry.ParentTemplate->IsA(Entry.Widget->GetClass())
        && Property->Identical_InContainer(Entry.Widget, Entry.ParentTemplate)
        ? TEXT("inherited") : TEXT("local");
}

TSharedRef<FUnrealMCPRecord> ReferenceRecord(
    const FString& Seed,
    const FString& ObjectPath,
    const UObject* Resolved,
    const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("type"), TEXT("reference"));
    Result->SetStringField(TEXT("reference_id"), StableIdentity(Seed + TEXT("|") + ObjectPath));
    Result->SetStringField(TEXT("object_path"), ObjectPath);
    Result->SetStringField(TEXT("class_path"),
        Resolved != nullptr ? Resolved->GetClass()->GetPathName() : FString());
    Result->SetBoolField(TEXT("resolved"), Resolved != nullptr);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FUnrealMCPRecord> DataTableRowRecord(
    const FString& Seed,
    const FDataTableRowHandle& Handle,
    const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("type"), TEXT("data_table_row"));
    Result->SetStringField(TEXT("row_name"), Handle.RowName.ToString());
    const FString Path = Handle.DataTable != nullptr ? Handle.DataTable->GetPathName() : FString();
    Result->SetObjectField(TEXT("data_table"),
        ReferenceRecord(Seed + TEXT("|table"), Path, Handle.DataTable, Source));
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

bool EncodeProperty(
    const FWidgetEntry& Entry,
    const FPropertySpec& Spec,
    TSharedRef<FUnrealMCPRecord>& Out,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    FProperty* Property = Entry.Widget->GetClass()->FindPropertyByName(FName(*Spec.PropertyName));
    if (Property == nullptr || Property->ArrayDim != 1)
    {
        OutError = {TEXT("extension_contract_violation"),
            TEXT("A frozen CommonUI widget property is unavailable")};
        return false;
    }
    const FString Source = PropertySource(Entry, Property);
    void* Value = Property->ContainerPtrToValuePtr<void>(Entry.Widget);
    Out = MakeShared<FUnrealMCPRecord>();
    Out->SetStringField(TEXT("source"), Source);
    if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        Out->SetStringField(TEXT("type"), TEXT("boolean"));
        Out->SetBoolField(TEXT("value"), BoolProperty->GetPropertyValue(Value));
    }
    else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        const int64 Numeric = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
        Out->SetStringField(TEXT("type"), TEXT("enum"));
        Out->SetStringField(TEXT("value"), EnumProperty->GetEnum()->GetNameStringByValue(Numeric));
    }
    else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property);
        ByteProperty != nullptr && ByteProperty->Enum != nullptr)
    {
        Out->SetStringField(TEXT("type"), TEXT("enum"));
        Out->SetStringField(TEXT("value"),
            ByteProperty->Enum->GetNameStringByValue(ByteProperty->GetPropertyValue(Value)));
    }
    else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        Out->SetStringField(TEXT("type"), TEXT("number"));
        Out->SetNumberField(TEXT("value"), NumericProperty->IsFloatingPoint()
            ? NumericProperty->GetFloatingPointPropertyValue(Value)
            : static_cast<double>(NumericProperty->GetSignedIntPropertyValue(Value)));
    }
    else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        Out->SetStringField(TEXT("type"), TEXT("name"));
        Out->SetStringField(TEXT("value"), NameProperty->GetPropertyValue(Value).ToString());
    }
    else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        Out->SetStringField(TEXT("type"), TEXT("text"));
        Out->SetStringField(TEXT("value"), TextProperty->GetPropertyValue(Value).ToString().Left(1024));
    }
    else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
    {
        Out->SetStringField(TEXT("type"), TEXT("string"));
        Out->SetStringField(TEXT("value"), StringProperty->GetPropertyValue(Value).Left(1024));
    }
    else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
    {
        const FSoftObjectPtr* Pointer = SoftObjectProperty->GetPropertyValuePtr(Value);
        const FString Path = Pointer != nullptr ? Pointer->ToSoftObjectPath().ToString() : FString();
        Out = ReferenceRecord(Spec.PropertyName, Path, Pointer != nullptr ? Pointer->Get() : nullptr, Source);
    }
    else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
        Out = ReferenceRecord(Spec.PropertyName,
            Object != nullptr ? Object->GetPathName() : FString(), Object, Source);
    }
    else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
        StructProperty != nullptr && StructProperty->Struct == TBaseStructure<FDataTableRowHandle>::Get())
    {
        Out = DataTableRowRecord(Spec.PropertyName,
            *static_cast<FDataTableRowHandle*>(Value), Source);
    }
    else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
        ArrayProperty != nullptr && CastField<FStructProperty>(ArrayProperty->Inner) != nullptr
        && CastFieldChecked<FStructProperty>(ArrayProperty->Inner)->Struct == TBaseStructure<FDataTableRowHandle>::Get())
    {
        FScriptArrayHelper Helper(ArrayProperty, Value);
        if (Helper.Num() > UnrealMCPCommonUI::MaxInputActionReferences)
        {
            OutError = {TEXT("response_too_large"),
                TEXT("A CommonUI input-action array exceeds the inspection limit")};
            return false;
        }
        TArray<TSharedPtr<FUnrealMCPValue>> Rows;
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
        {
            const FDataTableRowHandle* Handle =
                reinterpret_cast<const FDataTableRowHandle*>(Helper.GetRawPtr(Index));
            Rows.Add(MakeShared<FUnrealMCPValueObject>(
                DataTableRowRecord(Spec.PropertyName + LexToString(Index), *Handle, Source)));
        }
        Out->SetStringField(TEXT("type"), TEXT("data_table_rows"));
        Out->SetArrayField(TEXT("value"), MoveTemp(Rows));
    }
    else
    {
        FString Exported;
        Property->ExportTextItem_Direct(Exported, Value,
            Entry.ParentTemplate != nullptr
                ? Property->ContainerPtrToValuePtr<void>(Entry.ParentTemplate) : nullptr,
            Entry.Widget, PPF_None);
        const bool bTruncated = Exported.Len() > 1024;
        Out->SetStringField(TEXT("type"), TEXT("exported"));
        Out->SetStringField(TEXT("value"), Exported.Left(1024));
        Out->SetBoolField(TEXT("truncated"), bTruncated);
    }
    FString Exported;
    Property->ExportText_InContainer(0, Exported, Entry.Widget, Entry.ParentTemplate, Entry.Widget, PPF_None);
    OutFingerprint = Spec.PropertyName + TEXT("|") + Exported.Left(4096);
    return true;
}

const FWidgetEntry* FindEntry(const FModel& Model, const FString& Name)
{
    return Model.CommonUIWidgets.FindByPredicate([&Name](const FWidgetEntry& Entry)
        { return Entry.Widget->GetName() == Name; });
}

FString WidgetId(const FModel& Model, const UWidget* Widget)
{
    if (Widget == nullptr) return FString();
    if (const FWidgetEntry* Entry = FindEntry(Model, Widget->GetName())) return Entry->WidgetId;
    return StableIdentity(Model.Blueprint->GetPathName() + TEXT("|") + Widget->GetName()
        + TEXT("|") + Widget->GetClass()->GetPathName());
}

bool WidgetRecord(
    const FModel& Model,
    const FWidgetEntry& Entry,
    TSharedRef<FUnrealMCPRecord>& Out,
    TArray<FString>& OutFingerprint,
    FUnrealMCPError& OutError)
{
    Out = MakeShared<FUnrealMCPRecord>();
    Out->SetStringField(TEXT("widget_id"), Entry.WidgetId);
    Out->SetStringField(TEXT("name"), Entry.Widget->GetName());
    Out->SetStringField(TEXT("class"), Entry.Widget->GetClass()->GetPathName());
    Out->SetStringField(TEXT("family"), Entry.Family);
    Out->SetStringField(TEXT("ownership"), Entry.Ownership);
    Out->SetStringField(TEXT("selector"), FString(Section) + TEXT("/") + Entry.Widget->GetName());
    UPanelWidget* Parent = Entry.Widget->GetParent();
    if (Parent == nullptr)
    {
        Out->SetField(TEXT("parent_widget_id"), MakeShared<FUnrealMCPValueNull>());
        Out->SetField(TEXT("parent_name"), MakeShared<FUnrealMCPValueNull>());
    }
    else
    {
        Out->SetStringField(TEXT("parent_widget_id"), WidgetId(Model, Parent));
        Out->SetStringField(TEXT("parent_name"), Parent->GetName());
    }
    TArray<TSharedPtr<FUnrealMCPValue>> Children;
    if (const UPanelWidget* Panel = Cast<UPanelWidget>(Entry.Widget))
    {
        for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
            Children.Add(MakeShared<FUnrealMCPValueString>(WidgetId(Model, Panel->GetChildAt(Index))));
    }
    Out->SetArrayField(TEXT("children"), MoveTemp(Children));
    const TSharedRef<FUnrealMCPRecord> Properties = MakeShared<FUnrealMCPRecord>();
    for (const FPropertySpec& Spec : Entry.Properties)
    {
        TSharedRef<FUnrealMCPRecord> Encoded = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        if (!EncodeProperty(Entry, Spec, Encoded, Fingerprint, OutError)) return false;
        Properties->SetObjectField(Spec.FieldName, Encoded);
        OutFingerprint.Add(Entry.WidgetId + TEXT("|") + Fingerprint);
    }
    Out->SetObjectField(TEXT("properties"), Properties);
    return true;
}

TSharedRef<FUnrealMCPRecord> CollectionDescriptor(int32 Count)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("array"));
    Result->SetNumberField(TEXT("count"), Count);
    Result->SetStringField(TEXT("selector"), Section);
    return Result;
}

TSharedRef<FUnrealMCPRecord> PageRecord(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    int32 Total,
    int32 Returned)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("size"), Context.PageSize);
    Result->SetNumberField(TEXT("index"), Context.PageIndex);
    Result->SetNumberField(TEXT("count"), Total == 0 ? 0 : (Total + Context.PageSize - 1) / Context.PageSize);
    Result->SetNumberField(TEXT("returned"), Returned);
    Result->SetNumberField(TEXT("total_items"), Total);
    Result->SetBoolField(TEXT("has_previous"), Context.PageIndex > 0 && Total > 0);
    Result->SetBoolField(TEXT("has_next"),
        static_cast<int64>(Context.PageIndex + 1) * Context.PageSize < Total);
    Result->SetStringField(TEXT("snapshot_id"), Context.Identity.SnapshotId);
    return Result;
}
}

bool Inspect(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
    FUnrealMCPError& OutError)
{
    FModel Model;
    if (!BuildModel(Context.Asset, Model, &OutError)) return false;
    TArray<FString> Fingerprint;
    if (Context.Selector.IsRoot())
    {
        if (Context.bHasPaging)
        {
            OutError = {TEXT("invalid_argument"),
                TEXT("Paging parameters require the commonui_widgets collection selector")};
            return false;
        }
        if (!Document.Add({Section, TEXT("record"),
            MakeShared<FUnrealMCPValueObject>(CollectionDescriptor(Model.CommonUIWidgets.Num()))}, OutError))
            return false;
    }
    else if (Context.Selector.Segments.Num() == 1
        && Context.Selector.Segments[0] == Section)
    {
        const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
        const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Model.CommonUIWidgets.Num()));
        const int32 End = FMath::Min(Start + Context.PageSize, Model.CommonUIWidgets.Num());
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        for (int32 Index = Start; Index < End; ++Index)
        {
            TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
            if (!WidgetRecord(Model, Model.CommonUIWidgets[Index], Record, Fingerprint, OutError)) return false;
            Values.Add(MakeShared<FUnrealMCPValueObject>(Record));
        }
        if (!Document.Add({Section, TEXT("array"),
                MakeShared<FUnrealMCPValueArray>(MoveTemp(Values))}, OutError)
            || !Document.Add({TEXT("page"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(PageRecord(
                    Context, Model.CommonUIWidgets.Num(), End - Start))}, OutError))
            return false;
    }
    else if (Context.Selector.Segments.Num() == 2
        && Context.Selector.Segments[0] == Section)
    {
        if (Context.bHasPaging)
        {
            OutError = {TEXT("invalid_argument"),
                TEXT("Paging parameters apply only to the commonui_widgets collection")};
            return false;
        }
        const FWidgetEntry* Entry = FindEntry(Model, Context.Selector.Segments[1]);
        if (Entry == nullptr)
        {
            OutError = {TEXT("not_found"), TEXT("The selected CommonUI widget was not found")};
            return false;
        }
        TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        if (!WidgetRecord(Model, *Entry, Record, Fingerprint, OutError)
            || !Document.Add({TEXT("commonui_widget_detail"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Record)}, OutError))
            return false;
    }
    else
    {
        OutError = {TEXT("not_found"), TEXT("The selected CommonUI widget semantic child was not found")};
        return false;
    }
    const FString FullFingerprint = BuildFingerprint(Context.Asset);
    return !FullFingerprint.IsEmpty()
        && Snapshot.Add(TEXT("commonui_widgets"), FullFingerprint, OutError);
}

FString BuildFingerprint(UObject* Asset)
{
    FModel Model;
    if (!BuildModel(Asset, Model, nullptr)) return FString();
    TArray<FString> Lines;
    Lines.Add(TEXT("count|") + LexToString(Model.CommonUIWidgets.Num()));
    FUnrealMCPError Error;
    for (const FWidgetEntry& Entry : Model.CommonUIWidgets)
    {
        TSharedRef<FUnrealMCPRecord> Ignored = MakeShared<FUnrealMCPRecord>();
        if (!WidgetRecord(Model, Entry, Ignored, Lines, Error)) return FString();
    }
    Lines.Sort();
    return FString::Join(Lines, TEXT("\n"));
}

#if WITH_DEV_AUTOMATION_TESTS
bool ValidateFrozenAllowlist(int32& OutFamilyCount, FUnrealMCPError& OutError)
{
    OutFamilyCount = FamilyDefinitions().Num();
    TSet<FString> Families;
    for (const FFamilyDefinition& Definition : FamilyDefinitions())
    {
        UClass* Class = FindObject<UClass>(nullptr, *Definition.ClassPath);
        if (Class == nullptr || Definition.Family.IsEmpty() || Families.Contains(Definition.Family)
            || Definition.Properties.Num() > UnrealMCPCommonUI::MaxPropertiesPerWidget)
        {
            OutError = {TEXT("extension_contract_violation"),
                TEXT("The frozen CommonUI widget-family allowlist is invalid")};
            return false;
        }
        Families.Add(Definition.Family);
        TSet<FString> Fields;
        for (const FPropertySpec& Spec : Definition.Properties)
        {
            if (Class->FindPropertyByName(FName(*Spec.PropertyName)) == nullptr
                || Spec.FieldName.IsEmpty() || Fields.Contains(Spec.FieldName))
            {
                OutError = {TEXT("extension_contract_violation"),
                    TEXT("A frozen CommonUI widget-family property is unavailable or duplicated")};
                return false;
            }
            Fields.Add(Spec.FieldName);
        }
    }
    return true;
}
#endif
}
