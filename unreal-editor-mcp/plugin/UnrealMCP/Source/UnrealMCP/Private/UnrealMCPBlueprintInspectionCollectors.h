#pragma once

#include "UnrealMCPBlueprintInspectionSupport.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{
static bool CollectCustomEvents(
    UBlueprint* Blueprint,
    const TArray<TPair<UBlueprint*, FString>>& Owners,
    const TSet<FString>& Sections,
    const FString& FunctionFilter,
    const FString& LocalFilter,
    const FString& MacroFilter,
    const FString& CustomEventFilter,
    FInspectionSink& Sink,
    FUnrealMCPError& OutError)
{
bool bCustomEventFound = CustomEventFilter.IsEmpty();
for (const TPair<UBlueprint*, FString>& Owner : Owners)
{
    for (UEdGraph* EventGraph : Owner.Key->UbergraphPages)
    {
        if (EventGraph == nullptr || !FBlueprintEditorUtils::IsEventGraph(EventGraph)) continue;
        TArray<UK2Node_CustomEvent*> Events;
        EventGraph->GetNodesOfClass(Events);
        Events.Sort([](const UK2Node_CustomEvent& Left, const UK2Node_CustomEvent& Right)
        {
            return GuidString(Left.NodeGuid) < GuidString(Right.NodeGuid);
        });
        for (UK2Node_CustomEvent* Event : Events)
        {
            if (Event == nullptr) continue;
            const FString EventId = GuidString(Event->NodeGuid);
            if (!CustomEventFilter.IsEmpty() && EventId != CustomEventFilter) continue;
            bCustomEventFound = true;
            const bool bLocalOwner = Owner.Key == Blueprint;
            const bool bOverride = Event->IsOverride();
            const bool bEditable = bLocalOwner && !bOverride && Event->IsEditable() && EventId.Len() == 32;
            const TSharedRef<FJsonObject> Signature = CustomEventSignature(Event);
            const TSharedRef<FJsonObject> References = CustomEventReferences(Blueprint, Event);
            if (Sections.Contains(TEXT("custom_events")))
            {
                const TSharedRef<FJsonObject> Value = Record(TEXT("custom_event"));
                Value->SetStringField(TEXT("id"), EventId);
                Value->SetBoolField(TEXT("identity_stable"), !EventId.IsEmpty());
                Value->SetStringField(TEXT("name"), Event->CustomFunctionName.ToString());
                Value->SetStringField(TEXT("owner_blueprint"), Owner.Value);
                Value->SetBoolField(TEXT("inherited"), !bLocalOwner);
                Value->SetStringField(TEXT("ownership"), !bLocalOwner ? TEXT("inherited")
                    : bOverride ? TEXT("custom_event_override") : TEXT("local"));
                Value->SetBoolField(TEXT("editable"), bEditable);
                Value->SetObjectField(TEXT("signature"), Signature);
                Value->SetObjectField(TEXT("metadata"), CallableMetadata(Event->GetUserDefinedMetaData(), Event->bCallInEditor));
                Value->SetObjectField(TEXT("reference_summary"), References);
                const TSharedRef<FJsonObject> Relationship = MakeShared<FJsonObject>();
                Relationship->SetStringField(TEXT("graph_id"), GuidString(EventGraph->GraphGuid));
                Relationship->SetStringField(TEXT("graph_kind"), TEXT("event"));
                Value->SetObjectField(TEXT("graph_relationship"), Relationship);
                const TSharedRef<FJsonObject> Required = MakeShared<FJsonObject>();
                Required->SetStringField(TEXT("event_node_id"), EventId);
                Required->SetBoolField(TEXT("event_node_present"), true);
                Required->SetBoolField(TEXT("valid"), FBlueprintEditorUtils::IsEventGraph(EventGraph));
                Value->SetObjectField(TEXT("required_nodes"), Required);
                AddRecord(Sink.Records, Value);
            }
            int32 ParameterIndex = 0;
            for (const TSharedPtr<FUserPinInfo>& Pin : Event->UserDefinedPins)
            {
                if (!Pin.IsValid()) continue;
                UEdGraphPin* LivePin = Event->FindPin(Pin->PinName);
                if (Sections.Contains(TEXT("parameters")) && FunctionFilter.IsEmpty()
                    && LocalFilter.IsEmpty() && MacroFilter.IsEmpty())
                {
                    const TSharedRef<FJsonObject> Value = Record(TEXT("parameter"));
                    Value->SetStringField(TEXT("id"), LivePin != nullptr ? GuidString(LivePin->PinId) : FString());
                    Value->SetBoolField(TEXT("identity_stable"), LivePin != nullptr && LivePin->PinId.IsValid());
                    Value->SetStringField(TEXT("owner_kind"), TEXT("custom_event"));
                    Value->SetStringField(TEXT("owner_id"), EventId);
                    Value->SetStringField(TEXT("custom_event_id"), EventId);
                    Value->SetStringField(TEXT("custom_event_name"), Event->CustomFunctionName.ToString());
                    Value->SetNumberField(TEXT("index"), ParameterIndex);
                    Value->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    Value->SetStringField(TEXT("direction"), TEXT("input"));
                    Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
                    if (!Pin->PinType.bIsReference)
                    {
                        Value->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, Pin->PinDefaultValue));
                    }
                    AddRecord(Sink.Records, Value);
                }
                Sink.Fingerprint.Add(TEXT("custom_event_parameter|") + EventId + TEXT("|") + LexToString(ParameterIndex)
                    + TEXT("|") + Pin->PinName.ToString() + TEXT("|") + VariableTypeFingerprint(Pin->PinType)
                    + TEXT("|") + Pin->PinDefaultValue);
                ++ParameterIndex;
            }
            const FKismetUserDeclaredFunctionMetadata& Metadata = Event->GetUserDefinedMetaData();
            Sink.Fingerprint.Add(TEXT("custom_event|") + Owner.Value + TEXT("|") + GuidString(EventGraph->GraphGuid) + TEXT("|")
                + EventId + TEXT("|") + Event->CustomFunctionName.ToString() + TEXT("|") + LexToString(bOverride)
                + TEXT("|") + Metadata.Category.ToString() + TEXT("|") + Metadata.ToolTip.ToString()
                + TEXT("|") + Metadata.Keywords.ToString() + TEXT("|") + LexToString(Event->bCallInEditor));
        }
    }
}
if (!bCustomEventFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested custom-event identity was not found")};
    return false;
}
    return true;
}

static bool CollectGraphs(
    UBlueprint* Blueprint,
    const TArray<TPair<UBlueprint*, FString>>& Owners,
    const TSet<FString>& Sections,
    const FString& GraphFilter,
    FInspectionSink& Sink,
    FUnrealMCPError& OutError)
{
TArray<TPair<UEdGraph*, FString>> Graphs;
for (const TPair<UBlueprint*, FString>& Owner : Owners) AddBlueprintGraphs(Owner.Key, Owner.Value, Graphs);
Graphs.Sort([](const TPair<UEdGraph*, FString>& Left, const TPair<UEdGraph*, FString>& Right)
{
    const FString A = Left.Value + TEXT("|") + GuidString(Left.Key->GraphGuid) + TEXT("|") + Left.Key->GetName();
    const FString B = Right.Value + TEXT("|") + GuidString(Right.Key->GraphGuid) + TEXT("|") + Right.Key->GetName();
    return A < B;
});
bool bGraphFound = GraphFilter.IsEmpty();
for (const TPair<UEdGraph*, FString>& Entry : Graphs)
{
    UEdGraph* Graph = Entry.Key;
    const FString GraphId = GuidString(Graph->GraphGuid);
    if (!GraphFilter.IsEmpty() && GraphId != GraphFilter) continue;
    bGraphFound = true;
    UBlueprint* OwnerBlueprint = Graph->GetTypedOuter<UBlueprint>();
    const FString Kind = OwnerBlueprint != nullptr ? GraphKind(OwnerBlueprint, Graph) : TEXT("other");
    if (Sections.Contains(TEXT("graphs")))
    {
        const TSharedRef<FJsonObject> Value = Record(TEXT("graph"));
        Value->SetStringField(TEXT("id"), GraphId);
        Value->SetBoolField(TEXT("identity_stable"), !GraphId.IsEmpty());
        Value->SetStringField(TEXT("name"), Graph->GetName());
        Value->SetStringField(TEXT("kind"), Kind);
        Value->SetStringField(TEXT("owner_blueprint"), Entry.Value);
        Value->SetBoolField(TEXT("inherited"), OwnerBlueprint != Blueprint);
        Value->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        AddRecord(Sink.Records, Value);
    }
    Sink.Fingerprint.Add(TEXT("graph|") + Entry.Value + TEXT("|") + GraphId + TEXT("|") + Graph->GetName() + TEXT("|") + Kind);
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr) continue;
        const FString NodeId = GuidString(Node->NodeGuid);
        if (Sections.Contains(TEXT("nodes")))
        {
            const TSharedRef<FJsonObject> Value = Record(TEXT("node"));
            Value->SetStringField(TEXT("graph_id"), GraphId);
            Value->SetStringField(TEXT("id"), NodeId);
            Value->SetBoolField(TEXT("identity_stable"), !NodeId.IsEmpty());
            Value->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
            Value->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
            Value->SetNumberField(TEXT("x"), Node->NodePosX);
            Value->SetNumberField(TEXT("y"), Node->NodePosY);
            AddRecord(Sink.Records, Value);
        }
        Sink.Fingerprint.Add(TEXT("node|") + GraphId + TEXT("|") + NodeId + TEXT("|") + Node->GetClass()->GetPathName()
            + FString::Printf(TEXT("|%d|%d"), Node->NodePosX, Node->NodePosY));
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr) continue;
            const FString PinId = GuidString(Pin->PinId);
            if (Sections.Contains(TEXT("pins")))
            {
                const TSharedRef<FJsonObject> Value = Record(TEXT("pin"));
                Value->SetStringField(TEXT("graph_id"), GraphId);
                Value->SetStringField(TEXT("node_id"), NodeId);
                Value->SetStringField(TEXT("id"), PinId);
                Value->SetBoolField(TEXT("identity_stable"), !PinId.IsEmpty());
                Value->SetStringField(TEXT("name"), Pin->PinName.ToString());
                Value->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                Value->SetObjectField(TEXT("type"), PinType(Pin->PinType));
                Value->SetStringField(TEXT("default_value"), Pin->DefaultValue.Left(512));
                if (Pin->DefaultObject != nullptr) Value->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
                if (!Pin->DefaultTextValue.IsEmpty()) Value->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString().Left(512));
                const FString TypedDefault = Pin->DefaultObject != nullptr ? Pin->DefaultObject->GetPathName()
                    : Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text ? Pin->DefaultTextValue.ToString() : Pin->DefaultValue;
                Value->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, TypedDefault));
                AddRecord(Sink.Records, Value);
            }
            const FString DefaultObjectPath = Pin->DefaultObject != nullptr ? Pin->DefaultObject->GetPathName() : FString();
            Sink.Fingerprint.Add(TEXT("pin|") + GraphId + TEXT("|") + NodeId + TEXT("|") + PinId + TEXT("|")
                + Pin->PinName.ToString() + TEXT("|") + Pin->PinType.PinCategory.ToString() + TEXT("|") + Pin->DefaultValue
                + TEXT("|") + DefaultObjectPath + TEXT("|") + Pin->DefaultTextValue.ToString());
            if (Pin->Direction == EGPD_Output)
            {
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    if (Linked == nullptr || Linked->GetOwningNodeUnchecked() == nullptr) continue;
                    if (Sections.Contains(TEXT("connections")))
                    {
                        const TSharedRef<FJsonObject> Value = Record(TEXT("connection"));
                        Value->SetStringField(TEXT("graph_id"), GraphId);
                        Value->SetStringField(TEXT("from_node_id"), NodeId);
                        Value->SetStringField(TEXT("from_pin_id"), PinId);
                        Value->SetStringField(TEXT("to_node_id"), GuidString(Linked->GetOwningNodeUnchecked()->NodeGuid));
                        Value->SetStringField(TEXT("to_pin_id"), GuidString(Linked->PinId));
                        AddRecord(Sink.Records, Value);
                    }
                    Sink.Fingerprint.Add(TEXT("link|") + PinId + TEXT("|") + GuidString(Linked->PinId));
                }
            }
        }
    }
}
if (!bGraphFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested graph identity was not found")};
    return false;
}
    return true;
}
}
