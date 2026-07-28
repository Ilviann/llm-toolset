#pragma once

#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPWidgetTreeSupport.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{
inline FString WidgetDefaultsFingerprint(
    UWidget* Widget,
    TArray<TSharedPtr<FJsonValue>>& OutChanged,
    int32& OutWidgetChangedCount,
    int32& InOutChangedCount)
{
    TArray<FProperty*> Changed;
    UObject* Archetype = Widget != nullptr ? Widget->GetArchetype() : nullptr;
    if (Widget != nullptr)
    {
        for (TFieldIterator<FProperty> It(Widget->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_Transient)
                && (Archetype == nullptr || !Property->Identical_InContainer(Widget, Archetype)))
            {
                Changed.Add(Property);
            }
        }
    }
    Changed.Sort([](const FProperty& Left, const FProperty& Right)
    {
        return Left.GetName() < Right.GetName();
    });
    OutWidgetChangedCount = Changed.Num();
    InOutChangedCount += OutWidgetChangedCount;
    TArray<FString> Fingerprint;
    const int32 VisibleCount = FMath::Min(Changed.Num(), UnrealMCP::MaxWidgetDefaultsPerWidget);
    for (int32 Index = 0; Index < Changed.Num(); ++Index)
    {
        FProperty* Property = Changed[Index];
        FString Kind;
        const bool bSupported = UnrealMCP::PropertyCodec::IsSupportedEditable(Property, Kind);
        FString Encoded;
        Property->ExportText_InContainer(0, Encoded, Widget, Archetype, Widget, PPF_None);
        Fingerprint.Add(Property->GetName() + TEXT("|")
            + (bSupported ? Kind : TEXT("unsupported")) + TEXT("|") + HashLines({Encoded}));
        if (Index < VisibleCount)
        {
            OutChanged.Add(MakeShared<FJsonValueObject>(
                UnrealMCP::PropertyCodec::Encode(Widget, Property)));
        }
    }
    return FString::Join(Fingerprint, TEXT(";"));
}

inline bool CollectWidgetTree(
    UBlueprint* Blueprint,
    const FString& WidgetFilter,
    const TSet<FString>& PropertyNames,
    const TSet<FString>& Sections,
    FInspectionSink& Sink,
    FUnrealMCPError& OutError)
{
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
    if (WidgetBlueprint == nullptr)
    {
        if (!WidgetFilter.IsEmpty() || Sections.Contains(TEXT("widget_defaults")))
        {
            OutError = {TEXT("wrong_type"), TEXT("Widget inspection requires a Widget Blueprint")};
            return false;
        }
        return true;
    }
    if (WidgetBlueprint->WidgetTree == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The Widget Blueprint tree is unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }

    TArray<UWidget*> Widgets;
    UnrealMCP::WidgetTreePrivate::CollectWidgets(WidgetBlueprint, Widgets);
    if (Widgets.Num() > UnrealMCP::MaxWidgetTreeWidgets)
    {
        OutError = {TEXT("response_too_large"), TEXT("The widget tree exceeds the configured widget limit")};
        return false;
    }
    TArray<UnrealMCP::WidgetTreePrivate::FNamedSlotRef> NamedSlots;
    UnrealMCP::WidgetTreePrivate::CollectNamedSlots(WidgetBlueprint, Widgets, NamedSlots);
    if (NamedSlots.Num() > UnrealMCP::MaxWidgetNamedSlots)
    {
        OutError = {TEXT("response_too_large"), TEXT("The widget tree exceeds the configured named-slot limit")};
        return false;
    }
    bool bCycle = false;
    const int32 TreeDepth =
        UnrealMCP::WidgetTreePrivate::ComputeDepth(WidgetBlueprint, Widgets, NamedSlots, bCycle);
    if (bCycle || TreeDepth > UnrealMCP::MaxWidgetTreeDepth)
    {
        OutError = {
            bCycle ? TEXT("invalid_widget_tree") : TEXT("response_too_large"),
            bCycle ? TEXT("The widget tree contains a cycle")
                   : TEXT("The widget tree exceeds the configured depth limit")};
        return false;
    }

    TMap<UWidget*, FString> NamedParentSlots;
    for (const UnrealMCP::WidgetTreePrivate::FNamedSlotRef& Slot : NamedSlots)
    {
        if (Slot.Content != nullptr)
        {
            NamedParentSlots.Add(Slot.Content, Slot.Id);
        }
    }

    bool bWidgetFound = WidgetFilter.IsEmpty();
    int32 ChangedDefaultCount = 0;
    for (UWidget* Widget : Widgets)
    {
        const FString Id = UnrealMCP::WidgetTreePrivate::WidgetId(WidgetBlueprint, Widget);
        const bool bSelected = WidgetFilter.IsEmpty() || WidgetFilter == Id;
        bWidgetFound |= bSelected;
        const TSharedRef<FJsonObject> Value = Record(TEXT("widget"));
        TArray<TSharedPtr<FJsonValue>> ChangedDefaults;
        int32 WidgetChangedDefaultCount = 0;
        const FString DefaultsFingerprint =
            WidgetDefaultsFingerprint(
                Widget, ChangedDefaults, WidgetChangedDefaultCount, ChangedDefaultCount);
        if (ChangedDefaultCount > UnrealMCP::MaxWidgetChangedDefaults)
        {
            OutError = {TEXT("response_too_large"), TEXT("The widget tree exceeds the changed-default limit")};
            return false;
        }

        UPanelWidget* Parent = Widget->GetParent();
        const FString ParentId = UnrealMCP::WidgetTreePrivate::WidgetId(WidgetBlueprint, Parent);
        const FString* NamedSlot = NamedParentSlots.Find(Widget);
        const FString SlotId = Parent != nullptr
            ? UnrealMCP::WidgetTreePrivate::PanelSlotId(ParentId, Id)
            : NamedSlot != nullptr ? *NamedSlot : FString();
        const int32 ChildIndex = Parent != nullptr ? Parent->GetChildIndex(Widget) : INDEX_NONE;
        if (Sections.Contains(TEXT("widget_tree")) && bSelected)
        {
            Value->SetStringField(TEXT("id"), Id);
            Value->SetBoolField(TEXT("identity_stable"), !Id.IsEmpty());
            Value->SetStringField(TEXT("name"), Widget->GetName());
            Value->SetStringField(TEXT("class_path"), Widget->GetClass()->GetPathName());
            Value->SetStringField(TEXT("ownership"), TEXT("local"));
            Value->SetBoolField(TEXT("editable"), !Id.IsEmpty());
            Value->SetBoolField(TEXT("root"), WidgetBlueprint->WidgetTree->RootWidget == Widget);
            Value->SetBoolField(TEXT("is_variable"), Widget->bIsVariable != 0);
            Value->SetStringField(TEXT("parent_id"), ParentId);
            Value->SetStringField(TEXT("slot_id"), SlotId);
            Value->SetNumberField(TEXT("child_index"), ChildIndex);
            Value->SetArrayField(TEXT("changed_defaults"), ChangedDefaults);
            Value->SetNumberField(TEXT("changed_default_count"), WidgetChangedDefaultCount);
            Value->SetBoolField(
                TEXT("defaults_truncated"),
                WidgetChangedDefaultCount > UnrealMCP::MaxWidgetDefaultsPerWidget);
            AddRecord(Sink.Records, Value);
        }
        if (Sections.Contains(TEXT("widget_defaults")) && bSelected)
        {
            TArray<FString> Names = PropertyNames.Array();
            Names.Sort();
            for (const FString& Name : Names)
            {
                const TSharedRef<FJsonObject> Default = Record(TEXT("widget_default"));
                Default->SetStringField(TEXT("widget_id"), Id);
                const TSharedRef<FJsonObject> Encoded = UnrealMCP::PropertyCodec::Encode(
                    Widget, Widget->GetClass()->FindPropertyByName(FName(*Name)));
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Encoded->Values)
                {
                    Default->SetField(Pair.Key, Pair.Value);
                }
                AddRecord(Sink.Records, Default);
            }
        }
        Sink.Fingerprint.Add(TEXT("widget|") + Id + TEXT("|") + Widget->GetName()
            + TEXT("|") + Widget->GetClass()->GetPathName() + TEXT("|") + ParentId
            + TEXT("|") + SlotId + TEXT("|") + LexToString(ChildIndex)
            + TEXT("|") + (Widget->bIsVariable ? TEXT("1") : TEXT("0"))
            + TEXT("|") + DefaultsFingerprint);
    }
    if (!bWidgetFound)
    {
        OutError = {TEXT("not_found"), TEXT("The requested widget identity was not found")};
        return false;
    }

    for (UWidget* ParentWidget : Widgets)
    {
        UPanelWidget* Panel = Cast<UPanelWidget>(ParentWidget);
        if (Panel == nullptr)
        {
            continue;
        }
        const FString ParentId =
            UnrealMCP::WidgetTreePrivate::WidgetId(WidgetBlueprint, ParentWidget);
        for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
        {
            UWidget* Child = Panel->GetChildAt(Index);
            const FString ChildId =
                UnrealMCP::WidgetTreePrivate::WidgetId(WidgetBlueprint, Child);
            const FString Id = UnrealMCP::WidgetTreePrivate::PanelSlotId(ParentId, ChildId);
            if (Sections.Contains(TEXT("widget_tree")))
            {
                const TSharedRef<FJsonObject> Slot = Record(TEXT("widget_slot"));
                Slot->SetStringField(TEXT("id"), Id);
                Slot->SetBoolField(TEXT("identity_stable"), !Id.IsEmpty());
                Slot->SetStringField(TEXT("kind"), TEXT("panel"));
                Slot->SetStringField(TEXT("parent_id"), ParentId);
                Slot->SetStringField(TEXT("child_id"), ChildId);
                Slot->SetNumberField(TEXT("child_index"), Index);
                Slot->SetStringField(
                    TEXT("slot_class"),
                    Child != nullptr && Child->Slot != nullptr
                        ? Child->Slot->GetClass()->GetPathName() : FString());
                AddRecord(Sink.Records, Slot);
            }
            Sink.Fingerprint.Add(TEXT("widget_slot|panel|") + Id + TEXT("|")
                + ParentId + TEXT("|") + ChildId + TEXT("|") + LexToString(Index));
        }
    }
    for (const UnrealMCP::WidgetTreePrivate::FNamedSlotRef& Ref : NamedSlots)
    {
        const FString ChildId =
            UnrealMCP::WidgetTreePrivate::WidgetId(WidgetBlueprint, Ref.Content);
        if (Sections.Contains(TEXT("widget_tree")))
        {
            const TSharedRef<FJsonObject> Slot = Record(TEXT("widget_slot"));
            Slot->SetStringField(TEXT("id"), Ref.Id);
            Slot->SetBoolField(TEXT("identity_stable"), !Ref.Id.IsEmpty());
            Slot->SetStringField(TEXT("kind"), TEXT("named_slot"));
            Slot->SetStringField(TEXT("parent_id"), Ref.HostId);
            Slot->SetStringField(TEXT("child_id"), ChildId);
            Slot->SetStringField(TEXT("name"), Ref.Name.ToString());
            Slot->SetBoolField(TEXT("inherited"), Ref.bTreeHost);
            AddRecord(Sink.Records, Slot);
        }
        Sink.Fingerprint.Add(TEXT("widget_slot|named|") + Ref.Id + TEXT("|")
            + Ref.HostId + TEXT("|") + Ref.Name.ToString() + TEXT("|") + ChildId);
    }
    Sink.Fingerprint.Add(TEXT("widget_tree_depth|") + LexToString(TreeDepth));
    return true;
}
}
