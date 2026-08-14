#include "UnrealMCPUMGInspectionModel.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPStructuredDataInspection.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWidgetInspectionSupport.h"
#include "UnrealMCPWidgetTreeSupport.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace UnrealMCP::UMGInspection::Private
{
namespace
{
FString Sha1(const FString& Material)
{
    const FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

FString DeclaringClass(UWidget* Widget)
{
    const UWidgetTree* Tree = Widget != nullptr ? Widget->GetTypedOuter<UWidgetTree>() : nullptr;
    const UWidgetBlueprintGeneratedClass* Class =
        Tree != nullptr ? Cast<UWidgetBlueprintGeneratedClass>(Tree->GetOuter()) : nullptr;
    if (Class != nullptr) return Class->GetPathName();
    const UWidgetBlueprint* Blueprint =
        Tree != nullptr ? Cast<UWidgetBlueprint>(Tree->GetOuter()) : nullptr;
    return Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass->GetPathName() : FString();
}

FString EventName(UEdGraphNode* Node)
{
    if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node))
        return Custom->CustomFunctionName.ToString();
    if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
    {
        const FName MemberName = Event->EventReference.GetMemberName();
        return !MemberName.IsNone()
            ? MemberName.ToString()
            : Event->GetNodeTitle(ENodeTitleType::ListView).ToString();
    }
    return FString();
}

FString NameForWidgetId(UWidgetBlueprint* Blueprint, const FString& Id)
{
    if (Blueprint == nullptr || Id.IsEmpty()) return FString();
    for (const TPair<FName, FGuid>& Pair : Blueprint->WidgetVariableNameToGuidMap)
    {
        if (UnrealMCP::WidgetTreePrivate::GuidString(Pair.Value) == Id)
            return Pair.Key.ToString();
    }
    return FString();
}

FString PropertyType(const FProperty* Property)
{
    return Property != nullptr ? Property->GetCPPType() : FString();
}

FString FunctionSignature(const UFunction* Function)
{
    if (Function == nullptr) return FString();
    TArray<FString> Parameters;
    FString ReturnType = TEXT("void");
    for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_ReturnParm)) ReturnType = PropertyType(*It);
        else Parameters.Add(PropertyType(*It) + TEXT(" ") + It->GetName());
    }
    return ReturnType + TEXT(" (") + FString::Join(Parameters, TEXT(", ")) + TEXT(")");
}

void AddWidgetAndPanelChildren(
    UWidget* Widget,
    const FString& Parent,
    int32 ChildIndex,
    bool bRoot,
    UWidgetBlueprintGeneratedClass* CurrentClass,
    TArray<FWidgetEntry>& Out)
{
    if (Widget == nullptr || Out.ContainsByPredicate(
        [Widget](const FWidgetEntry& Existing) { return Existing.Widget == Widget; })) return;
    FWidgetEntry Entry;
    Entry.Widget = Widget;
    Entry.Slot = Widget->Slot;
    Entry.Name = Widget->GetName();
    Entry.ClassPath = Widget->GetClass()->GetPathName();
    Entry.DeclaredBy = DeclaringClass(Widget);
    Entry.Ownership = Entry.DeclaredBy == CurrentClass->GetPathName()
        ? TEXT("local") : TEXT("inherited");
    Entry.Parent = Parent;
    Entry.ChildIndex = ChildIndex;
    Entry.bRoot = bRoot;
    Entry.bVariable = Widget->bIsVariable != 0;
    Out.Add(MoveTemp(Entry));
    if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
    {
        for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
            AddWidgetAndPanelChildren(Panel->GetChildAt(Index), Widget->GetName(), Index, false, CurrentClass, Out);
    }
}

bool AddBindingsForBlueprint(UWidgetBlueprint* Blueprint, FModel& Model, FUnrealMCPError& OutError)
{
    TArray<WidgetInspection::FBindingRecord> Records;
    if (!WidgetInspection::CollectBindings(Blueprint, Records, OutError)) return false;
    const FString DeclaredBy = Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass->GetPathName() : Blueprint->GetPathName();
    for (const WidgetInspection::FBindingRecord& Source : Records)
    {
        if (!Source.Record.IsValid()) continue;
        FBindingEntry Entry;
        Entry.Widget = NameForWidgetId(Blueprint, Source.WidgetId);
        Entry.DeclaredBy = DeclaredBy;
        Source.Record->TryGetStringField(TEXT("record_type"), Entry.Kind);
        Source.Record->TryGetStringField(TEXT("cost"), Entry.Cost);
        if (Entry.Kind == TEXT("property_binding"))
        {
            Source.Record->TryGetStringField(TEXT("target_property"), Entry.Target);
            Source.Record->TryGetStringField(TEXT("source_kind"), Entry.SourceKind);
            Source.Record->TryGetStringField(TEXT("source_name"), Entry.SourceName);
            if (const FWidgetEntry* Target = Model.FindWidget(Entry.Widget))
                Entry.TargetType = PropertyType(Target->Widget->GetClass()->FindPropertyByName(FName(*Entry.Target)));
            if (Entry.SourceKind == TEXT("function"))
            {
                const UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(FName(*Entry.SourceName));
                Entry.SourceType = FunctionSignature(Function);
            }
            else
            {
                Entry.SourceType = PropertyType(
                    Blueprint->GeneratedClass->FindPropertyByName(FName(*Entry.SourceName)));
            }
        }
        else if (Entry.Kind == TEXT("event_binding"))
        {
            Source.Record->TryGetStringField(TEXT("delegate_name"), Entry.Target);
            if (const FWidgetEntry* Target = Model.FindWidget(Entry.Widget))
            {
                const FMulticastDelegateProperty* Delegate = CastField<FMulticastDelegateProperty>(
                    Target->Widget->GetClass()->FindPropertyByName(FName(*Entry.Target)));
                Entry.Signature = FunctionSignature(Delegate != nullptr ? Delegate->SignatureFunction : nullptr);
            }
            FString GraphId;
            FString NodeId;
            Source.Record->TryGetStringField(TEXT("graph_id"), GraphId);
            Source.Record->TryGetStringField(TEXT("node_id"), NodeId);
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                if (Graph == nullptr || UnrealMCP::WidgetTreePrivate::GuidString(Graph->GraphGuid) != GraphId) continue;
                Entry.Graph = Graph->GetName();
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (Node != nullptr && UnrealMCP::WidgetTreePrivate::GuidString(Node->NodeGuid) == NodeId)
                    {
                        Entry.Event = EventName(Node);
                        break;
                    }
                }
                break;
            }
        }
        if (!Entry.Widget.IsEmpty()) Model.Bindings.Add(MoveTemp(Entry));
    }
    return true;
}
}

const FWidgetEntry* FModel::FindWidget(const FString& Name) const
{
    return Widgets.FindByPredicate([&Name](const FWidgetEntry& Entry) { return Entry.Name == Name; });
}

TArray<FString> FModel::ChildrenOf(const FString& Parent) const
{
    TArray<const FWidgetEntry*> Matches;
    for (const FWidgetEntry& Entry : Widgets) if (Entry.Parent == Parent) Matches.Add(&Entry);
    Matches.Sort([](const FWidgetEntry& Left, const FWidgetEntry& Right)
    {
        return Left.ChildIndex == Right.ChildIndex ? Left.Name < Right.Name : Left.ChildIndex < Right.ChildIndex;
    });
    TArray<FString> Result;
    for (const FWidgetEntry* Entry : Matches) Result.Add(Entry->Name);
    return Result;
}

int32 FModel::BindingCountFor(const FString& Widget) const
{
    int32 Result = 0;
    for (const FBindingEntry& Entry : Bindings) if (Entry.Widget == Widget) ++Result;
    return Result;
}

TArray<FString> UserWidgetPropertyNames()
{
    return {
        TEXT("Visibility"), TEXT("bIsEnabled"), TEXT("RenderOpacity"), TEXT("Clipping"),
        TEXT("bIsVolatile"), TEXT("Cursor"), TEXT("RenderTransform"), TEXT("RenderTransformPivot"),
        TEXT("Navigation"), TEXT("ColorAndOpacity"), TEXT("ForegroundColor"), TEXT("Padding"),
        TEXT("bIsFocusable"), TEXT("Priority"), TEXT("bStopAction"), TEXT("TickFrequency")};
}

TArray<FString> WidgetPropertyNames(const UWidget* Widget)
{
    return WidgetInspection::SupportedStyleProperties(Widget);
}

TArray<FString> SlotPropertyNames(const UPanelSlot* Slot)
{
    return WidgetInspection::SupportedLayoutProperties(Slot);
}

bool BuildModel(UWidgetBlueprint* Blueprint, FModel& OutModel, FUnrealMCPError& OutError)
{
    OutModel = {};
    OutModel.Blueprint = Blueprint;
    OutModel.GeneratedClass = Blueprint != nullptr
        ? Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass) : nullptr;
    OutModel.Defaults = OutModel.GeneratedClass != nullptr
        ? Cast<UUserWidget>(OutModel.GeneratedClass->GetDefaultObject(false)) : nullptr;
    if (Blueprint == nullptr || OutModel.GeneratedClass == nullptr || OutModel.Defaults == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The Widget Blueprint generated class defaults are unavailable"),
            MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    UWidgetBlueprintGeneratedClass* TreeClass = OutModel.GeneratedClass;
    UWidgetTree* Tree = Blueprint->WidgetTree;
    if (Tree == nullptr || Tree->RootWidget == nullptr)
    {
        TreeClass = OutModel.GeneratedClass->FindWidgetTreeOwningClass();
        Tree = TreeClass != nullptr ? TreeClass->GetWidgetTreeArchetype() : nullptr;
    }
    if (Tree == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The effective Widget Blueprint tree is unavailable"),
            MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    if (Tree->RootWidget != nullptr)
    {
        OutModel.RootWidget = Tree->RootWidget->GetName();
        AddWidgetAndPanelChildren(Tree->RootWidget, FString(), INDEX_NONE, true,
            OutModel.GeneratedClass, OutModel.Widgets);
    }

    TMap<FName, UWidget*> EffectiveSlotContent;
    OutModel.GeneratedClass->GetNamedSlotArchetypeContent(
        [&EffectiveSlotContent](FName Name, UWidget* Content) { EffectiveSlotContent.Add(Name, Content); });
    TArray<FName> SlotNames = OutModel.GeneratedClass->NamedSlots;
    SlotNames.Sort(FNameLexicalLess());
    for (const FName SlotName : SlotNames)
    {
        UWidget* Content = EffectiveSlotContent.FindRef(SlotName);
        UWidget* Host = Tree->FindWidget(SlotName);
        if (Content != nullptr && OutModel.FindWidget(Content->GetName()) == nullptr)
            AddWidgetAndPanelChildren(Content, Host != nullptr ? Host->GetName() : FString(), 0, false,
                OutModel.GeneratedClass, OutModel.Widgets);
        FNamedSlotEntry Slot;
        Slot.Host = Host != nullptr ? Host->GetName() : FString();
        Slot.Name = SlotName.ToString();
        Slot.Content = Content != nullptr ? Content->GetName() : FString();
        Slot.DeclaredBy = Host != nullptr ? DeclaringClass(Host)
            : TreeClass != nullptr ? TreeClass->GetPathName() : FString();
        Slot.Ownership = Slot.DeclaredBy == OutModel.GeneratedClass->GetPathName()
            ? TEXT("local") : TEXT("inherited");
        Slot.bAvailableToSubclasses = OutModel.GeneratedClass->AvailableNamedSlots.Contains(SlotName);
        Slot.bExposedOnInstance = OutModel.GeneratedClass->InstanceNamedSlots.Contains(SlotName);
        OutModel.NamedSlots.Add(MoveTemp(Slot));
    }

    TSet<FString> Names;
    for (const FWidgetEntry& Entry : OutModel.Widgets)
    {
        if (Names.Contains(Entry.Name))
        {
            OutError = {TEXT("invalid_widget_tree"),
                TEXT("The effective Widget Blueprint hierarchy contains duplicate widget names")};
            return false;
        }
        Names.Add(Entry.Name);
        int32 Depth = 0;
        FString Parent = Entry.Parent;
        while (!Parent.IsEmpty() && Depth <= UnrealMCP::MaxWidgetTreeDepth)
        {
            ++Depth;
            const FWidgetEntry* ParentEntry = OutModel.FindWidget(Parent);
            Parent = ParentEntry != nullptr ? ParentEntry->Parent : FString();
        }
        OutModel.MaximumDepth = FMath::Max(OutModel.MaximumDepth, Depth);
    }
    if (OutModel.Widgets.Num() > UnrealMCP::MaxWidgetTreeWidgets
        || OutModel.MaximumDepth > UnrealMCP::MaxWidgetTreeDepth
        || OutModel.NamedSlots.Num() > UnrealMCP::MaxWidgetNamedSlots)
    {
        OutError = {TEXT("data_limit_exceeded"),
            TEXT("The effective Widget Blueprint hierarchy exceeds its configured safety limit")};
        return false;
    }

    UClass* Class = OutModel.GeneratedClass;
    while (UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(Class))
    {
        if (UWidgetBlueprint* Owner = Cast<UWidgetBlueprint>(WidgetClass->ClassGeneratedBy))
            if (!AddBindingsForBlueprint(Owner, OutModel, OutError)) return false;
        if (OutModel.Bindings.Num() > UnrealMCP::MaxWidgetBindings)
        {
            OutError = {TEXT("data_limit_exceeded"),
                TEXT("The effective Widget Blueprint exceeds the supported binding limit")};
            return false;
        }
        Class = Class->GetSuperClass();
    }
    OutModel.Bindings.Sort([](const FBindingEntry& Left, const FBindingEntry& Right)
    {
        return Left.DeclaredBy + TEXT("|") + Left.Widget + TEXT("|") + Left.Target
            < Right.DeclaredBy + TEXT("|") + Right.Widget + TEXT("|") + Right.Target;
    });
    return true;
}

FString BuildSnapshot(UObject* Asset)
{
    UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset);
    FModel Model;
    FUnrealMCPError Error;
    TArray<FString> Lines;
    Lines.Add(TEXT("asset|") + GetPathNameSafe(Asset));
    if (!BuildModel(Blueprint, Model, Error))
    {
        Lines.Add(TEXT("model_error|") + Error.Code + TEXT("|") + Error.Message);
        return Sha1(FString::Join(Lines, TEXT("\n")));
    }
    FUnrealMCPStructuredDataSource Defaults{
        Model.Defaults->GetClass(), Model.Defaults, Model.Defaults, true};
    Lines.Add(StructuredDataInspection::BuildSelectedSnapshot(
        Defaults, TEXT("user_widget_defaults"), UserWidgetPropertyNames()));
    for (const FWidgetEntry& Entry : Model.Widgets)
    {
        Lines.Add(TEXT("widget|") + Entry.Name + TEXT("|") + Entry.ClassPath + TEXT("|")
            + Entry.Ownership + TEXT("|") + Entry.DeclaredBy + TEXT("|") + Entry.Parent + TEXT("|")
            + LexToString(Entry.ChildIndex) + TEXT("|") + (Entry.bVariable ? TEXT("1") : TEXT("0")));
        FUnrealMCPStructuredDataSource Source{
            Entry.Widget->GetClass(), Entry.Widget, Entry.Widget, true};
        Lines.Add(StructuredDataInspection::BuildSelectedSnapshot(
            Source, TEXT("widget|") + Entry.Name, WidgetPropertyNames(Entry.Widget)));
        Lines.Add(TEXT("slot|") + Entry.Name + TEXT("|")
            + WidgetInspection::FingerprintLayout(Entry.Slot));
    }
    for (const FNamedSlotEntry& Slot : Model.NamedSlots)
        Lines.Add(TEXT("named_slot|") + Slot.Host + TEXT("|") + Slot.Name + TEXT("|")
            + Slot.Content + TEXT("|") + Slot.DeclaredBy);
    for (const FBindingEntry& Binding : Model.Bindings)
        Lines.Add(TEXT("binding|") + Binding.Kind + TEXT("|") + Binding.Widget + TEXT("|")
            + Binding.Target + TEXT("|") + Binding.SourceKind + TEXT("|") + Binding.SourceName
            + TEXT("|") + Binding.SourceType + TEXT("|") + Binding.TargetType + TEXT("|")
            + Binding.Signature + TEXT("|") + Binding.Graph + TEXT("|") + Binding.Event
            + TEXT("|") + Binding.DeclaredBy);
    Lines.Sort();
    return Sha1(FString::Join(Lines, TEXT("\n")));
}
}
