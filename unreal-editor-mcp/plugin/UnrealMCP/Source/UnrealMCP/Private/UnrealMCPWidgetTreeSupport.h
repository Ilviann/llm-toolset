#pragma once

#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelWidget.h"
#include "WidgetBlueprint.h"

namespace UnrealMCP::WidgetTreePrivate
{
inline FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

inline FString WidgetId(const UWidgetBlueprint* Blueprint, const UWidget* Widget)
{
    if (Blueprint == nullptr || Widget == nullptr)
    {
        return FString();
    }
    return GuidString(Blueprint->WidgetVariableNameToGuidMap.FindRef(Widget->GetFName()));
}

inline FString DeterministicId(const FString& Material)
{
    return GuidString(FGuid::NewDeterministicGuid(Material));
}

inline FString PanelSlotId(const FString& ParentId, const FString& ChildId)
{
    return ParentId.IsEmpty() || ChildId.IsEmpty()
        ? FString()
        : DeterministicId(TEXT("panel|") + ParentId + TEXT("|") + ChildId);
}

inline FString NamedSlotId(const FString& HostId, const FName SlotName)
{
    return SlotName.IsNone()
        ? FString()
        : DeterministicId(TEXT("named|") + (HostId.IsEmpty() ? TEXT("tree") : HostId)
            + TEXT("|") + SlotName.ToString());
}

struct FNamedSlotRef
{
    UObject* Host = nullptr;
    FString Id;
    FString HostId;
    FName Name;
    UWidget* Content = nullptr;
    bool bTreeHost = false;
};

inline void CollectWidgets(UWidgetBlueprint* Blueprint, TArray<UWidget*>& OutWidgets)
{
    OutWidgets.Reset();
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return;
    }
    Blueprint->WidgetTree->GetAllWidgets(OutWidgets);
    if (Blueprint->WidgetTree->RootWidget != nullptr)
    {
        OutWidgets.AddUnique(Blueprint->WidgetTree->RootWidget);
    }
    OutWidgets.RemoveAll([](const UWidget* Widget) { return Widget == nullptr; });
    OutWidgets.Sort([Blueprint](const UWidget& Left, const UWidget& Right)
    {
        const FString LeftId = WidgetId(Blueprint, &Left);
        const FString RightId = WidgetId(Blueprint, &Right);
        return LeftId == RightId ? Left.GetName() < Right.GetName() : LeftId < RightId;
    });
}

inline void CollectNamedSlots(UWidgetBlueprint* Blueprint, const TArray<UWidget*>& Widgets,
    TArray<FNamedSlotRef>& OutSlots)
{
    OutSlots.Reset();
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return;
    }
    auto AddHost = [&OutSlots](UObject* Host, const FString& HostId, const bool bTreeHost)
    {
        INamedSlotInterface* Interface = Cast<INamedSlotInterface>(Host);
        if (Interface == nullptr)
        {
            return;
        }
        TArray<FName> Names;
        Interface->GetSlotNames(Names);
        Names.Sort(FNameLexicalLess());
        for (const FName Name : Names)
        {
            OutSlots.Add({
                Host,
                NamedSlotId(HostId, Name),
                HostId,
                Name,
                Interface->GetContentForSlot(Name),
                bTreeHost});
        }
    };
    AddHost(Blueprint->WidgetTree, FString(), true);
    for (UWidget* Widget : Widgets)
    {
        AddHost(Widget, WidgetId(Blueprint, Widget), false);
    }
    OutSlots.Sort([](const FNamedSlotRef& Left, const FNamedSlotRef& Right)
    {
        return Left.Id < Right.Id;
    });
}

inline int32 ComputeDepth(
    UWidgetBlueprint* Blueprint,
    const TArray<UWidget*>& Widgets,
    const TArray<FNamedSlotRef>& NamedSlots,
    bool& bOutCycle)
{
    bOutCycle = false;
    if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
    {
        return 0;
    }
    TMap<UWidget*, TArray<UWidget*>> Children;
    for (UWidget* Widget : Widgets)
    {
        if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
            {
                if (UWidget* Child = Panel->GetChildAt(Index))
                {
                    Children.FindOrAdd(Widget).AddUnique(Child);
                }
            }
        }
    }
    TArray<UWidget*> Roots;
    if (Blueprint->WidgetTree->RootWidget != nullptr)
    {
        Roots.Add(Blueprint->WidgetTree->RootWidget);
    }
    for (const FNamedSlotRef& Slot : NamedSlots)
    {
        if (Slot.Content == nullptr)
        {
            continue;
        }
        if (Slot.bTreeHost)
        {
            Roots.AddUnique(Slot.Content);
        }
        else if (UWidget* HostWidget = Cast<UWidget>(Slot.Host))
        {
            Children.FindOrAdd(HostWidget).AddUnique(Slot.Content);
        }
    }

    int32 Maximum = 0;
    TSet<UWidget*> Active;
    TFunction<void(UWidget*, int32)> Visit = [&](UWidget* Widget, const int32 Depth)
    {
        if (Widget == nullptr || bOutCycle)
        {
            return;
        }
        if (Active.Contains(Widget))
        {
            bOutCycle = true;
            return;
        }
        Maximum = FMath::Max(Maximum, Depth);
        Active.Add(Widget);
        if (const TArray<UWidget*>* Direct = Children.Find(Widget))
        {
            for (UWidget* Child : *Direct)
            {
                Visit(Child, Depth + 1);
            }
        }
        Active.Remove(Widget);
    };
    for (UWidget* Root : Roots)
    {
        Visit(Root, 0);
    }
    return Maximum;
}
}
