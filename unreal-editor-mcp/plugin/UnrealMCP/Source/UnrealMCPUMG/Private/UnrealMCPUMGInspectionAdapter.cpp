#include "UnrealMCPUMGInspectionAdapter.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetNavigation.h"
#include "Components/PanelSlot.h"
#include "Components/Widget.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPStructuredDataInspection.h"
#include "UnrealMCPUMGInspectionModel.h"
#include "UnrealMCPVersion.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UnrealMCP::UMGInspection::Private
{
namespace
{
FString ValueTypeName(const TSharedPtr<FUnrealMCPValue>& Value)
{
    if (!Value.IsValid()) return TEXT("null");
    switch (Value->Type)
    {
    case EUnrealMCPValueType::Null: return TEXT("null");
    case EUnrealMCPValueType::Boolean: return TEXT("boolean");
    case EUnrealMCPValueType::Number: return TEXT("number");
    case EUnrealMCPValueType::String: return TEXT("string");
    case EUnrealMCPValueType::Array: return TEXT("array");
    case EUnrealMCPValueType::Record: return TEXT("record");
    }
    return TEXT("unknown");
}

bool AddResult(
    const TSharedRef<FUnrealMCPRecord>& Result,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPError& OutError)
{
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Result->Values)
        if (!Document.Add({Field.Key, ValueTypeName(Field.Value), Field.Value}, OutError)) return false;
    return true;
}

TArray<FUnrealMCPAssetFamilySelectorRoute> Routes()
{
    return {
        {TEXT("umg_widget_tree"), {TEXT("widget_tree")}, true, false},
        {TEXT("umg_widgets"), {TEXT("widgets")}, true, false},
        {TEXT("umg_named_slots"), {TEXT("named_slots")}, true, false},
        {TEXT("umg_bindings"), {TEXT("bindings")}, true, false},
        {TEXT("umg_properties"), {TEXT("properties")}, true, false}};
}

bool RegisterRoutes(FUnrealMCPAssetFamilySelectorRouter& Selectors, FUnrealMCPError& OutError)
{
    for (const FUnrealMCPAssetFamilySelectorRoute& Route : Routes())
        if (!Selectors.Register(Route, OutError)) return false;
    return Selectors.Freeze(OutError);
}

FString CanonicalSelector(const TArray<FString>& Segments)
{
    TArray<FString> Encoded;
    for (const FString& Segment : Segments)
        Encoded.Add(StructuredDataInspection::EncodeSelectorSegment(Segment));
    return FString::Join(Encoded, TEXT("/"));
}

void AddSelection(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    const TSharedRef<FUnrealMCPRecord>& Result)
{
    const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
    Selection->SetStringField(TEXT("selector"), CanonicalSelector(Context.Selector.Segments));
    Result->SetObjectField(TEXT("selection"), Selection);
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

void PageBounds(const FUnrealMCPAssetFamilyInspectionContext& Context, int32 Total, int32& OutStart, int32& OutEnd)
{
    const int64 Start64 = static_cast<int64>(Context.PageIndex) * Context.PageSize;
    OutStart = static_cast<int32>(FMath::Min<int64>(Start64, Total));
    OutEnd = FMath::Min(OutStart + Context.PageSize, Total);
}

int32 AvailablePropertyCount(UObject* Object, const TArray<FString>& Names)
{
    if (Object == nullptr) return 0;
    int32 Result = 0;
    for (const FString& Name : Names)
    {
        FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Name));
        if (Property != nullptr && Property->ArrayDim == 1
            && Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)
            && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) ++Result;
    }
    return Result;
}

void AddSemanticProperty(
    UObject* Object,
    const TCHAR* PropertyName,
    const TCHAR* OutputName,
    const TSharedRef<FUnrealMCPRecord>& Target)
{
    FProperty* Property = Object != nullptr
        ? Object->GetClass()->FindPropertyByName(PropertyName) : nullptr;
    if (Property == nullptr || Property->ArrayDim != 1
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) return;
    TSharedPtr<FUnrealMCPValue> Value;
    FUnrealMCPError Error;
    if (GameDataValueCodec::Encode(
        Property, Property->ContainerPtrToValuePtr<void>(Object), 0, Value, Error) && Value.IsValid())
    {
        Target->SetField(OutputName, Value);
    }
}

TSharedRef<FUnrealMCPRecord> NavigationRecord(const UWidget* Widget)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    const FObjectPropertyBase* Property = Widget != nullptr
        ? CastField<FObjectPropertyBase>(Widget->GetClass()->FindPropertyByName(TEXT("Navigation"))) : nullptr;
    const UWidgetNavigation* Navigation = Property != nullptr
        ? Cast<UWidgetNavigation>(Property->GetObjectPropertyValue_InContainer(Widget)) : nullptr;
    auto AddDirection = [&Result](const TCHAR* Name, const FWidgetNavigationData* Data)
    {
        const TSharedRef<FUnrealMCPRecord> Direction = MakeShared<FUnrealMCPRecord>();
        const EUINavigationRule Rule = Data != nullptr ? Data->Rule : EUINavigationRule::Escape;
        const TCHAR* RuleName = TEXT("escape");
        switch (Rule)
        {
        case EUINavigationRule::Explicit: RuleName = TEXT("explicit"); break;
        case EUINavigationRule::Custom: RuleName = TEXT("custom"); break;
        case EUINavigationRule::CustomBoundary: RuleName = TEXT("custom_boundary"); break;
        case EUINavigationRule::Stop: RuleName = TEXT("stop"); break;
        case EUINavigationRule::Wrap: RuleName = TEXT("wrap"); break;
        default: break;
        }
        Direction->SetStringField(TEXT("rule"), RuleName);
        if (Data != nullptr && !Data->WidgetToFocus.IsNone())
            Direction->SetStringField(TEXT("target"), Data->WidgetToFocus.ToString());
        Result->SetObjectField(Name, Direction);
    };
    AddDirection(TEXT("up"), Navigation != nullptr ? &Navigation->Up : nullptr);
    AddDirection(TEXT("down"), Navigation != nullptr ? &Navigation->Down : nullptr);
    AddDirection(TEXT("left"), Navigation != nullptr ? &Navigation->Left : nullptr);
    AddDirection(TEXT("right"), Navigation != nullptr ? &Navigation->Right : nullptr);
    AddDirection(TEXT("next"), Navigation != nullptr ? &Navigation->Next : nullptr);
    AddDirection(TEXT("previous"), Navigation != nullptr ? &Navigation->Previous : nullptr);
    return Result;
}

TSharedRef<FUnrealMCPRecord> CollectionDescriptor(const FString& Selector, int32 Count)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("array"));
    Result->SetNumberField(TEXT("count"), Count);
    Result->SetStringField(TEXT("selector"), Selector);
    return Result;
}

TSharedRef<FUnrealMCPRecord> WidgetIndexRecord(const FWidgetEntry& Entry, const FModel& Model)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("name"), Entry.Name);
    Result->SetStringField(TEXT("class"), Entry.ClassPath);
    Result->SetStringField(TEXT("ownership"), Entry.Ownership);
    Result->SetStringField(TEXT("declared_by"), Entry.DeclaredBy);
    Result->SetBoolField(TEXT("root"), Entry.bRoot);
    Result->SetBoolField(TEXT("variable"), Entry.bVariable);
    if (Entry.Parent.IsEmpty()) Result->SetField(TEXT("parent"), MakeShared<FUnrealMCPValueNull>());
    else Result->SetStringField(TEXT("parent"), Entry.Parent);
    if (Entry.ChildIndex == INDEX_NONE) Result->SetField(TEXT("child_index"), MakeShared<FUnrealMCPValueNull>());
    else Result->SetNumberField(TEXT("child_index"), Entry.ChildIndex);
    Result->SetNumberField(TEXT("child_count"), Model.ChildrenOf(Entry.Name).Num());
    Result->SetStringField(TEXT("selector"), TEXT("widgets/")
        + StructuredDataInspection::EncodeSelectorSegment(Entry.Name));
    return Result;
}

TSharedRef<FUnrealMCPRecord> BindingRecord(const FBindingEntry& Entry)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), Entry.Kind == TEXT("event_binding")
        ? TEXT("designer_event") : TEXT("legacy_property"));
    Result->SetStringField(TEXT("target_widget"), Entry.Widget);
    Result->SetStringField(Entry.Kind == TEXT("event_binding")
        ? TEXT("target_delegate") : TEXT("target_property"), Entry.Target);
    Result->SetStringField(TEXT("declared_by"), Entry.DeclaredBy);
    Result->SetStringField(TEXT("cost"), Entry.Cost);
    if (Entry.Kind == TEXT("property_binding"))
    {
        Result->SetStringField(TEXT("source_kind"), Entry.SourceKind);
        Result->SetStringField(TEXT("source_name"), Entry.SourceName);
        if (!Entry.SourceType.IsEmpty()) Result->SetStringField(TEXT("source_type"), Entry.SourceType);
        if (!Entry.TargetType.IsEmpty()) Result->SetStringField(TEXT("target_type"), Entry.TargetType);
    }
    else
    {
        Result->SetStringField(TEXT("graph"), Entry.Graph);
        Result->SetStringField(TEXT("event"), Entry.Event);
        if (!Entry.Signature.IsEmpty()) Result->SetStringField(TEXT("signature"), Entry.Signature);
        if (!Entry.Graph.IsEmpty() && !Entry.Event.IsEmpty())
            Result->SetStringField(TEXT("event_selector"), TEXT("events/")
                + StructuredDataInspection::EncodeSelectorSegment(Entry.Graph) + TEXT("/")
                + StructuredDataInspection::EncodeSelectorSegment(Entry.Event));
    }
    return Result;
}

bool BuildPropertySelection(
    UObject* Object,
    const TArray<FString>& Allowed,
    const FString& Prefix,
    const TArray<FString>& FieldSegments,
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    TSharedPtr<FUnrealMCPRecord>& Out,
    FUnrealMCPError& OutError)
{
    FUnrealMCPStructuredDataSource Source{Object != nullptr ? Object->GetClass() : nullptr, Object, Object, true};
    if (FieldSegments.IsEmpty())
    {
        return StructuredDataInspection::BuildSelectedPropertyPage(
            Source, Prefix, Allowed, Context.PageIndex, Context.PageSize,
            Context.Identity.SnapshotId, Out, OutError);
    }
    const FString& Requested = FieldSegments[0];
    const bool bAllowed = Allowed.ContainsByPredicate([Object, &Requested](const FString& Name)
    {
        FProperty* Property = Object != nullptr ? Object->GetClass()->FindPropertyByName(FName(*Name)) : nullptr;
        return Name == Requested || (Property != nullptr
            && Object->GetClass()->GetAuthoredNameForField(Property) == Requested);
    });
    if (!bAllowed)
    {
        OutError = {TEXT("not_found"), TEXT("The selected UMG property is not exposed by the base semantic view")};
        return false;
    }
    return StructuredDataInspection::InspectField(
        Source, Prefix, FieldSegments, CanonicalSelector(Context.Selector.Segments),
        Context.PageIndex, Context.PageSize, Context.bHasPaging,
        Context.Identity.SnapshotId, Out, OutError);
}

bool RequireNoPaging(const FUnrealMCPAssetFamilyInspectionContext& Context, FUnrealMCPError& OutError)
{
    if (!Context.bHasPaging) return true;
    OutError = {TEXT("invalid_argument"), TEXT("Paging parameters require a pageable UMG collection selector")};
    return false;
}

class FUMGInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        if (Context.bHasPartialGraphFlag)
        {
            OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
            return false;
        }
        FModel Model;
        if (!BuildModel(Cast<UWidgetBlueprint>(Context.Asset), Model, OutError)) return false;
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();

        if (Context.Selector.IsRoot())
        {
            if (!RequireNoPaging(Context, OutError)) return false;
            BuildRoot(Model, Result);
        }
        else if (!BuildSelection(Context, Model, Result, OutError))
        {
            return false;
        }
        return Snapshot.Add(TEXT("umg_snapshot"), BuildSnapshot(Context.Asset), OutError)
            && RegisterRoutes(Selectors, OutError)
            && AddResult(Result, Document, OutError);
    }

private:
    static void BuildRoot(const FModel& Model, const TSharedRef<FUnrealMCPRecord>& Result)
    {
        const TSharedRef<FUnrealMCPRecord> Blueprint = MakeShared<FUnrealMCPRecord>();
        Blueprint->SetStringField(TEXT("palette_category"), Model.Blueprint->PaletteCategory);
        Blueprint->SetBoolField(TEXT("can_initialize_without_player_context"),
            Model.Blueprint->bCanCallInitializedWithoutPlayerContext);
        Blueprint->SetBoolField(TEXT("property_bindings_allowed"), Model.Blueprint->ArePropertyBindingsAllowed());
        const TSharedRef<FUnrealMCPRecord> Tick = MakeShared<FUnrealMCPRecord>();
        AddSemanticProperty(Model.Defaults, TEXT("TickFrequency"), TEXT("desired_frequency"), Tick);
        AddSemanticProperty(Model.Blueprint, TEXT("TickPrediction"), TEXT("compile_prediction"), Tick);
        AddSemanticProperty(Model.Blueprint, TEXT("TickPredictionReason"), TEXT("prediction_reason"), Tick);
        Blueprint->SetObjectField(TEXT("tick"), Tick);
        Result->SetObjectField(TEXT("widget_blueprint"), Blueprint);

        const TSharedRef<FUnrealMCPRecord> Widget = MakeShared<FUnrealMCPRecord>();
        for (const TPair<const TCHAR*, const TCHAR*>& Pair : {
            TPair<const TCHAR*, const TCHAR*>(TEXT("Visibility"), TEXT("visibility")),
            {TEXT("bIsEnabled"), TEXT("enabled")}, {TEXT("RenderOpacity"), TEXT("render_opacity")},
            {TEXT("Clipping"), TEXT("clipping")}, {TEXT("bIsVolatile"), TEXT("volatile")},
            {TEXT("Cursor"), TEXT("cursor")}, {TEXT("RenderTransform"), TEXT("render_transform")},
            {TEXT("RenderTransformPivot"), TEXT("render_transform_pivot")}})
            AddSemanticProperty(Model.Defaults, Pair.Key, Pair.Value, Widget);
        Widget->SetObjectField(TEXT("navigation"), NavigationRecord(Model.Defaults));
        Result->SetObjectField(TEXT("widget"), Widget);

        const TSharedRef<FUnrealMCPRecord> UserWidget = MakeShared<FUnrealMCPRecord>();
        for (const TPair<const TCHAR*, const TCHAR*>& Pair : {
            TPair<const TCHAR*, const TCHAR*>(TEXT("ColorAndOpacity"), TEXT("color_and_opacity")),
            {TEXT("ForegroundColor"), TEXT("foreground_color")}, {TEXT("Padding"), TEXT("padding")},
            {TEXT("bIsFocusable"), TEXT("focusable")}, {TEXT("Priority"), TEXT("input_action_priority")},
            {TEXT("bStopAction"), TEXT("input_action_blocking")}})
            AddSemanticProperty(Model.Defaults, Pair.Key, Pair.Value, UserWidget);
        Result->SetObjectField(TEXT("user_widget"), UserWidget);

        const TSharedRef<FUnrealMCPRecord> Tree = MakeShared<FUnrealMCPRecord>();
        if (Model.RootWidget.IsEmpty()) Tree->SetField(TEXT("root_widget"), MakeShared<FUnrealMCPValueNull>());
        else Tree->SetStringField(TEXT("root_widget"), Model.RootWidget);
        Tree->SetNumberField(TEXT("widget_count"), Model.Widgets.Num());
        Tree->SetNumberField(TEXT("maximum_depth"), Model.MaximumDepth);
        Tree->SetObjectField(TEXT("widgets"), CollectionDescriptor(TEXT("widget_tree"), Model.Widgets.Num()));
        Result->SetObjectField(TEXT("widget_tree"), Tree);
        Result->SetObjectField(TEXT("named_slots"), CollectionDescriptor(TEXT("named_slots"), Model.NamedSlots.Num()));
        const TSharedRef<FUnrealMCPRecord> Bindings = MakeShared<FUnrealMCPRecord>();
        int32 PropertyCount = 0;
        int32 EventCount = 0;
        for (const FBindingEntry& Entry : Model.Bindings)
        {
            if (Entry.Kind == TEXT("property_binding")) ++PropertyCount;
            else if (Entry.Kind == TEXT("event_binding")) ++EventCount;
        }
        Bindings->SetNumberField(TEXT("property_count"), PropertyCount);
        Bindings->SetNumberField(TEXT("event_count"), EventCount);
        Bindings->SetStringField(TEXT("selector"), TEXT("bindings"));
        Result->SetObjectField(TEXT("bindings"), Bindings);
        TArray<TSharedPtr<FUnrealMCPValue>> SelectorValues;
        for (const TCHAR* Selector : {TEXT("widget_tree"), TEXT("widgets"), TEXT("named_slots"), TEXT("bindings"), TEXT("properties")})
            SelectorValues.Add(MakeShared<FUnrealMCPValueString>(Selector));
        Result->SetArrayField(TEXT("selectors"), SelectorValues);
    }

    static bool BuildSelection(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        const FModel& Model,
        const TSharedRef<FUnrealMCPRecord>& Result,
        FUnrealMCPError& OutError)
    {
        AddSelection(Context, Result);
        const TArray<FString>& Segments = Context.Selector.Segments;
        if ((Segments[0] == TEXT("widget_tree") || Segments[0] == TEXT("widgets"))
            && Segments.Num() == 1)
        {
            int32 Start = 0, End = 0;
            PageBounds(Context, Model.Widgets.Num(), Start, End);
            TArray<TSharedPtr<FUnrealMCPValue>> Values;
            for (int32 Index = Start; Index < End; ++Index)
                Values.Add(MakeShared<FUnrealMCPValueObject>(WidgetIndexRecord(Model.Widgets[Index], Model)));
            Result->SetArrayField(TEXT("widgets"), Values);
            Result->SetObjectField(TEXT("page"), PageRecord(Context, Model.Widgets.Num(), Values.Num()));
            return true;
        }
        if (Segments[0] == TEXT("widgets") && Segments.Num() >= 2)
            return BuildWidgetSelection(Context, Model, Result, OutError);
        if (Segments[0] == TEXT("named_slots") && Segments.Num() == 1)
        {
            int32 Start = 0, End = 0;
            PageBounds(Context, Model.NamedSlots.Num(), Start, End);
            TArray<TSharedPtr<FUnrealMCPValue>> Values;
            for (int32 Index = Start; Index < End; ++Index)
            {
                const FNamedSlotEntry& Source = Model.NamedSlots[Index];
                const TSharedRef<FUnrealMCPRecord> Slot = MakeShared<FUnrealMCPRecord>();
                if (Source.Host.IsEmpty()) Slot->SetField(TEXT("host"), MakeShared<FUnrealMCPValueNull>());
                else Slot->SetStringField(TEXT("host"), Source.Host);
                Slot->SetStringField(TEXT("name"), Source.Name);
                if (Source.Content.IsEmpty()) Slot->SetField(TEXT("content"), MakeShared<FUnrealMCPValueNull>());
                else Slot->SetStringField(TEXT("content"), Source.Content);
                Slot->SetStringField(TEXT("ownership"), Source.Ownership);
                Slot->SetStringField(TEXT("declared_by"), Source.DeclaredBy);
                Slot->SetBoolField(TEXT("available_to_subclasses"), Source.bAvailableToSubclasses);
                Slot->SetBoolField(TEXT("exposed_on_instance"), Source.bExposedOnInstance);
                Values.Add(MakeShared<FUnrealMCPValueObject>(Slot));
            }
            Result->SetArrayField(TEXT("named_slots"), Values);
            Result->SetObjectField(TEXT("page"), PageRecord(Context, Model.NamedSlots.Num(), Values.Num()));
            return true;
        }
        if (Segments[0] == TEXT("bindings") && Segments.Num() == 1)
            return BuildBindingPage(Context, Model.Bindings, Result);
        if (Segments[0] == TEXT("properties"))
        {
            TArray<FString> Fields = Segments;
            Fields.RemoveAt(0);
            TSharedPtr<FUnrealMCPRecord> Properties;
            if (!BuildPropertySelection(Model.Defaults, UserWidgetPropertyNames(), TEXT("properties"),
                Fields, Context, Properties, OutError)) return false;
            for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Properties->Values)
                Result->SetField(Field.Key, Field.Value);
            return true;
        }
        OutError = {TEXT("not_found"), TEXT("The selected UMG semantic child was not found")};
        return false;
    }

    static bool BuildWidgetSelection(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        const FModel& Model,
        const TSharedRef<FUnrealMCPRecord>& Result,
        FUnrealMCPError& OutError)
    {
        const TArray<FString>& Segments = Context.Selector.Segments;
        const FWidgetEntry* Entry = Model.FindWidget(Segments[1]);
        if (Entry == nullptr)
        {
            OutError = {TEXT("not_found"), TEXT("The selected effective widget was not found")};
            return false;
        }
        const FString Prefix = TEXT("widgets/")
            + StructuredDataInspection::EncodeSelectorSegment(Entry->Name);
        if (Segments.Num() == 2)
        {
            if (!RequireNoPaging(Context, OutError)) return false;
            const TSharedRef<FUnrealMCPRecord> Widget = WidgetIndexRecord(*Entry, Model);
            const TArray<FString> Properties = WidgetPropertyNames(Entry->Widget);
            Widget->SetObjectField(TEXT("properties"), CollectionDescriptor(
                Prefix + TEXT("/properties"), AvailablePropertyCount(Entry->Widget, Properties)));
            if (Entry->Slot != nullptr)
            {
                const TSharedRef<FUnrealMCPRecord> Slot = MakeShared<FUnrealMCPRecord>();
                Slot->SetStringField(TEXT("class"), Entry->Slot->GetClass()->GetPathName());
                Slot->SetStringField(TEXT("parent"), Entry->Parent);
                Slot->SetNumberField(TEXT("child_index"), Entry->ChildIndex);
                Slot->SetObjectField(TEXT("properties"), CollectionDescriptor(
                    Prefix + TEXT("/slot/properties"),
                    AvailablePropertyCount(Entry->Slot, SlotPropertyNames(Entry->Slot))));
                Widget->SetObjectField(TEXT("slot"), Slot);
            }
            Widget->SetObjectField(TEXT("children"), CollectionDescriptor(
                Prefix + TEXT("/children"), Model.ChildrenOf(Entry->Name).Num()));
            Widget->SetObjectField(TEXT("bindings"), CollectionDescriptor(
                Prefix + TEXT("/bindings"), Model.BindingCountFor(Entry->Name)));
            Result->SetObjectField(TEXT("widget"), Widget);
            return true;
        }
        if (Segments[2] == TEXT("properties"))
        {
            TArray<FString> Fields = Segments;
            Fields.RemoveAt(0, 3);
            TSharedPtr<FUnrealMCPRecord> Properties;
            if (!BuildPropertySelection(Entry->Widget, WidgetPropertyNames(Entry->Widget),
                Prefix + TEXT("/properties"), Fields, Context, Properties, OutError)) return false;
            for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Properties->Values)
                Result->SetField(Field.Key, Field.Value);
            return true;
        }
        if (Segments[2] == TEXT("slot") && Segments.Num() >= 4 && Segments[3] == TEXT("properties"))
        {
            if (Entry->Slot == nullptr)
            {
                OutError = {TEXT("not_found"), TEXT("The selected widget has no panel slot")}; return false;
            }
            TArray<FString> Fields = Segments;
            Fields.RemoveAt(0, 4);
            TSharedPtr<FUnrealMCPRecord> Properties;
            if (!BuildPropertySelection(Entry->Slot, SlotPropertyNames(Entry->Slot),
                Prefix + TEXT("/slot/properties"), Fields, Context, Properties, OutError)) return false;
            for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Properties->Values)
                Result->SetField(Field.Key, Field.Value);
            return true;
        }
        if (Segments[2] == TEXT("children") && Segments.Num() == 3)
        {
            const TArray<FString> Children = Model.ChildrenOf(Entry->Name);
            int32 Start = 0, End = 0;
            PageBounds(Context, Children.Num(), Start, End);
            TArray<TSharedPtr<FUnrealMCPValue>> Values;
            for (int32 Index = Start; Index < End; ++Index)
                if (const FWidgetEntry* Child = Model.FindWidget(Children[Index]))
                    Values.Add(MakeShared<FUnrealMCPValueObject>(WidgetIndexRecord(*Child, Model)));
            Result->SetArrayField(TEXT("children"), Values);
            Result->SetObjectField(TEXT("page"), PageRecord(Context, Children.Num(), Values.Num()));
            return true;
        }
        if (Segments[2] == TEXT("bindings") && Segments.Num() == 3)
        {
            TArray<FBindingEntry> Bindings;
            for (const FBindingEntry& Binding : Model.Bindings)
                if (Binding.Widget == Entry->Name) Bindings.Add(Binding);
            return BuildBindingPage(Context, Bindings, Result);
        }
        OutError = {TEXT("not_found"), TEXT("The selected widget semantic child was not found")};
        return false;
    }

    static bool BuildBindingPage(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        const TArray<FBindingEntry>& Bindings,
        const TSharedRef<FUnrealMCPRecord>& Result)
    {
        int32 Start = 0, End = 0;
        PageBounds(Context, Bindings.Num(), Start, End);
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        for (int32 Index = Start; Index < End; ++Index)
            Values.Add(MakeShared<FUnrealMCPValueObject>(BindingRecord(Bindings[Index])));
        Result->SetArrayField(TEXT("bindings"), Values);
        Result->SetObjectField(TEXT("page"), PageRecord(Context, Bindings.Num(), Values.Num()));
        return true;
    }
};
}
}

bool UnrealMCP::UMGInspection::RegisterAdapter(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError)
{
    using namespace Private;
    FUnrealMCPAssetFamilyDescriptor Descriptor;
    Descriptor.FamilyId = TEXT("umg_widget");
    Descriptor.NativeClass = UUserWidget::StaticClass();
    Descriptor.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Descriptor.Priority = 150;
    Descriptor.RequiredModules = {TEXT("UnrealMCPUMG"), TEXT("UnrealMCPBlueprint"), TEXT("UMG"), TEXT("UMGEditor")};
    Descriptor.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Descriptor.Bounds.MaxValueNodes = 65536;
    Descriptor.Limits = {
        {TEXT("page_size"), UnrealMCP::MaxAssetInspectPageSize},
        {TEXT("selector_bytes"), UnrealMCP::MaxAssetInspectSelectorBytes},
        {TEXT("widgets"), UnrealMCP::MaxWidgetTreeWidgets},
        {TEXT("tree_depth"), UnrealMCP::MaxWidgetTreeDepth},
        {TEXT("named_slots"), UnrealMCP::MaxWidgetNamedSlots},
        {TEXT("bindings"), UnrealMCP::MaxWidgetBindings}};
    Descriptor.Capabilities.bInspection = true;
    Descriptor.SelectorRoutes = Routes();
    Descriptor.bComposableInspectionOverlay = true;
    Descriptor.InspectionAdapter = MakeShared<FUMGInspectionAdapter>();
    Descriptor.SnapshotBuilder = [](UObject* Asset) { return Private::BuildSnapshot(Asset); };
    return Registry.Register(MoveTemp(Descriptor), OutError);
}
