#include "UnrealMCPBlueprintBlockReplacementService.h"

#include "UnrealMCPBlueprintBlockReplacementRequest.h"
#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintLogicUnitFingerprint.h"
#include "UnrealMCPBlueprintNodeLayout.h"
#include "UnrealMCPBlueprintGraphResultBuilder.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPVersion.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealMCP::BlueprintBlockReplacementPrivate
{
using namespace UnrealMCP::BlueprintBlockReplacement;
using namespace UnrealMCP::BlueprintMutationPrivate;
namespace LogicUnit = UnrealMCP::BlueprintLogicUnitFingerprint;
using UnrealMCP::BlueprintInspectionPrivate::IsStructuralGraphPin;
using UnrealMCP::BlueprintInspectionPrivate::StructuralGraphPinCount;
using UnrealMCP::BlueprintInspectionPrivate::VariableTypeFingerprint;

struct FAppliedPlan
{
    TMap<FString, UEdGraphNode*> NodesByKey;
    TArray<UEdGraphNode*> CreatedNodes;
    TSet<FString> ExternalLinks;
    FString SemanticFingerprint;
    UnrealMCP::BlueprintNodeLayout::FResult Layout;
    int32 ConversionNodeCount = 0;
};

struct FScratchPackage
{
    UPackage* Package = nullptr;
    UBlueprint* Blueprint = nullptr;

    ~FScratchPackage()
    {
        CleanupFailedCreation(Package, Blueprint, FString(), false);
    }
};

FString HashLines(TArray<FString> Lines)
{
    Lines.Sort();
    const FString Joined = FString::Join(Lines, TEXT("\n"));
    FTCHARToUTF8 Encoded(*Joined);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

bool SameStrings(const TArray<FString>& Left, const TArray<FString>& Right)
{
    return Left.Num() == Right.Num() && Left == Right;
}

UEdGraph* FindGraph(UBlueprint* Blueprint, const FRequest& Request)
{
    if (Blueprint == nullptr) return nullptr;
    const TArray<TObjectPtr<UEdGraph>>* Graphs = nullptr;
    switch (Request.TargetKind)
    {
    case ETargetKind::Function: Graphs = &Blueprint->FunctionGraphs; break;
    case ETargetKind::Macro: Graphs = &Blueprint->MacroGraphs; break;
    case ETargetKind::CustomEvent:
    case ETargetKind::Event: Graphs = &Blueprint->UbergraphPages; break;
    }
    if (Graphs != nullptr)
        for (UEdGraph* Graph : *Graphs)
            if (Graph != nullptr && GuidString(Graph->GraphGuid) == Request.GraphId) return Graph;
    return nullptr;
}

UEdGraphNode* FindNode(UEdGraph* Graph, const FString& NodeId)
{
    if (Graph == nullptr) return nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node != nullptr && GuidString(Node->NodeGuid) == NodeId) return Node;
    return nullptr;
}

bool DescribeBoundary(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FRequest& Request,
    LogicUnit::FBoundary& Out)
{
    if (Blueprint == nullptr || Graph == nullptr) return false;
    switch (Request.TargetKind)
    {
    case ETargetKind::Function:
        return IsUserOwnedFunction(Blueprint, Graph) && Request.LogicUnitId == Request.GraphId
            && LogicUnit::DescribeFunction(Graph, Out);
    case ETargetKind::Macro:
        return Blueprint->MacroGraphs.Contains(Graph) && Request.LogicUnitId == Request.GraphId
            && LogicUnit::DescribeMacro(Graph, Out);
    case ETargetKind::CustomEvent:
    {
        UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(FindNode(Graph, Request.EntryNodeId));
        return Event != nullptr && Request.LogicUnitId == Request.EntryNodeId
            && LogicUnit::DescribeEventHandler(Graph, Event, Out);
    }
    case ETargetKind::Event:
    {
        UEdGraphNode* Event = FindNode(Graph, Request.EntryNodeId);
        return Event != nullptr && Event->IsA<UK2Node_Event>() && !Event->IsA<UK2Node_CustomEvent>()
            && Request.LogicUnitId == Request.EntryNodeId
            && LogicUnit::DescribeEventHandler(Graph, Event, Out);
    }
    }
    return false;
}

bool RebindScratchHandlerRoot(
    UEdGraph* ScratchGraph,
    const FRequest& Request,
    const LogicUnit::FBoundary& LiveBoundary)
{
    if (Request.TargetKind != ETargetKind::CustomEvent && Request.TargetKind != ETargetKind::Event)
        return true;
    if (FindNode(ScratchGraph, Request.EntryNodeId) != nullptr) return true;
    if (ScratchGraph == nullptr) return false;
    UEdGraphNode* Match = nullptr;
    for (UEdGraphNode* Candidate : ScratchGraph->Nodes)
    {
        if (Candidate == nullptr || LiveBoundary.Entry == nullptr
            || Candidate->GetClass() != LiveBoundary.Entry->GetClass()) continue;
        bool bSame = Candidate->GetFName() == LiveBoundary.Entry->GetFName();
        if (const UK2Node_CustomEvent* LiveEvent = Cast<UK2Node_CustomEvent>(LiveBoundary.Entry))
        {
            const UK2Node_CustomEvent* ScratchEvent = Cast<UK2Node_CustomEvent>(Candidate);
            bSame = ScratchEvent != nullptr
                && ScratchEvent->CustomFunctionName == LiveEvent->CustomFunctionName;
        }
        else if (!bSame)
            bSame = Candidate->GetNodeTitle(ENodeTitleType::ListView).EqualTo(
                LiveBoundary.Entry->GetNodeTitle(ENodeTitleType::ListView));
        if (!bSame) continue;
        if (Match != nullptr) return false;
        Match = Candidate;
    }
    FGuid ExpectedGuid;
    if (Match == nullptr || !FGuid::ParseExact(
        Request.EntryNodeId, EGuidFormats::Digits, ExpectedGuid)) return false;
    Match->NodeGuid = ExpectedGuid;
    return true;
}

bool RebindScratchExternalEndpoints(
    UEdGraph* LiveGraph,
    UEdGraph* ScratchGraph,
    const FRequest& Request)
{
    if (Request.ExternalConnections.IsEmpty()) return true;
    if (LiveGraph == nullptr || ScratchGraph == nullptr) return false;
    for (const FExternalConnectionPlan& Plan : Request.ExternalConnections)
    {
        UEdGraphNode* LiveNode = FindNode(LiveGraph, Plan.External.NodeId);
        if (LiveNode == nullptr) return false;
        UEdGraphNode* ScratchNode = FindNode(ScratchGraph, Plan.External.NodeId);
        if (ScratchNode == nullptr)
        {
            for (UEdGraphNode* Candidate : ScratchGraph->Nodes)
            {
                if (Candidate == nullptr || Candidate->GetClass() != LiveNode->GetClass()
                    || Candidate->GetFName() != LiveNode->GetFName()) continue;
                if (ScratchNode != nullptr) return false;
                ScratchNode = Candidate;
            }
            FGuid NodeGuid;
            if (ScratchNode == nullptr || !FGuid::ParseExact(
                Plan.External.NodeId, EGuidFormats::Digits, NodeGuid)) return false;
            ScratchNode->NodeGuid = NodeGuid;
        }
        UEdGraphPin* LivePin = nullptr;
        for (UEdGraphPin* Pin : LiveNode->Pins)
            if (Pin != nullptr && GuidString(Pin->PinId) == Plan.External.PinId) LivePin = Pin;
        if (LivePin == nullptr) return false;
        UEdGraphPin* ScratchPin = nullptr;
        for (UEdGraphPin* Pin : ScratchNode->Pins)
        {
            if (Pin == nullptr) continue;
            if (GuidString(Pin->PinId) == Plan.External.PinId)
            {
                ScratchPin = Pin;
                break;
            }
            if (Pin->PinName == LivePin->PinName && Pin->Direction == LivePin->Direction)
            {
                if (ScratchPin != nullptr) return false;
                ScratchPin = Pin;
            }
        }
        FGuid PinGuid;
        if (ScratchPin == nullptr || !FGuid::ParseExact(
            Plan.External.PinId, EGuidFormats::Digits, PinGuid)) return false;
        ScratchPin->PinId = PinGuid;
    }
    return true;
}

UEdGraphPin* FindUniquePin(
    UEdGraphNode* Node,
    const FString& PinName,
    EEdGraphPinDirection Direction,
    FUnrealMCPError& OutError)
{
    UEdGraphPin* Match = nullptr;
    if (Node != nullptr)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin) || Pin->PinName.ToString() != PinName
                || Pin->Direction != Direction) continue;
            if (Match != nullptr)
            {
                OutError = {TEXT("invalid_pin"),
                    TEXT("A semantic replacement pin name is ambiguous on its node")};
                return nullptr;
            }
            Match = Pin;
        }
    }
    if (Match == nullptr)
        OutError = {TEXT("invalid_pin"),
            TEXT("A semantic replacement pin name is unavailable in the required direction")};
    return Match;
}

bool IsStableNode(UEdGraph* Graph, UEdGraphNode* Node)
{
    if (Graph == nullptr || Node == nullptr || Node->GetGraph() != Graph || Node->GetOuter() != Graph
        || Node->IsIntermediateNode() || !Node->NodeGuid.IsValid()
        || StructuralGraphPinCount(Node) > UnrealMCP::MaxGraphPinsPerNode) return false;
    for (UEdGraphPin* Pin : Node->Pins)
        if (IsStructuralGraphPin(Node, Pin)
            && (Pin == nullptr || !Pin->PinId.IsValid()
                || Pin->LinkedTo.Num() > UnrealMCP::MaxGraphLinksPerPin)) return false;
    return true;
}

FString PinDefaultText(const UEdGraphPin* Pin)
{
    if (Pin == nullptr) return FString();
    if (Pin->DefaultObject != nullptr) return Pin->DefaultObject->GetPathName();
    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
        return Pin->DefaultTextValue.ToString();
    return Pin->DefaultValue;
}

bool SetDefault(
    const UEdGraphSchema_K2* Schema,
    UEdGraphNode* Node,
    UEdGraphPin* Pin,
    const TSharedPtr<FJsonObject>& Value,
    FUnrealMCPError& OutError)
{
    const TSharedRef<FJsonObject> EncodedType = UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType);
    if (Schema == nullptr || Node == nullptr || Pin == nullptr || !Pin->PinId.IsValid()
        || Pin->bWasTrashed || Pin->bOrphanedPin || Pin->bNotConnectable
        || Pin->HasAnyConnections() || Pin->bDefaultValueIsIgnored || Pin->bDefaultValueIsReadOnly
        || Schema->ShouldHidePinDefaultValue(Pin) || !EncodedType->GetBoolField(TEXT("supported")))
    {
        OutError = {TEXT("protected_pin"),
            TEXT("A replacement default targets a protected, linked, or unsupported pin")};
        return false;
    }
    FString Kind;
    FString Canonical;
    if (!Value.IsValid() || !Value->TryGetStringField(TEXT("kind"), Kind)
        || !UnrealMCP::K2TypeCodec::DecodeDefault(Pin->PinType, Value, Canonical, OutError))
        return false;
    const bool bEngineDefault = Kind == TEXT("engine_default");
    if (bEngineDefault) Canonical = Pin->AutogeneratedDefaultValue;
    if (Canonical.Len() > UnrealMCP::MaxPinDefaultChars)
    {
        OutError = {TEXT("pin_default_too_large"),
            TEXT("A canonical replacement default exceeds the published character limit")};
        return false;
    }
    FString ParsedValue;
    TObjectPtr<UObject> ParsedObject = nullptr;
    FText ParsedText;
    Schema->GetPinDefaultValuesFromString(
        Pin->PinType, Node, Canonical, ParsedValue, ParsedObject, ParsedText, false);
    const FString Validation =
        Schema->IsPinDefaultValid(Pin, ParsedValue, ParsedObject, ParsedText);
    if (!Validation.IsEmpty())
    {
        OutError = {TEXT("invalid_pin_default"), Validation.Left(UnrealMCP::MaxDiagnosticChars)};
        return false;
    }
    if (bEngineDefault) Schema->ResetPinToAutogeneratedDefaultValue(Pin, true);
    else Schema->TrySetDefaultValue(*Pin, Canonical, true);
    return bEngineDefault ? Schema->DoesDefaultValueMatchAutogenerated(*Pin)
        : Pin->DefaultValue == ParsedValue && Pin->DefaultObject == ParsedObject
            && Pin->DefaultTextValue.EqualTo(ParsedText);
}

bool HasPathThrough(
    UEdGraphPin* FromPin,
    UEdGraphPin* ToPin,
    const TSet<UEdGraphNode*>& AllowedIntermediates)
{
    TArray<UEdGraphPin*> Pending{FromPin};
    TSet<UEdGraphPin*> Visited;
    while (!Pending.IsEmpty() && Visited.Num() <= UnrealMCP::MaxGraphLinksPerPin * 2)
    {
        UEdGraphPin* Output = Pending.Pop(EAllowShrinking::No);
        if (Output == nullptr || Visited.Contains(Output)) continue;
        Visited.Add(Output);
        for (UEdGraphPin* Linked : Output->LinkedTo)
        {
            if (Linked == nullptr || !Linked->LinkedTo.Contains(Output)) continue;
            if (Linked == ToPin) return true;
            UEdGraphNode* Intermediate = Linked->GetOwningNodeUnchecked();
            if (!AllowedIntermediates.Contains(Intermediate)) continue;
            for (UEdGraphPin* Candidate : Intermediate->Pins)
                if (Candidate != nullptr && Candidate->Direction == EGPD_Output) Pending.Add(Candidate);
        }
    }
    return false;
}

UEdGraphPin* FindPinById(UEdGraphNode* Node, const FString& PinId)
{
    if (Node == nullptr) return nullptr;
    for (UEdGraphPin* Pin : Node->Pins)
        if (IsStructuralGraphPin(Node, Pin) && GuidString(Pin->PinId) == PinId) return Pin;
    return nullptr;
}

bool BuildSemanticFingerprint(
    UEdGraph* Graph,
    const TMap<FString, UEdGraphNode*>& NodesByKey,
    const TSet<FString>& ExpectedExternalLinks,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    TMap<UEdGraphNode*, FString> KeysByNode;
    for (const TPair<FString, UEdGraphNode*>& Pair : NodesByKey)
    {
        if (!IsStableNode(Graph, Pair.Value) || KeysByNode.Contains(Pair.Value))
        {
            OutError = {TEXT("internal_error"),
                TEXT("The replacement plan contains an unstable or duplicate live node")};
            return false;
        }
        KeysByNode.Add(Pair.Value, Pair.Key);
    }
    TArray<FString> Lines;
    TSet<FString> ActualExternalLinks;
    for (const TPair<FString, UEdGraphNode*>& Pair : NodesByKey)
    {
        UEdGraphNode* Node = Pair.Value;
        Lines.Add(TEXT("node|") + Pair.Key + TEXT("|") + Node->GetClass()->GetPathName()
            + FString::Printf(TEXT("|%d|%d"), Node->NodePosX, Node->NodePosY));
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin)) continue;
            Lines.Add(TEXT("pin|") + Pair.Key + TEXT("|") + Pin->PinName.ToString() + TEXT("|")
                + LexToString(static_cast<int32>(Pin->Direction)) + TEXT("|")
                + VariableTypeFingerprint(Pin->PinType) + TEXT("|") + PinDefaultText(Pin));
            if (Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                const FString* LinkedKey = KeysByNode.Find(LinkedNode);
                if (LinkedKey == nullptr)
                {
                    if (LinkedNode == nullptr || !LinkedNode->NodeGuid.IsValid() || !Linked->PinId.IsValid())
                    {
                        OutError = {TEXT("internal_error"), TEXT("The replacement produced an unstable external link")};
                        return false;
                    }
                    const FString Identity = TEXT("out|") + Pair.Key + TEXT("|") + Pin->PinName.ToString()
                        + TEXT("|") + GuidString(LinkedNode->NodeGuid) + TEXT("|") + GuidString(Linked->PinId);
                    ActualExternalLinks.Add(Identity);
                    Lines.Add(TEXT("external|") + Identity);
                    continue;
                }
                Lines.Add(TEXT("link|") + Pair.Key + TEXT("|") + Pin->PinName.ToString()
                    + TEXT("|") + *LinkedKey + TEXT("|") + Linked->PinName.ToString());
            }
        }
    }
    for (UEdGraphNode* External : Graph->Nodes)
    {
        if (External == nullptr || KeysByNode.Contains(External)) continue;
        for (UEdGraphPin* Pin : External->Pins)
        {
            if (!IsStructuralGraphPin(External, Pin) || Pin->Direction != EGPD_Output
                || !External->NodeGuid.IsValid() || !Pin->PinId.IsValid()) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                const FString* LinkedKey = KeysByNode.Find(LinkedNode);
                if (LinkedKey == nullptr) continue;
                const FString Identity = TEXT("in|") + GuidString(External->NodeGuid) + TEXT("|")
                    + GuidString(Pin->PinId) + TEXT("|") + *LinkedKey + TEXT("|") + Linked->PinName.ToString();
                ActualExternalLinks.Add(Identity);
                Lines.Add(TEXT("external|") + Identity);
            }
        }
    }
    bool bExternalLinksMatch = ActualExternalLinks.Num() == ExpectedExternalLinks.Num();
    if (bExternalLinksMatch)
        for (const FString& Expected : ExpectedExternalLinks)
            if (!ActualExternalLinks.Contains(Expected))
            {
                bExternalLinksMatch = false;
                break;
            }
    if (!bExternalLinksMatch)
    {
        OutError = {TEXT("internal_error"),
            TEXT("The replacement produced links outside its declared logic-unit boundary")};
        return false;
    }
    OutFingerprint = HashLines(MoveTemp(Lines));
    return true;
}

bool BuildUntouchedGraphFingerprint(
    UEdGraph* Graph,
    const TSet<UEdGraphNode*>& ChangedNodes,
    FString& OutFingerprint)
{
    if (Graph == nullptr) return false;
    TArray<FString> Lines{TEXT("graph|") + GuidString(Graph->GraphGuid) + TEXT("|") + Graph->GetName()};
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr || ChangedNodes.Contains(Node)) continue;
        if (!Node->NodeGuid.IsValid()) return false;
        const FString NodeId = GuidString(Node->NodeGuid);
        Lines.Add(TEXT("node|") + NodeId + TEXT("|") + Node->GetClass()->GetPathName()
            + FString::Printf(TEXT("|%d|%d"), Node->NodePosX, Node->NodePosY));
        if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
            Lines.Add(FString::Printf(TEXT("comment|%s|%d|%d|%d"), *NodeId,
                Comment->NodeWidth, Comment->NodeHeight, static_cast<int32>(Comment->MoveMode.GetValue())));
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin) || !Pin->PinId.IsValid()) continue;
            const FString PinId = GuidString(Pin->PinId);
            Lines.Add(TEXT("pin|") + NodeId + TEXT("|") + PinId + TEXT("|")
                + Pin->PinName.ToString() + TEXT("|")
                + LexToString(static_cast<int32>(Pin->Direction)) + TEXT("|")
                + VariableTypeFingerprint(Pin->PinType) + TEXT("|") + PinDefaultText(Pin));
            if (Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (LinkedNode == nullptr || ChangedNodes.Contains(LinkedNode)
                    || !LinkedNode->NodeGuid.IsValid() || !Linked->PinId.IsValid()) continue;
                Lines.Add(TEXT("link|") + NodeId + TEXT("|") + PinId + TEXT("|")
                    + GuidString(LinkedNode->NodeGuid) + TEXT("|") + GuidString(Linked->PinId));
            }
        }
    }
    OutFingerprint = HashLines(MoveTemp(Lines));
    return true;
}

bool ApplyPlan(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FRequest& Request,
    const LogicUnit::FBoundary* KnownBoundary,
    const TMap<FString, FUnrealMCPBlueprintActionCatalog::FResolvedAction>& Actions,
    const FUnrealMCPBlueprintBlockReplacementService::FNodeInvoker& NodeInvoker,
    const FUnrealMCPBlueprintBlockReplacementService::FConnectionInvoker& ConnectionInvoker,
    const UnrealMCP::BlueprintNodeLayout::FResult* ResolvedLayout,
    FAppliedPlan& Out,
    FUnrealMCPError& OutError)
{
    LogicUnit::FBoundary Boundary = KnownBoundary != nullptr
        ? *KnownBoundary : LogicUnit::FBoundary();
    const UEdGraphSchema_K2* Schema =
        Cast<UEdGraphSchema_K2>(Graph != nullptr ? Graph->GetSchema() : nullptr);
    const bool bBoundaryAvailable = KnownBoundary != nullptr
        || DescribeBoundary(Blueprint, Graph, Request, Boundary);
    if (!bBoundaryAvailable || Schema == nullptr)
    {
        UEdGraphNode* Candidate = FindNode(Graph, Request.EntryNodeId);
        OutError = {TEXT("stale_precondition"),
            FString::Printf(TEXT("The %s replacement target no longer has one supported logic-unit boundary (root=%s, nodes=%d)"),
                *TargetKindString(Request.TargetKind),
                Candidate != nullptr ? *Candidate->GetClass()->GetName() : TEXT("missing"),
                Graph != nullptr ? Graph->Nodes.Num() : -1)};
        return false;
    }
    const int32 BoundaryNodeCount = 1 + (Boundary.Result != nullptr ? 1 : 0) + Boundary.OwnedNodes.Num();
    const int32 UnrelatedNodeCount = Graph->Nodes.Num() - BoundaryNodeCount;
    for (UEdGraphNode* Node : Boundary.OwnedNodes)
        FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    Boundary.Entry->Modify();
    for (UEdGraphPin* Pin : Boundary.Entry->Pins)
        if (Pin != nullptr) Pin->BreakAllPinLinks(true);
    if (Request.LayoutPolicy == ELayoutPolicy::Explicit)
    {
        Boundary.Entry->NodePosX = Request.EntryPosition.X;
        Boundary.Entry->NodePosY = Request.EntryPosition.Y;
    }
    Out.NodesByKey.Add(TEXT("$entry"), Boundary.Entry);
    if (Boundary.Result != nullptr)
    {
        Boundary.Result->Modify();
        for (UEdGraphPin* Pin : Boundary.Result->Pins)
            if (Pin != nullptr) Pin->BreakAllPinLinks(true);
        if (Request.LayoutPolicy == ELayoutPolicy::Explicit)
        {
            Boundary.Result->NodePosX = Request.ResultPosition.X;
            Boundary.Result->NodePosY = Request.ResultPosition.Y;
        }
        Out.NodesByKey.Add(TEXT("$result"), Boundary.Result);
    }

    for (const FNodePlan& Plan : Request.Nodes)
    {
        const FUnrealMCPBlueprintActionCatalog::FResolvedAction* Action = Actions.Find(Plan.ActionId);
        if (Action == nullptr)
        {
            OutError = {TEXT("invalid_action"), TEXT("A replacement action was not resolved")};
            return false;
        }
        TSet<UEdGraphNode*> Before;
        for (UEdGraphNode* Node : Graph->Nodes) if (Node != nullptr) Before.Add(Node);
        const FVector2D SpawnPosition = Request.LayoutPolicy == ELayoutPolicy::Explicit
            ? FVector2D(Plan.Position.X, Plan.Position.Y)
            : FVector2D(Boundary.Entry->NodePosX, Boundary.Entry->NodePosY);
        UEdGraphNode* Created = NodeInvoker(*Action, Graph, SpawnPosition);
        if (Created == nullptr || Before.Contains(Created) || !Graph->Nodes.Contains(Created))
        {
            OutError = {TEXT("invalid_action"),
                TEXT("A replacement action did not create one new function node")};
            return false;
        }
        if (!Created->NodeGuid.IsValid()) Created->CreateNewGuid();
        for (UEdGraphPin* Pin : Created->Pins)
            if (IsStructuralGraphPin(Created, Pin) && !Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
        if (Request.LayoutPolicy == ELayoutPolicy::Explicit)
        {
            Created->NodePosX = Plan.Position.X;
            Created->NodePosY = Plan.Position.Y;
        }
        if (!IsStableNode(Graph, Created))
        {
            OutError = {TEXT("invalid_action"),
                TEXT("A replacement action produced an unstable or oversized node")};
            return false;
        }
        Out.NodesByKey.Add(Plan.Key, Created);
        Out.CreatedNodes.Add(Created);
    }

    for (const FDefaultPlan& Plan : Request.Defaults)
    {
        UEdGraphNode* const* Node = Out.NodesByKey.Find(Plan.Endpoint.NodeKey);
        UEdGraphPin* Pin = Node != nullptr
            ? FindUniquePin(*Node, Plan.Endpoint.PinName, EGPD_Input, OutError) : nullptr;
        if (Pin == nullptr || !SetDefault(Schema, *Node, Pin, Plan.Value, OutError))
        {
            if (OutError.Code.IsEmpty())
                OutError = {TEXT("internal_error"), TEXT("A replacement default failed read-back")};
            return false;
        }
    }

    int32 ConnectionIndex = 0;
    for (const FConnectionPlan& Plan : Request.Connections)
    {
        UEdGraphNode* const* FromNode = Out.NodesByKey.Find(Plan.From.NodeKey);
        UEdGraphNode* const* ToNode = Out.NodesByKey.Find(Plan.To.NodeKey);
        UEdGraphPin* FromPin = FromNode != nullptr
            ? FindUniquePin(*FromNode, Plan.From.PinName, EGPD_Output, OutError) : nullptr;
        UEdGraphPin* ToPin = ToNode != nullptr
            ? FindUniquePin(*ToNode, Plan.To.PinName, EGPD_Input, OutError) : nullptr;
        if (FromPin == nullptr || ToPin == nullptr || FromPin == ToPin
            || FromPin->LinkedTo.Contains(ToPin) || FromPin->bNotConnectable || ToPin->bNotConnectable)
        {
            if (OutError.Code.IsEmpty())
                OutError = {TEXT("invalid_connection"), TEXT("A replacement connection endpoint is invalid")};
            return false;
        }
        const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
        const bool bNeedsConversion =
            Response.Response == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE;
        const bool bSupportedDirect = Response.Response == CONNECT_RESPONSE_MAKE
            || Response.Response == CONNECT_RESPONSE_MAKE_WITH_PROMOTION;
        if ((Plan.bAutomaticConversion && !bNeedsConversion)
            || (!Plan.bAutomaticConversion && !bSupportedDirect))
        {
            OutError = {bNeedsConversion ? TEXT("conversion_required") : TEXT("incompatible_pins"),
                bNeedsConversion
                    ? TEXT("The replacement connection requires an explicit conversion position")
                    : TEXT("The live K2 schema did not match the planned direct/conversion connection")};
            OutError.Details->SetStringField(
                TEXT("schema_message"), Response.Message.ToString().Left(UnrealMCP::MaxDiagnosticChars));
            return false;
        }
        TSet<UEdGraphNode*> Before;
        for (UEdGraphNode* Node : Graph->Nodes) if (Node != nullptr) Before.Add(Node);
        if (!ConnectionInvoker(Schema, FromPin, ToPin))
        {
            OutError = {TEXT("invalid_connection"),
                TEXT("The live K2 schema rejected a prevalidated replacement connection")};
            return false;
        }
        TArray<UEdGraphNode*> Inserted;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Node != nullptr && !Before.Contains(Node)) Inserted.Add(Node);
        if (Plan.bAutomaticConversion)
        {
            if (Inserted.Num() != 1)
            {
                OutError = {TEXT("internal_error"),
                    TEXT("A replacement conversion did not insert exactly one bounded node")};
                return false;
            }
            UEdGraphNode* Conversion = Inserted[0];
            if (!Conversion->NodeGuid.IsValid()) Conversion->CreateNewGuid();
            for (UEdGraphPin* Pin : Conversion->Pins)
                if (IsStructuralGraphPin(Conversion, Pin) && !Pin->PinId.IsValid())
                    Pin->PinId = FGuid::NewGuid();
            if (Request.LayoutPolicy == ELayoutPolicy::Explicit)
            {
                Conversion->NodePosX = Plan.ConversionPosition.X;
                Conversion->NodePosY = Plan.ConversionPosition.Y;
            }
            if (!IsStableNode(Graph, Conversion)
                || !HasPathThrough(FromPin, ToPin, TSet<UEdGraphNode*>{Conversion}))
            {
                OutError = {TEXT("internal_error"),
                    TEXT("The replacement conversion path failed authoritative read-back")};
                return false;
            }
            const FString Key = FString::Printf(TEXT("$conversion_%d"), ConnectionIndex);
            Out.NodesByKey.Add(Key, Conversion);
            Out.CreatedNodes.Add(Conversion);
            ++Out.ConversionNodeCount;
        }
        else if (!Inserted.IsEmpty() || !FromPin->LinkedTo.Contains(ToPin)
            || !ToPin->LinkedTo.Contains(FromPin))
        {
            OutError = {TEXT("internal_error"),
                TEXT("A direct replacement connection produced an unexpected node or link")};
            return false;
        }
        ++ConnectionIndex;
    }

    for (const FExternalConnectionPlan& Plan : Request.ExternalConnections)
    {
        UEdGraphNode* const* InternalNode = Out.NodesByKey.Find(Plan.Internal.NodeKey);
        UEdGraphNode* ExternalNode = FindNode(Graph, Plan.External.NodeId);
        if (InternalNode == nullptr || ExternalNode == nullptr
            || Out.NodesByKey.FindKey(ExternalNode) != nullptr)
        {
            OutError = {TEXT("invalid_node"), TEXT("A declared external endpoint is unavailable or internal")};
            return false;
        }
        const EEdGraphPinDirection InternalDirection = Plan.bExternalFrom ? EGPD_Input : EGPD_Output;
        const EEdGraphPinDirection ExternalDirection = Plan.bExternalFrom ? EGPD_Output : EGPD_Input;
        UEdGraphPin* InternalPin = FindUniquePin(*InternalNode, Plan.Internal.PinName, InternalDirection, OutError);
        UEdGraphPin* ExternalPin = FindPinById(ExternalNode, Plan.External.PinId);
        if (InternalPin == nullptr || ExternalPin == nullptr || ExternalPin->Direction != ExternalDirection
            || ExternalPin->bNotConnectable || InternalPin->bNotConnectable)
        {
            if (OutError.Code.IsEmpty())
                OutError = {TEXT("invalid_connection"), TEXT("A declared external connection endpoint is invalid")};
            return false;
        }
        UEdGraphPin* FromPin = Plan.bExternalFrom ? ExternalPin : InternalPin;
        UEdGraphPin* ToPin = Plan.bExternalFrom ? InternalPin : ExternalPin;
        const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
        if (Response.Response != CONNECT_RESPONSE_MAKE
            && Response.Response != CONNECT_RESPONSE_MAKE_WITH_PROMOTION)
        {
            OutError = {TEXT("incompatible_pins"),
                TEXT("Declared external connections must be direct and context-valid")};
            OutError.Details->SetStringField(
                TEXT("schema_message"), Response.Message.ToString().Left(UnrealMCP::MaxDiagnosticChars));
            return false;
        }
        const int32 NodeCountBefore = Graph->Nodes.Num();
        if (!ConnectionInvoker(Schema, FromPin, ToPin) || Graph->Nodes.Num() != NodeCountBefore
            || !FromPin->LinkedTo.Contains(ToPin) || !ToPin->LinkedTo.Contains(FromPin))
        {
            OutError = {TEXT("invalid_connection"),
                TEXT("The live K2 schema rejected a prevalidated external connection")};
            return false;
        }
        const FString Identity = Plan.bExternalFrom
            ? TEXT("in|") + Plan.External.NodeId + TEXT("|") + Plan.External.PinId + TEXT("|")
                + Plan.Internal.NodeKey + TEXT("|") + Plan.Internal.PinName
            : TEXT("out|") + Plan.Internal.NodeKey + TEXT("|") + Plan.Internal.PinName + TEXT("|")
                + Plan.External.NodeId + TEXT("|") + Plan.External.PinId;
        Out.ExternalLinks.Add(Identity);
    }

    if (Request.LayoutPolicy == ELayoutPolicy::LayeredV1)
    {
        if (ResolvedLayout != nullptr)
        {
            Out.Layout = *ResolvedLayout;
            if (!UnrealMCP::BlueprintNodeLayout::ApplyResolved(Out.NodesByKey, Out.Layout, OutError)) return false;
        }
        else if (!UnrealMCP::BlueprintNodeLayout::PlanAndApply(
            Graph, Out.NodesByKey, Out.Layout, OutError)) return false;
    }

    if (Graph->Nodes.Num() > UnrealMCP::MaxGraphNodes
        || Graph->Nodes.Num() != UnrelatedNodeCount + (Boundary.Result != nullptr ? 2 : 1)
            + Request.Nodes.Num() + Out.ConversionNodeCount)
    {
        OutError = {TEXT("graph_limit_exceeded"),
            TEXT("The replacement result does not match its complete bounded node plan")};
        return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return BuildSemanticFingerprint(
        Graph, Out.NodesByKey, Out.ExternalLinks, Out.SemanticFingerprint, OutError);
}

bool RollbackAndVerify(
    FUnrealMCPBlueprintInspector& Inspector,
    UBlueprint* Blueprint,
    const FString& AssetPath,
    const FString& ExpectedSnapshot,
    bool bDirtyBefore,
    EBlueprintStatus StatusBefore,
    FUnrealMCPError& OutError)
{
    if (!RestoreFailedTransaction(OutError)) return false;
    FString RestoredSnapshot;
    FUnrealMCPError InspectError;
    if (!ReadSnapshot(Inspector, AssetPath, RestoredSnapshot, InspectError)
        || RestoredSnapshot != ExpectedSnapshot
        || Blueprint->GetOutermost()->IsDirty() != bDirtyBefore
        || Blueprint->Status != StatusBefore)
    {
        OutError = {TEXT("internal_error"),
            TEXT("An unexpected live replacement failure did not restore exact prior Blueprint state")};
        return false;
    }
    return true;
}
}

FUnrealMCPBlueprintBlockReplacementService::FUnrealMCPBlueprintBlockReplacementService(
    FUnrealMCPBlueprintInspector& InInspector,
    FUnrealMCPBlueprintActionCatalog& InActionCatalog,
    FCompile InCompile,
    FNodeInvoker InNodeInvoker,
    FConnectionInvoker InConnectionInvoker)
    : Inspector(InInspector), ActionCatalog(InActionCatalog),
      CompileBlueprint(InCompile ? MoveTemp(InCompile) : FCompile([](UBlueprint* Blueprint, FCompilerResultsLog& Log)
      {
          FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
      })),
      NodeInvoker(InNodeInvoker ? MoveTemp(InNodeInvoker) : FNodeInvoker(
      [](const FUnrealMCPBlueprintActionCatalog::FResolvedAction& Action, UEdGraph* Graph, const FVector2D& Position)
      {
          return Action.Spawner != nullptr ? Action.Spawner->Invoke(Graph, Action.Bindings, Position) : nullptr;
      })),
      ConnectionInvoker(InConnectionInvoker ? MoveTemp(InConnectionInvoker) : FConnectionInvoker(
      [](const UEdGraphSchema_K2* Schema, UEdGraphPin* FromPin, UEdGraphPin* ToPin)
      {
          return Schema != nullptr && Schema->TryCreateConnection(FromPin, ToPin);
      }))
{
}

bool FUnrealMCPBlueprintBlockReplacementService::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintBlockReplacement;
    using namespace UnrealMCP::BlueprintBlockReplacementPrivate;
    using namespace UnrealMCP::BlueprintMutationPrivate;
    check(IsInGameThread());

    FRequest Request;
    if (!Decode(Arguments, Request, OutError)
        || !ValidateMutationScope(Request.PackageName, OutError)) return false;
    const FAssetData Asset =
        FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(Request.AssetPath));
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr
        || !UnrealMCP::BlueprintFamilyPolicy::Supports(
            Blueprint->GeneratedClass, UnrealMCP::BlueprintFamilyPolicy::EOperation::GraphEdit))
    {
        OutError = {TEXT("not_found"),
            TEXT("The requested Blueprint family is unavailable for logic-unit replacement")};
        return false;
    }
    if (Blueprint->bBeingCompiled)
    {
        OutError = {TEXT("busy"), TEXT("The requested Blueprint is compiling"),
            MakeShared<FJsonObject>(), true};
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, Request.AssetPath, Snapshot, OutError)) return false;
    if (Snapshot != Request.ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"),
            TEXT("The Blueprint structural snapshot changed before logic-unit replacement")};
        OutError.Details->SetStringField(TEXT("current_snapshot"), Snapshot);
        return false;
    }
    UEdGraph* Graph = FindGraph(Blueprint, Request);
    LogicUnit::FBoundary Boundary;
    UK2Node_CustomEvent* InitialCustomEvent = Request.TargetKind == ETargetKind::CustomEvent
        ? Cast<UK2Node_CustomEvent>(FindNode(Graph, Request.EntryNodeId)) : nullptr;
    if (Graph == nullptr
        || (Request.TargetKind == ETargetKind::CustomEvent
            && (InitialCustomEvent == nullptr || InitialCustomEvent->IsOverride()))
        || !DescribeBoundary(Blueprint, Graph, Request, Boundary))
    {
        OutError = {TEXT("stale_precondition"),
            TEXT("The exact requested logic-unit boundary is unavailable or unsupported")};
        return false;
    }
    if (GuidString(Boundary.Entry->NodeGuid) != Request.EntryNodeId
        || ((Boundary.Result != nullptr ? GuidString(Boundary.Result->NodeGuid) : FString()) != Request.ResultNodeId)
        || !SameStrings(Boundary.OwnedNodeIds, Request.OwnedNodeIds)
        || !SameStrings(Boundary.LocalVariableIds, Request.LocalVariableIds)
        || Boundary.Fingerprint != Request.ExpectedLogicUnitFingerprint)
    {
        OutError = {TEXT("stale_precondition"),
            TEXT("The supplied root, terminal, owned-node, local, or logic-unit fingerprint boundary is stale")};
        OutError.Details->SetStringField(TEXT("current_logic_unit_fingerprint"), Boundary.Fingerprint);
        return false;
    }
    if (Graph->Nodes.Num() - Boundary.OwnedNodes.Num() + Request.Nodes.Num()
            + Request.Connections.Num() > UnrealMCP::MaxGraphNodes)
    {
        OutError = {TEXT("graph_limit_exceeded"),
            TEXT("The replacement cannot fit within the published graph limit")};
        return false;
    }

    TArray<FString> ActionIds;
    for (const FNodePlan& Node : Request.Nodes) ActionIds.AddUnique(Node.ActionId);
    TMap<FString, FUnrealMCPBlueprintActionCatalog::FResolvedAction> Actions;
    if (!ActionCatalog.ResolveManyForReplacement(
        ActionIds, Blueprint, Graph, Request.AssetPath, Request.GraphId, Snapshot, Actions, OutError))
        return false;

    const FString ScratchName =
        TEXT("UnrealMCPLogicUnitReplace_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FScratchPackage Scratch;
    Scratch.Package = CreatePackage(*(TEXT("/Temp/") + ScratchName));
    Scratch.Blueprint = Scratch.Package != nullptr
        ? DuplicateObject<UBlueprint>(Blueprint, Scratch.Package, *ScratchName) : nullptr;
    UEdGraph* ScratchGraph = FindGraph(Scratch.Blueprint, Request);
    if (Scratch.Package == nullptr || Scratch.Blueprint == nullptr || ScratchGraph == nullptr
        || !RebindScratchHandlerRoot(ScratchGraph, Request, Boundary)
        || !RebindScratchExternalEndpoints(Graph, ScratchGraph, Request)
        || Scratch.Package->HasAnyFlags(RF_Transient) || ScratchGraph->HasAnyFlags(RF_Transient))
    {
        OutError = {TEXT("internal_error"),
            TEXT("Unreal could not create the required isolated non-transient scratch Blueprint")};
        return false;
    }
    FAppliedPlan ScratchApplied;
    bool bScratchApplied = false;
    {
        FScopedTransaction ScratchTransaction(
            FText::FromString(TEXT("Unreal MCP scratch logic-unit replacement")));
        bScratchApplied = ApplyPlan(
            Scratch.Blueprint, ScratchGraph, Request, nullptr, Actions, NodeInvoker, ConnectionInvoker,
            nullptr, ScratchApplied, OutError);
        ScratchTransaction.Cancel();
    }
    if (!bScratchApplied) return false;
    const FString SemanticBeforeCompile = ScratchApplied.SemanticFingerprint;
    FCompilerResultsLog CompileLog;
    CompileLog.bSilentMode = true;
    CompileBlueprint(Scratch.Blueprint, CompileLog);
    FString SemanticAfterCompile;
    if (CompileLog.NumErrors > 0 || Scratch.Blueprint->Status == BS_Error)
    {
        OutError = {TEXT("compile_failed"),
            TEXT("The isolated replacement candidate did not compile")};
        OutError.Details->SetNumberField(TEXT("diagnostic_count"), CompileLog.Messages.Num());
        if (!CompileLog.Messages.IsEmpty())
            OutError.Details->SetStringField(TEXT("first_diagnostic"),
                CompileLog.Messages[0]->ToText().ToString().Left(UnrealMCP::MaxDiagnosticChars));
        return false;
    }
    if (!BuildSemanticFingerprint(
        ScratchGraph, ScratchApplied.NodesByKey, ScratchApplied.ExternalLinks,
        SemanticAfterCompile, OutError)
        || SemanticAfterCompile != SemanticBeforeCompile)
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("compile_failed"),
                TEXT("Candidate compilation changed the planned replacement structure")};
        return false;
    }
    LogicUnit::FBoundary ScratchBoundary;
    if (!DescribeBoundary(Scratch.Blueprint, ScratchGraph, Request, ScratchBoundary)
        || ScratchBoundary.OwnedNodeIds.Num() != ScratchApplied.CreatedNodes.Num())
    {
        OutError = {TEXT("compile_failed"),
            TEXT("Candidate compilation changed the complete logic-unit ownership boundary")};
        return false;
    }
    CleanupFailedCreation(Scratch.Package, Scratch.Blueprint, FString(), false);
    Scratch.Package = nullptr;
    Scratch.Blueprint = nullptr;
    ScratchGraph = nullptr;

    FString SnapshotAfterPreflight;
    LogicUnit::FBoundary BoundaryAfterPreflight;
    if (!ReadSnapshot(Inspector, Request.AssetPath, SnapshotAfterPreflight, OutError)
        || SnapshotAfterPreflight != Snapshot
        || !DescribeBoundary(Blueprint, Graph, Request, BoundaryAfterPreflight)
        || BoundaryAfterPreflight.Fingerprint != Request.ExpectedLogicUnitFingerprint)
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("internal_error"),
                TEXT("Scratch preflight unexpectedly changed the live Blueprint")};
        return false;
    }

    TSet<UEdGraphNode*> UntouchedBeforeExclusions;
    UntouchedBeforeExclusions.Add(Boundary.Entry);
    if (Boundary.Result != nullptr) UntouchedBeforeExclusions.Add(Boundary.Result);
    for (UEdGraphNode* Node : Boundary.OwnedNodes) UntouchedBeforeExclusions.Add(Node);
    FString UntouchedFingerprintBefore;
    if (!BuildUntouchedGraphFingerprint(Graph, UntouchedBeforeExclusions, UntouchedFingerprintBefore))
    {
        OutError = {TEXT("internal_error"), TEXT("The preflight could not fingerprint untouched graph content")};
        return false;
    }

    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    FAppliedPlan LiveApplied;
    bool bApplied = false;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP replace logic unit")));
        Blueprint->Modify();
        Graph->Modify();
        Boundary.Entry->Modify();
        if (Boundary.Result != nullptr) Boundary.Result->Modify();
        for (UEdGraphNode* Node : Boundary.OwnedNodes) Node->Modify();
        bApplied = ApplyPlan(Blueprint, Graph, Request, &Boundary, Actions, NodeInvoker, ConnectionInvoker,
            Request.LayoutPolicy == ELayoutPolicy::LayeredV1 ? &ScratchApplied.Layout : nullptr,
            LiveApplied, OutError);
    }
    if (!bApplied)
    {
        RollbackAndVerify(
            Inspector, Blueprint, Request.AssetPath, Snapshot, bDirtyBefore, StatusBefore, OutError);
        return false;
    }
    if (LiveApplied.SemanticFingerprint != SemanticAfterCompile)
    {
        OutError = {TEXT("internal_error"),
            TEXT("The live replacement did not match the compiled scratch candidate")};
        RollbackAndVerify(
            Inspector, Blueprint, Request.AssetPath, Snapshot, bDirtyBefore, StatusBefore, OutError);
        return false;
    }

    TSet<UEdGraphNode*> UntouchedAfterExclusions;
    for (const TPair<FString, UEdGraphNode*>& Pair : LiveApplied.NodesByKey)
        UntouchedAfterExclusions.Add(Pair.Value);
    FString UntouchedFingerprintAfter;
    if (!BuildUntouchedGraphFingerprint(Graph, UntouchedAfterExclusions, UntouchedFingerprintAfter)
        || UntouchedFingerprintAfter != UntouchedFingerprintBefore)
    {
        OutError = {TEXT("internal_error"),
            TEXT("The live replacement changed untouched graph content or positions")};
        RollbackAndVerify(
            Inspector, Blueprint, Request.AssetPath, Snapshot, bDirtyBefore, StatusBefore, OutError);
        return false;
    }

    FString NewSnapshot;
    LogicUnit::FBoundary NewBoundary;
    if (!ReadSnapshot(Inspector, Request.AssetPath, NewSnapshot, OutError)
        || NewSnapshot == Snapshot
        || !DescribeBoundary(Blueprint, Graph, Request, NewBoundary)
        || GuidString(NewBoundary.Entry->NodeGuid) != Request.EntryNodeId
        || ((NewBoundary.Result != nullptr ? GuidString(NewBoundary.Result->NodeGuid) : FString()) != Request.ResultNodeId)
        || !SameStrings(NewBoundary.LocalVariableIds, Request.LocalVariableIds)
        || NewBoundary.OwnedNodeIds.Num() != LiveApplied.CreatedNodes.Num())
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("internal_error"),
                TEXT("The live replacement failed authoritative logic-unit read-back")};
        RollbackAndVerify(
            Inspector, Blueprint, Request.AssetPath, Snapshot, bDirtyBefore, StatusBefore, OutError);
        return false;
    }
    for (const FString& RemovedId : Request.OwnedNodeIds)
    {
        if (NewBoundary.OwnedNodeIds.Contains(RemovedId))
        {
            OutError = {TEXT("internal_error"),
                TEXT("The live replacement retained an old owned body node identity")};
            RollbackAndVerify(
                Inspector, Blueprint, Request.AssetPath, Snapshot, bDirtyBefore, StatusBefore, OutError);
            return false;
        }
    }

    const TSharedRef<FJsonObject> Changed = MakeShared<FJsonObject>();
    Changed->SetStringField(TEXT("target_kind"), TargetKindString(Request.TargetKind));
    Changed->SetStringField(TEXT("logic_unit_id"), Request.LogicUnitId);
    Changed->SetStringField(TEXT("graph_id"), Request.GraphId);
    Changed->SetStringField(TEXT("logic_unit_fingerprint"), NewBoundary.Fingerprint);
    Changed->SetStringField(TEXT("entry_node_id"), Request.EntryNodeId);
    if (!Request.ResultNodeId.IsEmpty()) Changed->SetStringField(TEXT("result_node_id"), Request.ResultNodeId);
    Changed->SetStringField(TEXT("semantic_fingerprint"), LiveApplied.SemanticFingerprint);
    Changed->SetStringField(TEXT("untouched_graph_fingerprint"), UntouchedFingerprintAfter);
    if (Request.LayoutPolicy == ELayoutPolicy::LayeredV1)
    {
        const TSharedRef<FJsonObject> Layout = MakeShared<FJsonObject>();
        Layout->SetStringField(TEXT("policy"), TEXT("layered_v1"));
        Layout->SetStringField(TEXT("fingerprint"), LiveApplied.Layout.Fingerprint);
        Layout->SetNumberField(TEXT("iterations"), LiveApplied.Layout.Iterations);
        const TSharedRef<FJsonObject> Bounds = MakeShared<FJsonObject>();
        Bounds->SetNumberField(TEXT("min_x"), LiveApplied.Layout.MinX);
        Bounds->SetNumberField(TEXT("min_y"), LiveApplied.Layout.MinY);
        Bounds->SetNumberField(TEXT("max_x"), LiveApplied.Layout.MaxX);
        Bounds->SetNumberField(TEXT("max_y"), LiveApplied.Layout.MaxY);
        Layout->SetObjectField(TEXT("bounds"), Bounds);
        Changed->SetObjectField(TEXT("layout"), Layout);
    }
    Changed->SetBoolField(TEXT("scratch_preflight"), true);
    Changed->SetBoolField(TEXT("compile_succeeded"), true);
    Changed->SetNumberField(TEXT("removed_node_count"), Request.OwnedNodeIds.Num());
    Changed->SetNumberField(TEXT("created_node_count"), LiveApplied.CreatedNodes.Num());
    Changed->SetNumberField(TEXT("conversion_node_count"), LiveApplied.ConversionNodeCount);
    Changed->SetNumberField(TEXT("external_connection_count"), Request.ExternalConnections.Num());
    if (Request.TargetKind == ETargetKind::Function)
    {
        Changed->SetStringField(TEXT("function_id"), Request.LogicUnitId);
        Changed->SetStringField(TEXT("function_fingerprint"), NewBoundary.Fingerprint);
    }
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (UEdGraphNode* Node : LiveApplied.CreatedNodes)
        Nodes.Add(MakeShared<FJsonValueObject>(
            UnrealMCP::BlueprintGraphResultBuilder::EncodeNode(Graph, Node)));
    Changed->SetArrayField(TEXT("nodes"), Nodes);

    OutResult = MakeShared<FJsonObject>();
    OutResult->SetStringField(TEXT("asset_path"), Request.AssetPath);
    OutResult->SetStringField(TEXT("blueprint_family"),
        UnrealMCP::BlueprintFamilyPolicy::Classify(Blueprint->ParentClass).Name);
    OutResult->SetObjectField(TEXT("family_capabilities"),
        UnrealMCP::BlueprintFamilyPolicy::BuildLiveCapabilities(Blueprint));
    OutResult->SetStringField(TEXT("edit"), TEXT("replace_") + TargetKindString(Request.TargetKind));
    OutResult->SetStringField(TEXT("target_kind"), TargetKindString(Request.TargetKind));
    OutResult->SetStringField(TEXT("logic_unit_id"), Request.LogicUnitId);
    OutResult->SetStringField(TEXT("graph_id"), Request.GraphId);
    if (Request.TargetKind == ETargetKind::Function)
        OutResult->SetStringField(TEXT("function_id"), Request.LogicUnitId);
    OutResult->SetStringField(TEXT("snapshot_id"), NewSnapshot);
    OutResult->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    OutResult->SetObjectField(TEXT("changed"), Changed);
    AddDiagnostics(CompileLog, OutResult.ToSharedRef());
    return true;
}
