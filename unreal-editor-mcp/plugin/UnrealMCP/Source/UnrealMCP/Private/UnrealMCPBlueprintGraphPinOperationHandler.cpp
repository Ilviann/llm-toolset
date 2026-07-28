#include "UnrealMCPBlueprintGraphPinOperationHandler.h"

#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintGraphRequestValidation.h"
#include "UnrealMCPBlueprintGraphResultBuilder.h"

#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPBlueprintMutationCommon.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCP::BlueprintGraphPinOperationHandler
{
using namespace UnrealMCP::BlueprintMutationPrivate;
using UnrealMCP::BlueprintInspectionPrivate::IsStructuralGraphPin;
using UnrealMCP::BlueprintInspectionPrivate::StructuralGraphPinCount;
using FGraphEditRequest = UnrealMCP::BlueprintGraphRequestValidation::FRequest;
using UnrealMCP::BlueprintGraphResultBuilder::EncodeNode;

static UEdGraphNode* FindNode(UEdGraph* Graph, const FString& NodeId)
{
    if (Graph == nullptr) return nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node != nullptr && GuidString(Node->NodeGuid) == NodeId) return Node;
    return nullptr;
}

static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinId)
{
    if (Node == nullptr) return nullptr;
    for (UEdGraphPin* Pin : Node->Pins)
        if (IsStructuralGraphPin(Node, Pin) && Pin->GetOwningNodeUnchecked() == Node
            && GuidString(Pin->PinId) == PinId) return Pin;
    return nullptr;
}

static bool IsStablePinNode(UEdGraph* Graph, UEdGraphNode* Node)
{
    return Graph != nullptr && Node != nullptr && Node->GetGraph() == Graph && Node->GetOuter() == Graph
        && Node->NodeGuid.IsValid() && !Node->IsIntermediateNode();
}

static bool IsProtectedConnectionPin(UEdGraphPin* Pin)
{
    return Pin == nullptr || !Pin->PinId.IsValid() || Pin->bWasTrashed || Pin->bOrphanedPin || Pin->bNotConnectable;
}

static FString PinDefaultText(const UEdGraphPin* Pin)
{
    if (Pin == nullptr) return FString();
    if (Pin->DefaultObject != nullptr) return Pin->DefaultObject->GetPathName();
    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text) return Pin->DefaultTextValue.ToString();
    return Pin->DefaultValue;
}

struct FPinIdentityState
{
    FString Id;
    FGuid PersistentGuid;
    FName Name;
    EEdGraphPinDirection Direction = EGPD_Input;
    FEdGraphPinType Type;
    UEdGraphPin* Pointer = nullptr;
};

struct FNodeIdentityState
{
    FString Id;
    UEdGraphNode* Pointer = nullptr;
    TArray<FPinIdentityState> Pins;
};

static FNodeIdentityState CaptureNodeIdentity(UEdGraphNode* Node)
{
    FNodeIdentityState State;
    State.Pointer = Node;
    State.Id = Node != nullptr ? GuidString(Node->NodeGuid) : FString();
    if (Node != nullptr)
    {
        State.Pins.Reserve(Node->Pins.Num());
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin)) continue;
            State.Pins.Add({GuidString(Pin->PinId), Pin->PersistentGuid, Pin->PinName, Pin->Direction, Pin->PinType, Pin});
        }
    }
    return State;
}

static UEdGraphPin* ResolveReconstructedPin(UEdGraphNode* Node, const FPinIdentityState& Before)
{
    if (Node == nullptr) return nullptr;
    if (UEdGraphPin* SameId = FindPin(Node, Before.Id)) return SameId;
    if (Before.PersistentGuid.IsValid())
    {
        UEdGraphPin* Match = nullptr;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (IsStructuralGraphPin(Node, Pin) && Pin->PersistentGuid == Before.PersistentGuid)
            {
                if (Match != nullptr) return nullptr;
                Match = Pin;
            }
        }
        if (Match != nullptr) return Match;
    }
    UEdGraphPin* Match = nullptr;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (IsStructuralGraphPin(Node, Pin) && Pin->PinName == Before.Name && Pin->Direction == Before.Direction)
        {
            if (Match != nullptr) return nullptr;
            Match = Pin;
        }
    }
    return Match;
}

static bool WasNodeReconstructed(UEdGraphNode* LiveNode, const FNodeIdentityState& Before)
{
    if (LiveNode == nullptr || LiveNode != Before.Pointer
        || StructuralGraphPinCount(LiveNode) != Before.Pins.Num()) return true;
    for (const FPinIdentityState& PinBefore : Before.Pins)
    {
        UEdGraphPin* LivePin = ResolveReconstructedPin(LiveNode, PinBefore);
        if (LivePin == nullptr || LivePin != PinBefore.Pointer || LivePin->PinType != PinBefore.Type) return true;
    }
    return false;
}

static FString PinIdentity(const UEdGraphPin* Pin)
{
    const UEdGraphNode* Node = Pin != nullptr ? Pin->GetOwningNodeUnchecked() : nullptr;
    return Node != nullptr && Node->NodeGuid.IsValid() && Pin->PinId.IsValid()
        ? GuidString(Node->NodeGuid) + TEXT(":") + GuidString(Pin->PinId)
        : FString();
}

static TSet<FString> LinkIdentities(const UEdGraphPin* Pin)
{
    TSet<FString> Result;
    if (Pin != nullptr)
        for (const UEdGraphPin* Linked : Pin->LinkedTo)
            if (const FString Id = PinIdentity(Linked); !Id.IsEmpty()) Result.Add(Id);
    return Result;
}

static bool SameLinkIdentities(const UEdGraphPin* Pin, const TSet<FString>& Expected)
{
    if (Pin == nullptr || Pin->LinkedTo.Num() != Expected.Num()) return false;
    for (UEdGraphPin* Linked : Pin->LinkedTo)
        if (Linked == nullptr || !Linked->LinkedTo.Contains(const_cast<UEdGraphPin*>(Pin))
            || !Expected.Contains(PinIdentity(Linked))) return false;
    return true;
}

static bool WouldCreateDirectedCycle(UEdGraphNode* FromNode, UEdGraphNode* ToNode)
{
    if (FromNode == nullptr || ToNode == nullptr || FromNode == ToNode) return true;
    TArray<UEdGraphNode*> Pending{ToNode};
    TSet<UEdGraphNode*> Visited;
    while (!Pending.IsEmpty() && Visited.Num() <= UnrealMCP::MaxGraphNodes)
    {
        UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
        if (Node == nullptr || Visited.Contains(Node)) continue;
        if (Node == FromNode) return true;
        Visited.Add(Node);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
                if (Linked != nullptr && Linked->Direction == EGPD_Input) Pending.Add(Linked->GetOwningNodeUnchecked());
        }
    }
    return false;
}

static bool HasConnectionPathThrough(
    UEdGraphPin* FromPin,
    UEdGraphPin* ToPin,
    const TSet<UEdGraphNode*>& AllowedIntermediateNodes)
{
    if (FromPin == nullptr || ToPin == nullptr) return false;
    TArray<UEdGraphPin*> Pending{FromPin};
    TSet<UEdGraphPin*> Visited;
    while (!Pending.IsEmpty() && Visited.Num() <= UnrealMCP::MaxGraphLinksPerPin * (UnrealMCP::MaxAutomaticConversionNodes + 1))
    {
        UEdGraphPin* Output = Pending.Pop(EAllowShrinking::No);
        if (Output == nullptr || Visited.Contains(Output)) continue;
        Visited.Add(Output);
        for (UEdGraphPin* Linked : Output->LinkedTo)
        {
            if (Linked == nullptr || !Linked->LinkedTo.Contains(Output)) continue;
            if (Linked == ToPin) return true;
            UEdGraphNode* Intermediate = Linked->GetOwningNodeUnchecked();
            if (!AllowedIntermediateNodes.Contains(Intermediate)) continue;
            for (UEdGraphPin* Candidate : Intermediate->Pins)
                if (Candidate != nullptr && Candidate->Direction == EGPD_Output) Pending.Add(Candidate);
        }
    }
    return false;
}

static void AddNodeAndPinIdentities(UEdGraphNode* Node, TSet<FString>& OutIdentities)
{
    if (Node == nullptr || !Node->NodeGuid.IsValid()) return;
    OutIdentities.Add(GuidString(Node->NodeGuid));
    for (const UEdGraphPin* Pin : Node->Pins)
        if (IsStructuralGraphPin(Node, Pin) && Pin->PinId.IsValid()) OutIdentities.Add(GuidString(Pin->PinId));
}

bool Execute(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FGraphEditRequest& Request,
    FUnrealMCPBlueprintInspector& Inspector,
    const FUnrealMCPBlueprintGraphEditor::FConnectionInvoker& ConnectionInvoker,
    const FString& Snapshot,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph != nullptr ? Graph->GetSchema() : nullptr);
    if (Blueprint == nullptr || Graph == nullptr || Schema == nullptr)
    {
        OutError = {TEXT("protected_pin"), TEXT("Pin edits require one live local K2 graph schema")};
        return false;
    }

    UEdGraphNode* FromNode = nullptr;
    UEdGraphNode* ToNode = nullptr;
    UEdGraphPin* FromPin = nullptr;
    UEdGraphPin* ToPin = nullptr;
    FString NewDefaultText;
    FString ParsedDefaultValue;
    TObjectPtr<UObject> ParsedDefaultObject = nullptr;
    FText ParsedDefaultText;
    bool bEngineDefault = false;
    bool bConversionInsertion = false;
    bool bWildcardSpecialization = false;
    ECanCreateConnectionResponse ConnectionResponse = CONNECT_RESPONSE_DISALLOW;
    TSet<FString> FromLinksBefore;
    TSet<FString> ToLinksBefore;
    TSet<FString> ExpectedFromLinks;
    TSet<FString> ExpectedToLinks;
    FNodeIdentityState FromNodeBefore;
    FNodeIdentityState ToNodeBefore;
    FPinIdentityState FromPinBefore;
    FPinIdentityState ToPinBefore;
    int32 ReplacedLinkCount = 0;

    if (Request.Operation == TEXT("set_pin_default"))
    {
        FromNode = FindNode(Graph, Request.NodeId);
        FromPin = FindPin(FromNode, Request.PinId);
        if (!IsStablePinNode(Graph, FromNode) || FromPin == nullptr)
        {
            OutError = {TEXT("invalid_pin"), TEXT("The requested stable node and pin identities were not found in the graph")};
            return false;
        }
        const TSharedRef<FJsonObject> EncodedType = UnrealMCP::K2TypeCodec::EncodeType(FromPin->PinType);
        if (IsProtectedConnectionPin(FromPin) || FromPin->Direction != EGPD_Input || FromPin->HasAnyConnections()
            || FromPin->bDefaultValueIsIgnored || FromPin->bDefaultValueIsReadOnly
            || Schema->ShouldHidePinDefaultValue(FromPin) || !EncodedType->GetBoolField(TEXT("supported")))
        {
            OutError = {TEXT("protected_pin"), TEXT("The requested pin is not one editable, unlinked, supported K2 input default")};
            return false;
        }
        FString Kind;
        if (!Request.Default.IsValid() || !Request.Default->TryGetStringField(TEXT("kind"), Kind)
            || !UnrealMCP::K2TypeCodec::DecodeDefault(FromPin->PinType, Request.Default, NewDefaultText, OutError))
        {
            return false;
        }
        bEngineDefault = Kind == TEXT("engine_default");
        if (bEngineDefault) NewDefaultText = FromPin->AutogeneratedDefaultValue;
        if (NewDefaultText.Len() > UnrealMCP::MaxPinDefaultChars)
        {
            OutError = {TEXT("pin_default_too_large"), TEXT("The canonical pin default exceeds the published character limit")};
            return false;
        }
        Schema->GetPinDefaultValuesFromString(FromPin->PinType, FromNode, NewDefaultText,
            ParsedDefaultValue, ParsedDefaultObject, ParsedDefaultText, false);
        const FString Validation = Schema->IsPinDefaultValid(FromPin, ParsedDefaultValue, ParsedDefaultObject, ParsedDefaultText);
        if (!Validation.IsEmpty())
        {
            OutError = {TEXT("invalid_pin_default"), TEXT("The live K2 schema rejected the typed pin default")};
            OutError.Details->SetStringField(TEXT("schema_message"), Validation.Left(UnrealMCP::MaxDiagnosticChars));
            return false;
        }
        const bool bAlreadySet = bEngineDefault ? Schema->DoesDefaultValueMatchAutogenerated(*FromPin)
            : FromPin->DefaultValue == ParsedDefaultValue && FromPin->DefaultObject == ParsedDefaultObject
                && FromPin->DefaultTextValue.EqualTo(ParsedDefaultText);
        if (bAlreadySet)
        {
            OutError = {TEXT("no_change"), TEXT("The pin already has the requested default")};
            return false;
        }
    }
    else
    {
        FromNode = FindNode(Graph, Request.FromNodeId);
        ToNode = FindNode(Graph, Request.ToNodeId);
        FromPin = FindPin(FromNode, Request.FromPinId);
        ToPin = FindPin(ToNode, Request.ToPinId);
        if (!IsStablePinNode(Graph, FromNode) || !IsStablePinNode(Graph, ToNode)
            || IsProtectedConnectionPin(FromPin) || IsProtectedConnectionPin(ToPin))
        {
            OutError = {TEXT("invalid_pin"), TEXT("The requested stable node and pin identities were not found or are protected")};
            return false;
        }
        if (FromPin->Direction != EGPD_Output || ToPin->Direction != EGPD_Input)
        {
            OutError = {TEXT("invalid_connection"), TEXT("Direct connections require an output from_pin and input to_pin")};
            return false;
        }
        if (StructuralGraphPinCount(FromNode) > UnrealMCP::MaxGraphPinsPerNode
            || StructuralGraphPinCount(ToNode) > UnrealMCP::MaxGraphPinsPerNode)
        {
            OutError = {TEXT("graph_limit_exceeded"), TEXT("One requested endpoint exceeds the changed-node pin limit")};
            return false;
        }
        if (FromPin->LinkedTo.Num() > UnrealMCP::MaxGraphLinksPerPin || ToPin->LinkedTo.Num() > UnrealMCP::MaxGraphLinksPerPin)
        {
            OutError = {TEXT("graph_limit_exceeded"), TEXT("One requested pin exceeds the supported direct-link limit")};
            return false;
        }
        FromNodeBefore = CaptureNodeIdentity(FromNode);
        ToNodeBefore = CaptureNodeIdentity(ToNode);
        for (const FPinIdentityState& Pin : FromNodeBefore.Pins)
            if (Pin.Id == Request.FromPinId) FromPinBefore = Pin;
        for (const FPinIdentityState& Pin : ToNodeBefore.Pins)
            if (Pin.Id == Request.ToPinId) ToPinBefore = Pin;
        FromLinksBefore = LinkIdentities(FromPin);
        ToLinksBefore = LinkIdentities(ToPin);
        ExpectedFromLinks = FromLinksBefore;
        ExpectedToLinks = ToLinksBefore;
        if (Request.Operation == TEXT("disconnect_pins"))
        {
            if (!FromLinksBefore.Contains(PinIdentity(ToPin)) || !ToLinksBefore.Contains(PinIdentity(FromPin)))
            {
                OutError = {TEXT("invalid_connection"), TEXT("The requested direct pin connection does not exist")};
                return false;
            }
            ExpectedFromLinks.Remove(PinIdentity(ToPin));
            ExpectedToLinks.Remove(PinIdentity(FromPin));
        }
        else
        {
            if (FromLinksBefore.Contains(PinIdentity(ToPin)) || ToLinksBefore.Contains(PinIdentity(FromPin)))
            {
                OutError = {TEXT("invalid_connection"), TEXT("The requested pins are already directly connected")};
                return false;
            }
            if (WouldCreateDirectedCycle(FromNode, ToNode))
            {
                OutError = {TEXT("invalid_connection"), TEXT("The requested connection would create a directed graph cycle")};
                OutError.Details->SetBoolField(TEXT("cycle"), true);
                return false;
            }
            const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
            ConnectionResponse = Response.Response;
            bConversionInsertion = ConnectionResponse == CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE;
            bWildcardSpecialization = FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
                || ToPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
                || ConnectionResponse == CONNECT_RESPONSE_MAKE_WITH_PROMOTION;
            if (bConversionInsertion && !Request.bAutomaticConversion)
            {
                OutError = {TEXT("conversion_required"), TEXT("The live K2 schema requires a conversion node; automatic insertion is disabled")};
                OutError.Details->SetStringField(TEXT("schema_message"), Response.Message.ToString().Left(UnrealMCP::MaxDiagnosticChars));
                return false;
            }
            if (ConnectionResponse != CONNECT_RESPONSE_MAKE && ConnectionResponse != CONNECT_RESPONSE_BREAK_OTHERS_A
                && ConnectionResponse != CONNECT_RESPONSE_BREAK_OTHERS_B && ConnectionResponse != CONNECT_RESPONSE_BREAK_OTHERS_AB
                && ConnectionResponse != CONNECT_RESPONSE_MAKE_WITH_PROMOTION
                && ConnectionResponse != CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE)
            {
                OutError = {TEXT("incompatible_pins"), TEXT("The live K2 schema rejected the pin connection")};
                OutError.Details->SetStringField(TEXT("schema_message"), Response.Message.ToString().Left(UnrealMCP::MaxDiagnosticChars));
                return false;
            }
            if (bConversionInsertion && Graph->Nodes.Num() + UnrealMCP::MaxAutomaticConversionNodes > UnrealMCP::MaxGraphNodes)
            {
                OutError = {TEXT("graph_limit_exceeded"), TEXT("The graph has no bounded capacity for automatic conversion insertion")};
                return false;
            }
            if (ConnectionResponse == CONNECT_RESPONSE_BREAK_OTHERS_A || ConnectionResponse == CONNECT_RESPONSE_BREAK_OTHERS_AB)
            {
                ReplacedLinkCount += ExpectedFromLinks.Num();
                ExpectedFromLinks.Reset();
            }
            if (ConnectionResponse == CONNECT_RESPONSE_BREAK_OTHERS_B || ConnectionResponse == CONNECT_RESPONSE_BREAK_OTHERS_AB)
            {
                ReplacedLinkCount += ExpectedToLinks.Num();
                ExpectedToLinks.Reset();
            }
            if (ReplacedLinkCount + 1 > UnrealMCP::MaxGraphLinksPerPin)
            {
                OutError = {TEXT("graph_limit_exceeded"), TEXT("The direct connection replacement exceeds the transaction-work limit")};
                return false;
            }
            if (!bConversionInsertion)
            {
                ExpectedFromLinks.Add(PinIdentity(ToPin));
                ExpectedToLinks.Add(PinIdentity(FromPin));
                if (ExpectedFromLinks.Num() > UnrealMCP::MaxGraphLinksPerPin
                    || ExpectedToLinks.Num() > UnrealMCP::MaxGraphLinksPerPin)
                {
                    OutError = {TEXT("graph_limit_exceeded"), TEXT("The resulting direct connection would exceed the per-pin link limit")};
                    return false;
                }
            }
        }
    }

    const int32 NodesBefore = Graph->Nodes.Num();
    TSet<UEdGraphNode*> NodeSetBefore;
    for (UEdGraphNode* Node : Graph->Nodes) if (Node != nullptr) NodeSetBefore.Add(Node);
    bool bApplied = false;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP graph pin edit")));
        Blueprint->Modify();
        Graph->Modify();
        FromNode->Modify();
        FromPin->Modify();
        if (ToNode != nullptr && ToNode != FromNode) ToNode->Modify();
        if (ToPin != nullptr) ToPin->Modify();
        if (Request.Operation == TEXT("set_pin_default"))
        {
            if (bEngineDefault) Schema->ResetPinToAutogeneratedDefaultValue(FromPin, true);
            else Schema->TrySetDefaultValue(*FromPin, NewDefaultText, true);
            bApplied = true;
        }
        else if (Request.Operation == TEXT("connect_pins"))
        {
            bApplied = ConnectionInvoker(Schema, FromPin, ToPin);
        }
        else
        {
            Schema->BreakSinglePinLink(FromPin, ToPin);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            bApplied = true;
        }
    }

    TArray<UEdGraphNode*> InsertedNodes;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node != nullptr && !NodeSetBefore.Contains(Node)) InsertedNodes.Add(Node);
    UEdGraphNode* LiveFromNode = FindNode(Graph, Request.Operation == TEXT("set_pin_default") ? Request.NodeId : Request.FromNodeId);
    UEdGraphNode* LiveToNode = Request.Operation == TEXT("set_pin_default") ? nullptr : FindNode(Graph, Request.ToNodeId);
    UEdGraphPin* LiveFromPin = Request.Operation == TEXT("set_pin_default")
        ? FindPin(LiveFromNode, Request.PinId) : ResolveReconstructedPin(LiveFromNode, FromPinBefore);
    UEdGraphPin* LiveToPin = Request.Operation == TEXT("set_pin_default")
        ? nullptr : ResolveReconstructedPin(LiveToNode, ToPinBefore);
    const bool bExpectedInsertionCount = bConversionInsertion
        ? InsertedNodes.Num() >= 1 && InsertedNodes.Num() <= UnrealMCP::MaxAutomaticConversionNodes
        : InsertedNodes.IsEmpty();
    bool bInsertedNodesStable = true;
    TSet<UEdGraphNode*> InsertedNodeSet;
    for (UEdGraphNode* Node : InsertedNodes)
    {
        InsertedNodeSet.Add(Node);
        bInsertedNodesStable &= Node->GetGraph() == Graph && Node->GetOuter() == Graph && Node->NodeGuid.IsValid()
            && StructuralGraphPinCount(Node) <= UnrealMCP::MaxGraphPinsPerNode;
        for (const UEdGraphPin* Pin : Node->Pins)
            if (Pin == nullptr)
                bInsertedNodesStable = false;
            else if (IsStructuralGraphPin(Node, Pin))
                bInsertedNodesStable &= Pin->PinId.IsValid() && Pin->LinkedTo.Num() <= UnrealMCP::MaxGraphLinksPerPin;
    }
    bool bVerified = bApplied && bExpectedInsertionCount && bInsertedNodesStable
        && Graph->Nodes.Num() <= UnrealMCP::MaxGraphNodes && LiveFromNode != nullptr && LiveFromPin != nullptr;
    if (Request.Operation == TEXT("set_pin_default"))
    {
        bVerified = bVerified && Graph->Nodes.Num() == NodesBefore && LiveFromNode == FromNode && LiveFromPin == FromPin
            && (bEngineDefault ? Schema->DoesDefaultValueMatchAutogenerated(*LiveFromPin)
            : LiveFromPin->DefaultValue == ParsedDefaultValue && LiveFromPin->DefaultObject == ParsedDefaultObject
                && LiveFromPin->DefaultTextValue.EqualTo(ParsedDefaultText));
    }
    else
    {
        bVerified = bVerified && LiveToNode != nullptr && LiveToPin != nullptr
            && LiveFromPin->LinkedTo.Num() <= UnrealMCP::MaxGraphLinksPerPin
            && LiveToPin->LinkedTo.Num() <= UnrealMCP::MaxGraphLinksPerPin;
        if (bConversionInsertion)
        {
            bVerified = bVerified && HasConnectionPathThrough(LiveFromPin, LiveToPin, InsertedNodeSet);
        }
        else
        {
            if (Request.Operation == TEXT("connect_pins"))
            {
                ExpectedFromLinks.Remove(Request.ToNodeId + TEXT(":") + Request.ToPinId);
                ExpectedToLinks.Remove(Request.FromNodeId + TEXT(":") + Request.FromPinId);
                ExpectedFromLinks.Add(PinIdentity(LiveToPin));
                ExpectedToLinks.Add(PinIdentity(LiveFromPin));
            }
            bVerified = bVerified && SameLinkIdentities(LiveFromPin, ExpectedFromLinks)
                && SameLinkIdentities(LiveToPin, ExpectedToLinks);
        }
    }
    if (!bVerified)
    {
        OutError = {TEXT("internal_error"), TEXT("Graph pin mutation failed identity or authoritative schema read-back verification")};
        RestoreFailedTransaction(OutError);
        return false;
    }

    FString NewSnapshot;
    if (!ReadSnapshot(Inspector, Request.AssetPath, NewSnapshot, OutError) || NewSnapshot == Snapshot)
    {
        if (OutError.Code.IsEmpty()) OutError = {TEXT("internal_error"), TEXT("Graph pin mutation did not produce a new structural snapshot")};
        RestoreFailedTransaction(OutError);
        return false;
    }
    TSet<FString> CreatedIdentities;
    TSet<FString> ReconstructedIdentities;
    TArray<TSharedPtr<FJsonValue>> ChangedNodes;
    for (UEdGraphNode* Node : InsertedNodes)
    {
        AddNodeAndPinIdentities(Node, CreatedIdentities);
        ChangedNodes.Add(MakeShared<FJsonValueObject>(EncodeNode(Graph, Node)));
    }
    if (Request.Operation != TEXT("set_pin_default"))
    {
        if (WasNodeReconstructed(LiveFromNode, FromNodeBefore))
        {
            AddNodeAndPinIdentities(LiveFromNode, ReconstructedIdentities);
            ChangedNodes.Add(MakeShared<FJsonValueObject>(EncodeNode(Graph, LiveFromNode)));
        }
        if (LiveToNode != LiveFromNode && WasNodeReconstructed(LiveToNode, ToNodeBefore))
        {
            AddNodeAndPinIdentities(LiveToNode, ReconstructedIdentities);
            ChangedNodes.Add(MakeShared<FJsonValueObject>(EncodeNode(Graph, LiveToNode)));
        }
    }
    const TSharedRef<FJsonObject> Changed = MakeShared<FJsonObject>();
    Changed->SetStringField(TEXT("operation"), Request.Operation);
    if (Request.Operation == TEXT("set_pin_default"))
    {
        Changed->SetObjectField(TEXT("node"), EncodeNode(Graph, LiveFromNode));
        Changed->SetStringField(TEXT("pin_id"), Request.PinId);
        Changed->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(LiveFromPin->PinType, PinDefaultText(LiveFromPin)));
    }
    else
    {
        const TSharedRef<FJsonObject> Connection = MakeShared<FJsonObject>();
        Connection->SetStringField(TEXT("from_node_id"), Request.FromNodeId);
        Connection->SetStringField(TEXT("from_pin_id"), GuidString(LiveFromPin->PinId));
        Connection->SetStringField(TEXT("to_node_id"), Request.ToNodeId);
        Connection->SetStringField(TEXT("to_pin_id"), GuidString(LiveToPin->PinId));
        Connection->SetBoolField(TEXT("connected"), Request.Operation == TEXT("connect_pins"));
        Connection->SetBoolField(TEXT("direct"), !bConversionInsertion);
        Connection->SetBoolField(TEXT("automatic_conversion"), bConversionInsertion);
        Connection->SetBoolField(TEXT("wildcard_specialized"), bWildcardSpecialization);
        Connection->SetNumberField(TEXT("conversion_node_count"), InsertedNodes.Num());
        Connection->SetNumberField(TEXT("replaced_link_count"), ReplacedLinkCount);
        Changed->SetObjectField(TEXT("connection"), Connection);
        Changed->SetArrayField(TEXT("nodes"), ChangedNodes);
    }
    OutResult = UnrealMCP::BlueprintGraphResultBuilder::Build(
        Blueprint, Request, NewSnapshot, Changed, CreatedIdentities, ReconstructedIdentities);
    return true;
}
}
