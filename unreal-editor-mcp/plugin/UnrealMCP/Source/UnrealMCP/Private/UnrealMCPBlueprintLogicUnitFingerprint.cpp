#include "UnrealMCPBlueprintLogicUnitFingerprint.h"

#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPVersion.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/SecureHash.h"

namespace UnrealMCP::BlueprintLogicUnitFingerprint
{
using UnrealMCP::BlueprintInspectionPrivate::IsStructuralGraphPin;
using UnrealMCP::BlueprintInspectionPrivate::VariableTypeFingerprint;

namespace
{
FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

FString HashLines(TArray<FString> Lines)
{
    Lines.Sort();
    const FString Joined = FString::Join(Lines, TEXT("\n"));
    FTCHARToUTF8 Encoded(*Joined);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

FString PinDefault(const UEdGraphPin* Pin)
{
    if (Pin == nullptr) return FString();
    return Pin->DefaultValue + TEXT("|")
        + (Pin->DefaultObject != nullptr ? Pin->DefaultObject->GetPathName() : FString()) + TEXT("|")
        + Pin->DefaultTextValue.ToString();
}

bool IsExecPin(const UEdGraphNode* Node, const UEdGraphPin* Pin)
{
    return IsStructuralGraphPin(Node, Pin)
        && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

bool IsPureDependency(const UEdGraphNode* Node)
{
    if (Node == nullptr || Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_Tunnel>()) return false;
    for (const UEdGraphPin* Pin : Node->Pins)
        if (IsExecPin(Node, Pin)) return false;
    return true;
}

void AddExecDescendants(UEdGraphNode* Start, const TSet<UEdGraphNode*>& Candidates, TSet<UEdGraphNode*>& Out)
{
    TArray<UEdGraphNode*> Pending{Start};
    while (!Pending.IsEmpty())
    {
        UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
        if (Node == nullptr || Out.Contains(Node) || !Candidates.Contains(Node)) continue;
        Out.Add(Node);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsExecPin(Node, Pin) || Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Next = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (Candidates.Contains(Next)) Pending.Add(Next);
            }
        }
    }
}

bool CollectExternalLinks(const TSet<UEdGraphNode*>& UnitNodes, TArray<FExternalLink>& Out)
{
    TSet<FString> Unique;
    for (UEdGraphNode* Node : UnitNodes)
    {
        if (Node == nullptr || !Node->NodeGuid.IsValid()) return false;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin) || Pin->Direction != EGPD_Output || !Pin->PinId.IsValid()) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Other = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (Other == nullptr || UnitNodes.Contains(Other)) continue;
                if (!Other->NodeGuid.IsValid() || !Linked->PinId.IsValid()) return false;
                FExternalLink Link{GuidString(Node->NodeGuid), GuidString(Pin->PinId),
                    GuidString(Other->NodeGuid), GuidString(Linked->PinId)};
                const FString Identity = Link.FromNodeId + Link.FromPinId + Link.ToNodeId + Link.ToPinId;
                if (!Unique.Contains(Identity))
                {
                    Unique.Add(Identity);
                    Out.Add(MoveTemp(Link));
                }
            }
        }
    }
    // Incoming links are owned by an external output pin and therefore need a second pass.
    if (!UnitNodes.IsEmpty())
    {
        UEdGraph* Graph = nullptr;
        for (UEdGraphNode* Node : UnitNodes)
        {
            Graph = Node != nullptr ? Node->GetGraph() : nullptr;
            break;
        }
        if (Graph != nullptr) for (UEdGraphNode* External : Graph->Nodes)
        {
            if (External == nullptr || UnitNodes.Contains(External)) continue;
            for (UEdGraphPin* Pin : External->Pins)
            {
                if (!IsStructuralGraphPin(External, Pin) || Pin->Direction != EGPD_Output || !Pin->PinId.IsValid()) continue;
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    UEdGraphNode* Other = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                    if (Other == nullptr || !UnitNodes.Contains(Other)) continue;
                    if (!External->NodeGuid.IsValid() || !Linked->PinId.IsValid()) return false;
                    FExternalLink Link{GuidString(External->NodeGuid), GuidString(Pin->PinId),
                        GuidString(Other->NodeGuid), GuidString(Linked->PinId)};
                    const FString Identity = Link.FromNodeId + Link.FromPinId + Link.ToNodeId + Link.ToPinId;
                    if (!Unique.Contains(Identity))
                    {
                        Unique.Add(Identity);
                        Out.Add(MoveTemp(Link));
                    }
                }
            }
        }
    }
    Out.Sort([](const FExternalLink& A, const FExternalLink& B)
    {
        return A.FromNodeId + A.FromPinId + A.ToNodeId + A.ToPinId
            < B.FromNodeId + B.FromPinId + B.ToNodeId + B.ToPinId;
    });
    return Out.Num() <= UnrealMCP::MaxLogicUnitExternalConnections;
}

bool Finalize(FBoundary& Out)
{
    if (Out.Graph == nullptr || Out.Entry == nullptr || !Out.Entry->NodeGuid.IsValid()) return false;
    TSet<UEdGraphNode*> UnitNodes{Out.Entry};
    if (Out.Result != nullptr) UnitNodes.Add(Out.Result);
    for (UEdGraphNode* Node : Out.OwnedNodes)
    {
        if (Node == nullptr || Node->GetGraph() != Out.Graph || !Node->NodeGuid.IsValid()) return false;
        UnitNodes.Add(Node);
        Out.OwnedNodeIds.Add(GuidString(Node->NodeGuid));
    }
    if (Out.OwnedNodeIds.Num() > UnrealMCP::MaxLogicUnitOwnedNodes
        || !CollectExternalLinks(UnitNodes, Out.ExternalLinks)) return false;
    Out.OwnedNodeIds.Sort();
    Out.LocalVariableIds.Sort();
    Out.Fingerprint = Build(Out);
    return Out.Fingerprint.Len() == 40;
}
}

FString KindString(EKind Kind)
{
    switch (Kind)
    {
    case EKind::Function: return TEXT("function");
    case EKind::Macro: return TEXT("macro");
    case EKind::CustomEvent: return TEXT("custom_event");
    case EKind::Event: return TEXT("event");
    }
    return FString();
}

FString Build(const FBoundary& Boundary)
{
    if (Boundary.Graph == nullptr || Boundary.Entry == nullptr) return FString();
    TSet<UEdGraphNode*> UnitNodes{Boundary.Entry};
    if (Boundary.Result != nullptr) UnitNodes.Add(Boundary.Result);
    for (UEdGraphNode* Node : Boundary.OwnedNodes) UnitNodes.Add(Node);
    TArray<FString> Lines;
    Lines.Add(TEXT("unit|") + KindString(Boundary.Kind) + TEXT("|")
        + GuidString(Boundary.Graph->GraphGuid) + TEXT("|") + Boundary.Graph->GetName() + TEXT("|")
        + GuidString(Boundary.Entry->NodeGuid) + TEXT("|")
        + (Boundary.Result != nullptr ? GuidString(Boundary.Result->NodeGuid) : FString()));
    if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Boundary.Entry))
    {
        Lines.Add(TEXT("function_entry|") + LexToString(Entry->GetFunctionFlags()) + TEXT("|")
            + Entry->MetaData.Category.ToString() + TEXT("|") + Entry->MetaData.ToolTip.ToString() + TEXT("|")
            + Entry->MetaData.Keywords.ToString() + TEXT("|") + LexToString(Entry->MetaData.bCallInEditor));
        for (const FBPVariableDescription& Local : Entry->LocalVariables)
            Lines.Add(TEXT("local|") + GuidString(Local.VarGuid) + TEXT("|") + Local.VarName.ToString()
                + TEXT("|") + VariableTypeFingerprint(Local.VarType) + TEXT("|") + Local.DefaultValue);
    }
    if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Boundary.Entry))
        Lines.Add(TEXT("custom_event|") + Event->CustomFunctionName.ToString() + TEXT("|")
            + LexToString(Event->FunctionFlags));
    for (UEdGraphNode* Node : UnitNodes)
    {
        const FString NodeId = GuidString(Node->NodeGuid);
        Lines.Add(TEXT("node|") + NodeId + TEXT("|") + Node->GetClass()->GetPathName()
            + FString::Printf(TEXT("|%d|%d"), Node->NodePosX, Node->NodePosY));
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin)) continue;
            const FString PinId = GuidString(Pin->PinId);
            Lines.Add(TEXT("pin|") + NodeId + TEXT("|") + PinId + TEXT("|")
                + Pin->PinName.ToString() + TEXT("|") + LexToString(static_cast<int32>(Pin->Direction))
                + TEXT("|") + VariableTypeFingerprint(Pin->PinType) + TEXT("|") + PinDefault(Pin)
                + TEXT("|") + LexToString(Pin->bHidden) + TEXT("|") + LexToString(Pin->bAdvancedView));
            if (Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (LinkedNode != nullptr && UnitNodes.Contains(LinkedNode))
                    Lines.Add(TEXT("link|") + NodeId + TEXT("|") + PinId + TEXT("|")
                        + GuidString(LinkedNode->NodeGuid) + TEXT("|") + GuidString(Linked->PinId));
            }
        }
    }
    for (const FExternalLink& Link : Boundary.ExternalLinks)
        Lines.Add(TEXT("external|") + Link.FromNodeId + TEXT("|") + Link.FromPinId + TEXT("|")
            + Link.ToNodeId + TEXT("|") + Link.ToPinId);
    return HashLines(MoveTemp(Lines));
}

bool DescribeFunction(UEdGraph* Graph, FBoundary& Out)
{
    Out = FBoundary();
    Out.Kind = EKind::Function;
    Out.Graph = Graph;
    if (Graph == nullptr) return false;
    Out.Entry = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph));
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Results);
    if (Out.Entry == nullptr || Results.Num() != 1 || Results[0] == nullptr) return false;
    Out.Result = Results[0];
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node != nullptr && Node != Out.Entry && Node != Out.Result) Out.OwnedNodes.Add(Node);
    for (const FBPVariableDescription& Local : CastChecked<UK2Node_FunctionEntry>(Out.Entry)->LocalVariables)
    {
        if (!Local.VarGuid.IsValid()) return false;
        Out.LocalVariableIds.Add(GuidString(Local.VarGuid));
    }
    return Finalize(Out) && Out.ExternalLinks.IsEmpty();
}

bool DescribeMacro(UEdGraph* Graph, FBoundary& Out)
{
    Out = FBoundary();
    Out.Kind = EKind::Macro;
    Out.Graph = Graph;
    UK2Node_Tunnel* Entry = nullptr;
    UK2Node_Tunnel* Exit = nullptr;
    bool bPure = false;
    if (Graph == nullptr) return false;
    FKismetEditorUtilities::GetInformationOnMacro(Graph, Entry, Exit, bPure);
    if (Entry == nullptr || Exit == nullptr || Entry == Exit) return false;
    Out.Entry = Entry;
    Out.Result = Exit;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node != nullptr && Node != Entry && Node != Exit) Out.OwnedNodes.Add(Node);
    return Finalize(Out) && Out.ExternalLinks.IsEmpty();
}

bool DescribeEventHandler(UEdGraph* Graph, UEdGraphNode* EventRoot, FBoundary& Out)
{
    Out = FBoundary();
    Out.Graph = Graph;
    Out.Entry = EventRoot;
    const UK2Node_Event* Event = Cast<UK2Node_Event>(EventRoot);
    if (Graph == nullptr || Event == nullptr || EventRoot->GetGraph() != Graph
        || !FBlueprintEditorUtils::IsEventGraph(Graph)) return false;
    Out.Kind = EventRoot->IsA<UK2Node_CustomEvent>() ? EKind::CustomEvent : EKind::Event;

    TSet<UEdGraphNode*> Reachable{EventRoot};
    TArray<UEdGraphNode*> Pending{EventRoot};
    while (!Pending.IsEmpty() && Reachable.Num() <= UnrealMCP::MaxLogicUnitOwnedNodes + 1)
    {
        UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsExecPin(Node, Pin) || Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Next = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (Next == nullptr || Next->IsA<UK2Node_Event>() || Reachable.Contains(Next)) continue;
                Reachable.Add(Next);
                Pending.Add(Next);
            }
        }
    }
    if (Reachable.Num() > UnrealMCP::MaxLogicUnitOwnedNodes + 1) return false;

    TSet<UEdGraphNode*> Cut;
    for (UEdGraphNode* Node : Reachable)
    {
        if (Node == EventRoot) continue;
        bool bSharedControl = false;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsExecPin(Node, Pin) || Pin->Direction != EGPD_Input) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Source = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (Source != nullptr && !Reachable.Contains(Source)) bSharedControl = true;
            }
        }
        if (bSharedControl) AddExecDescendants(Node, Reachable, Cut);
    }
    TSet<UEdGraphNode*> UnitNodes{EventRoot};
    for (UEdGraphNode* Node : Reachable)
        if (Node != EventRoot && !Cut.Contains(Node)) UnitNodes.Add(Node);

    TSet<UEdGraphNode*> PureCandidates;
    Pending = UnitNodes.Array();
    while (!Pending.IsEmpty() && PureCandidates.Num() <= UnrealMCP::MaxLogicUnitOwnedNodes)
    {
        UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin) || IsExecPin(Node, Pin) || Pin->Direction != EGPD_Input) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Source = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (Source != nullptr && !UnitNodes.Contains(Source) && !PureCandidates.Contains(Source)
                    && IsPureDependency(Source))
                {
                    PureCandidates.Add(Source);
                    Pending.Add(Source);
                }
            }
        }
    }
    if (PureCandidates.Num() > UnrealMCP::MaxLogicUnitOwnedNodes) return false;
    bool bChanged = true;
    while (bChanged)
    {
        bChanged = false;
        for (UEdGraphNode* Candidate : PureCandidates.Array())
        {
            bool bShared = false;
            for (UEdGraphPin* Pin : Candidate->Pins)
            {
                if (!IsStructuralGraphPin(Candidate, Pin) || Pin->Direction != EGPD_Output) continue;
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    UEdGraphNode* Consumer = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                    if (Consumer != nullptr && !UnitNodes.Contains(Consumer) && !PureCandidates.Contains(Consumer))
                        bShared = true;
                }
            }
            if (bShared)
            {
                PureCandidates.Remove(Candidate);
                bChanged = true;
            }
        }
    }
    for (UEdGraphNode* Pure : PureCandidates) UnitNodes.Add(Pure);
    for (UEdGraphNode* Node : UnitNodes)
        if (Node != EventRoot) Out.OwnedNodes.Add(Node);
    return Finalize(Out);
}
}
