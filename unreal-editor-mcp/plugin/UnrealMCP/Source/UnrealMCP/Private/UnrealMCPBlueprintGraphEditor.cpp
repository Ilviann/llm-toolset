#include "UnrealMCPBlueprintGraphEditor.h"

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

namespace UnrealMCP::BlueprintGraphEditorPrivate
{
using namespace UnrealMCP::BlueprintMutationPrivate;

struct FGraphEditRequest
{
    FString Operation;
    FString AssetPath;
    FString PackageName;
    FString ExpectedSnapshot;
    FString GraphId;
    FString ActionId;
    FString NodeId;
    FString PinId;
    FString FromNodeId;
    FString FromPinId;
    FString ToNodeId;
    FString ToPinId;
    TSharedPtr<FJsonObject> Default;
    bool bAutomaticConversion = false;
    int32 X = 0;
    int32 Y = 0;
};

static bool IsGuidString(const FString& Value, int32 Digits)
{
    if (Value.Len() != Digits) return false;
    for (TCHAR Character : Value)
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character)) return false;
    return true;
}

static bool ReadPosition(const FJsonObject& Arguments, int32& OutX, int32& OutY, FUnrealMCPError& OutError)
{
    const TSharedPtr<FJsonObject>* Position = nullptr;
    double X = 0.0;
    double Y = 0.0;
    if (!Arguments.TryGetObjectField(TEXT("position"), Position) || Position == nullptr || !Position->IsValid()
        || !HasOnlyFields(**Position, {TEXT("x"), TEXT("y")})
        || !(*Position)->TryGetNumberField(TEXT("x"), X) || !(*Position)->TryGetNumberField(TEXT("y"), Y)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y)
        || !FMath::IsNearlyEqual(X, FMath::RoundToDouble(X)) || !FMath::IsNearlyEqual(Y, FMath::RoundToDouble(Y))
        || FMath::Abs(X) > UnrealMCP::MaxGraphCoordinate || FMath::Abs(Y) > UnrealMCP::MaxGraphCoordinate)
    {
        OutError = {TEXT("invalid_argument"), TEXT("position must contain bounded integer x and y coordinates")};
        return false;
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}

static bool DecodeRequest(const TSharedPtr<FJsonObject>& Arguments, FGraphEditRequest& Out, FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("operation"), Out.Operation)
        || (Out.Operation != TEXT("add_node") && Out.Operation != TEXT("move_node") && Out.Operation != TEXT("remove_node")
            && Out.Operation != TEXT("set_pin_default") && Out.Operation != TEXT("connect_pins")
            && Out.Operation != TEXT("disconnect_pins")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph edit operation is invalid")};
        return false;
    }
    const bool bAdd = Out.Operation == TEXT("add_node");
    const bool bMove = Out.Operation == TEXT("move_node");
    const bool bSetDefault = Out.Operation == TEXT("set_pin_default");
    const bool bConnect = Out.Operation == TEXT("connect_pins");
    const bool bConnection = bConnect || Out.Operation == TEXT("disconnect_pins");
    const bool bShapeValid = bAdd
        ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("action_id"), TEXT("position")})
        : bMove
            ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("node_id"), TEXT("position")})
            : bSetDefault
                ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("node_id"), TEXT("pin_id"), TEXT("default")})
                : bConnect
                    ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"),
                        TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("to_node_id"), TEXT("to_pin_id"), TEXT("automatic_conversion")})
                    : bConnection
                        ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"),
                            TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("to_node_id"), TEXT("to_pin_id")})
                    : HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("node_id")});
    if (!bShapeValid)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph edit arguments have an invalid shape")};
        return false;
    }
    FString OperationId;
    FString RawAssetPath;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !IsGuidString(OperationId, 32)
        || !Arguments->TryGetStringField(TEXT("asset_path"), RawAssetPath)
        || !NormalizeAssetPath(RawAssetPath, Out.AssetPath, Out.PackageName)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), Out.ExpectedSnapshot) || !IsGuidString(Out.ExpectedSnapshot, 40)
        || !Arguments->TryGetStringField(TEXT("graph_id"), Out.GraphId) || !IsGuidString(Out.GraphId, 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph edit identities or asset path are invalid")};
        return false;
    }
    if (bAdd)
    {
        if (!Arguments->TryGetStringField(TEXT("action_id"), Out.ActionId) || !IsGuidString(Out.ActionId, 32)
            || !ReadPosition(*Arguments, Out.X, Out.Y, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_action"), TEXT("action_id is invalid")};
            return false;
        }
    }
    else if (bMove || Out.Operation == TEXT("remove_node") || bSetDefault)
    {
        if (!Arguments->TryGetStringField(TEXT("node_id"), Out.NodeId) || !IsGuidString(Out.NodeId, 32))
        {
            OutError = {TEXT("invalid_node"), TEXT("node_id is invalid")};
            return false;
        }
        if (bMove && !ReadPosition(*Arguments, Out.X, Out.Y, OutError)) return false;
        if (bSetDefault)
        {
            const TSharedPtr<FJsonObject>* Default = nullptr;
            if (!Arguments->TryGetStringField(TEXT("pin_id"), Out.PinId) || !IsGuidString(Out.PinId, 32)
                || !Arguments->TryGetObjectField(TEXT("default"), Default) || Default == nullptr || !Default->IsValid())
            {
                OutError = {TEXT("invalid_pin"), TEXT("pin_id and default must identify one stable pin and typed value")};
                return false;
            }
            Out.Default = *Default;
        }
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("from_node_id"), Out.FromNodeId) || !IsGuidString(Out.FromNodeId, 32)
            || !Arguments->TryGetStringField(TEXT("from_pin_id"), Out.FromPinId) || !IsGuidString(Out.FromPinId, 32)
            || !Arguments->TryGetStringField(TEXT("to_node_id"), Out.ToNodeId) || !IsGuidString(Out.ToNodeId, 32)
            || !Arguments->TryGetStringField(TEXT("to_pin_id"), Out.ToPinId) || !IsGuidString(Out.ToPinId, 32))
        {
            OutError = {TEXT("invalid_pin"), TEXT("Direct connections require stable from/to node and pin identities")};
            return false;
        }
        if (bConnect && Arguments->HasField(TEXT("automatic_conversion"))
            && !Arguments->TryGetBoolField(TEXT("automatic_conversion"), Out.bAutomaticConversion))
        {
            OutError = {TEXT("invalid_argument"), TEXT("automatic_conversion must be a Boolean when supplied")};
            return false;
        }
    }
    return true;
}

static UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphId)
{
    if (Blueprint == nullptr) return nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == GraphId) return Graph;
    return nullptr;
}

static bool IsProtectedGraph(UBlueprint* Blueprint, UEdGraph* Graph)
{
    if (Blueprint == nullptr || Graph == nullptr || Graph->GetTypedOuter<UBlueprint>() != Blueprint
        || Graph->HasAnyFlags(RF_Transient) || Blueprint->IntermediateGeneratedGraphs.Contains(Graph)
        || FBlueprintEditorUtils::FindUserConstructionScript(Blueprint) == Graph)
    {
        return true;
    }
    const bool bOwned = Blueprint->UbergraphPages.Contains(Graph)
        || Blueprint->FunctionGraphs.Contains(Graph) || Blueprint->MacroGraphs.Contains(Graph);
    if (!bOwned) return true;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        if (Interface.Graphs.Contains(Graph)) return true;
    if (Blueprint->FunctionGraphs.Contains(Graph))
    {
        const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph));
        if (Entry == nullptr || !Entry->IsEditable()) return true;
    }
    return Graph->GetSchema() == nullptr || !Graph->GetSchema()->IsA<UEdGraphSchema_K2>();
}

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
        if (Pin != nullptr && Pin->GetOwningNodeUnchecked() == Node && GuidString(Pin->PinId) == PinId) return Pin;
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

static bool IsProtectedNode(UEdGraphNode* Node)
{
    return Node == nullptr || Node->IsIntermediateNode() || !Node->CanUserDeleteNode()
        || Node->GetGraph() == nullptr || Node->GetOuter() != Node->GetGraph();
}

static TSharedRef<FJsonObject> EncodeNode(UEdGraph* Graph, UEdGraphNode* Node)
{
    const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("graph_id"), Graph != nullptr ? GuidString(Graph->GraphGuid) : FString());
    Record->SetStringField(TEXT("id"), Node != nullptr ? GuidString(Node->NodeGuid) : FString());
    Record->SetBoolField(TEXT("identity_stable"), Node != nullptr && Node->NodeGuid.IsValid());
    Record->SetStringField(TEXT("class_path"), Node != nullptr ? Node->GetClass()->GetPathName() : FString());
    Record->SetStringField(TEXT("title"), Node != nullptr ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256) : FString());
    Record->SetNumberField(TEXT("x"), Node != nullptr ? Node->NodePosX : 0);
    Record->SetNumberField(TEXT("y"), Node != nullptr ? Node->NodePosY : 0);
    TArray<TSharedPtr<FJsonValue>> Pins;
    if (Node != nullptr)
    {
        const int32 Count = FMath::Min(Node->Pins.Num(), UnrealMCP::MaxGraphPinsPerNode);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const UEdGraphPin* Pin = Node->Pins[Index];
            if (Pin == nullptr) continue;
            const TSharedRef<FJsonObject> PinRecord = MakeShared<FJsonObject>();
            PinRecord->SetStringField(TEXT("id"), GuidString(Pin->PinId));
            PinRecord->SetBoolField(TEXT("identity_stable"), Pin->PinId.IsValid());
            PinRecord->SetStringField(TEXT("name"), Pin->PinName.ToString().Left(128));
            PinRecord->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
            PinRecord->SetObjectField(TEXT("type"), UnrealMCP::BlueprintInspectionPrivate::PinType(Pin->PinType));
            PinRecord->SetStringField(TEXT("default_value"), Pin->DefaultValue.Left(512));
            if (Pin->DefaultObject != nullptr) PinRecord->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName().Left(512));
            if (!Pin->DefaultTextValue.IsEmpty()) PinRecord->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString().Left(512));
            PinRecord->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, PinDefaultText(Pin)));
            Pins.Add(MakeShared<FJsonValueObject>(PinRecord));
        }
    }
    Record->SetArrayField(TEXT("pins"), Pins);
    Record->SetNumberField(TEXT("pin_count"), Node != nullptr ? Node->Pins.Num() : 0);
    return Record;
}

static void MarkForNode(UBlueprint* Blueprint, UEdGraphNode* Node)
{
    const UK2Node* K2Node = Cast<UK2Node>(Node);
    if (K2Node != nullptr && K2Node->NodeCausesStructuralBlueprintChange())
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    else
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
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
            if (Pin == nullptr) continue;
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
            if (Pin != nullptr && Pin->PersistentGuid == Before.PersistentGuid)
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
        if (Pin != nullptr && Pin->PinName == Before.Name && Pin->Direction == Before.Direction)
        {
            if (Match != nullptr) return nullptr;
            Match = Pin;
        }
    }
    return Match;
}

static bool WasNodeReconstructed(UEdGraphNode* LiveNode, const FNodeIdentityState& Before)
{
    if (LiveNode == nullptr || LiveNode != Before.Pointer || LiveNode->Pins.Num() != Before.Pins.Num()) return true;
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
        if (Pin != nullptr && Pin->PinId.IsValid()) OutIdentities.Add(GuidString(Pin->PinId));
}

static TArray<TSharedPtr<FJsonValue>> EncodeIdentities(const TSet<FString>& Identities)
{
    TArray<FString> Sorted = Identities.Array();
    Sorted.Sort();
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(Sorted.Num());
    for (const FString& Id : Sorted) Result.Add(MakeShared<FJsonValueString>(Id));
    return Result;
}

static TSharedRef<FJsonObject> BuildResult(
    UBlueprint* Blueprint,
    const FGraphEditRequest& Request,
    const FString& Snapshot,
    const TSharedRef<FJsonObject>& Node,
    bool bCreated,
    bool bReturnedExisting,
    const TArray<FString>& CreatedIdentities)
{
    const TSharedRef<FJsonObject> Changed = MakeShared<FJsonObject>();
    Changed->SetStringField(TEXT("operation"), Request.Operation);
    Changed->SetObjectField(TEXT("node"), Node);
    Changed->SetBoolField(TEXT("created"), bCreated);
    Changed->SetBoolField(TEXT("returned_existing"), bReturnedExisting);
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Request.AssetPath);
    Result->SetStringField(TEXT("edit"), Request.Operation);
    Result->SetStringField(TEXT("graph_id"), Request.GraphId);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    Result->SetObjectField(TEXT("changed"), Changed);
    TArray<TSharedPtr<FJsonValue>> Identities;
    for (const FString& Id : CreatedIdentities) Identities.Add(MakeShared<FJsonValueString>(Id));
    Result->SetArrayField(TEXT("created_identities"), Identities);
    Result->SetArrayField(TEXT("reconstructed_identities"), {});
    return Result;
}


static bool ExecutePinEdit(
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
        if (FromNode->Pins.Num() > UnrealMCP::MaxGraphPinsPerNode || ToNode->Pins.Num() > UnrealMCP::MaxGraphPinsPerNode)
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
            && Node->Pins.Num() <= UnrealMCP::MaxGraphPinsPerNode;
        for (const UEdGraphPin* Pin : Node->Pins)
            bInsertedNodesStable &= Pin != nullptr && Pin->PinId.IsValid()
                && Pin->LinkedTo.Num() <= UnrealMCP::MaxGraphLinksPerPin;
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
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Request.AssetPath);
    Result->SetStringField(TEXT("edit"), Request.Operation);
    Result->SetStringField(TEXT("graph_id"), Request.GraphId);
    Result->SetStringField(TEXT("snapshot_id"), NewSnapshot);
    Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    Result->SetObjectField(TEXT("changed"), Changed);
    Result->SetArrayField(TEXT("created_identities"), EncodeIdentities(CreatedIdentities));
    Result->SetArrayField(TEXT("reconstructed_identities"), EncodeIdentities(ReconstructedIdentities));
    OutResult = Result;
    return true;
}
}

FUnrealMCPBlueprintGraphEditor::FUnrealMCPBlueprintGraphEditor(
    FUnrealMCPBlueprintInspector& InInspector,
    FUnrealMCPBlueprintActionCatalog& InActionCatalog,
    FActionResolver InActionResolver,
    FNodeInvoker InNodeInvoker,
    FConnectionInvoker InConnectionInvoker)
    : Inspector(InInspector), ActionCatalog(InActionCatalog), ActionResolver(MoveTemp(InActionResolver)),
      NodeInvoker(MoveTemp(InNodeInvoker)), ConnectionInvoker(MoveTemp(InConnectionInvoker))
{
    if (!ActionResolver)
    {
        ActionResolver = [this](const FString& ActionId, UBlueprint* Blueprint, UEdGraph* Graph,
            const FString& AssetPath, const FString& GraphId, const FString& SnapshotId,
            FUnrealMCPBlueprintActionCatalog::FResolvedAction& OutAction, FUnrealMCPError& OutError)
        {
            return ActionCatalog.ResolveForInvocation(ActionId, Blueprint, Graph, AssetPath, GraphId, SnapshotId, OutAction, OutError);
        };
    }
    if (!NodeInvoker)
    {
        NodeInvoker = [](const FUnrealMCPBlueprintActionCatalog::FResolvedAction& Action, UEdGraph* Graph, const FVector2D& Position)
        {
            return Action.Spawner != nullptr ? Action.Spawner->Invoke(Graph, Action.Bindings, Position) : nullptr;
        };
    }
    if (!ConnectionInvoker)
    {
        ConnectionInvoker = [](const UEdGraphSchema_K2* Schema, UEdGraphPin* FromPin, UEdGraphPin* ToPin)
        {
            return Schema != nullptr && Schema->TryCreateConnection(FromPin, ToPin);
        };
    }
}

bool FUnrealMCPBlueprintGraphEditor::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintGraphEditorPrivate;
    using namespace UnrealMCP::BlueprintMutationPrivate;
    check(IsInGameThread());
    FGraphEditRequest Request;
    if (!DecodeRequest(Arguments, Request, OutError) || !ValidateMutationScope(Request.PackageName, OutError)) return false;
    const FAssetData Asset = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(Request.AssetPath));
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = {TEXT("not_found"), TEXT("The requested Actor Blueprint was not found")};
        return false;
    }
    if (Blueprint->bBeingCompiled)
    {
        OutError = {TEXT("busy"), TEXT("The requested Blueprint is compiling"), MakeShared<FJsonObject>(), true};
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, Request.AssetPath, Snapshot, OutError)) return false;
    if (Snapshot != Request.ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The Blueprint structural snapshot changed before graph mutation")};
        OutError.Details->SetStringField(TEXT("current_snapshot"), Snapshot);
        return false;
    }
    UEdGraph* Graph = FindGraph(Blueprint, Request.GraphId);
    if (Graph == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The requested graph identity was not found")};
        return false;
    }
    if (IsProtectedGraph(Blueprint, Graph))
    {
        OutError = {TEXT("protected_node"), TEXT("The requested graph is inherited, interface-owned, construction, signature, intermediate, or read-only")};
        return false;
    }
    if (Graph->Nodes.Num() > UnrealMCP::MaxGraphNodes
        || (Request.Operation == TEXT("add_node") && Graph->Nodes.Num() >= UnrealMCP::MaxGraphNodes))
    {
        OutError = {TEXT("graph_limit_exceeded"), TEXT("The graph exceeds the supported node limit")};
        return false;
    }
    if (Request.Operation == TEXT("set_pin_default") || Request.Operation == TEXT("connect_pins")
        || Request.Operation == TEXT("disconnect_pins"))
    {
        return ExecutePinEdit(Blueprint, Graph, Request, Inspector, ConnectionInvoker, Snapshot, OutResult, OutError);
    }

    UEdGraphNode* TargetNode = nullptr;
    FUnrealMCPBlueprintActionCatalog::FResolvedAction ResolvedAction;
    if (Request.Operation == TEXT("add_node"))
    {
        if (!ActionResolver(Request.ActionId, Blueprint, Graph, Request.AssetPath, Request.GraphId, Snapshot, ResolvedAction, OutError)) return false;
    }
    else
    {
        TargetNode = FindNode(Graph, Request.NodeId);
        if (TargetNode == nullptr)
        {
            OutError = {TEXT("invalid_node"), TEXT("The requested stable node identity was not found in the graph")};
            return false;
        }
        if (IsProtectedNode(TargetNode))
        {
            OutError = {TEXT("protected_node"), TEXT("The requested node is required, intermediate, signature-owned, or otherwise protected")};
            return false;
        }
    }

    TSharedRef<FJsonObject> ChangedNode = TargetNode != nullptr ? EncodeNode(Graph, TargetNode) : MakeShared<FJsonObject>();
    TArray<FString> CreatedIdentities;
    bool bCreated = false;
    bool bReturnedExisting = false;
    bool bApplied = false;
    bool bTransactionCancelled = false;
    const int32 NodesBefore = Graph->Nodes.Num();
    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP graph node edit")));
        Blueprint->Modify();
        Graph->Modify();
        if (Request.Operation == TEXT("add_node"))
        {
            TSet<UEdGraphNode*> ExistingNodes;
            for (UEdGraphNode* Node : Graph->Nodes) if (Node != nullptr) ExistingNodes.Add(Node);
            TargetNode = NodeInvoker(ResolvedAction, Graph, FVector2D(Request.X, Request.Y));
            if (TargetNode != nullptr && TargetNode->GetGraph() == Graph && Graph->Nodes.Contains(TargetNode))
            {
                bReturnedExisting = ExistingNodes.Contains(TargetNode);
                bCreated = !bReturnedExisting;
                if (bCreated)
                {
                    if (!TargetNode->NodeGuid.IsValid()) TargetNode->CreateNewGuid();
                    CreatedIdentities.Add(GuidString(TargetNode->NodeGuid));
                    for (UEdGraphPin* Pin : TargetNode->Pins)
                    {
                        if (Pin == nullptr) continue;
                        if (!Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
                        CreatedIdentities.Add(GuidString(Pin->PinId));
                    }
                }
                if (TargetNode->NodeGuid.IsValid() && TargetNode->Pins.Num() <= UnrealMCP::MaxGraphPinsPerNode
                    && Graph->Nodes.Num() <= UnrealMCP::MaxGraphNodes)
                {
                    bool bPinsStable = true;
                    for (const UEdGraphPin* Pin : TargetNode->Pins) bPinsStable &= Pin != nullptr && Pin->PinId.IsValid();
                    if (bPinsStable)
                    {
                        if (bCreated) MarkForNode(Blueprint, TargetNode);
                        bApplied = true;
                    }
                }
            }
            FString FailureSnapshot;
            FUnrealMCPError FailureInspectError;
            if (!bApplied && Graph->Nodes.Num() == NodesBefore
                && Blueprint->GetOutermost()->IsDirty() == bDirtyBefore && Blueprint->Status == StatusBefore
                && ReadSnapshot(Inspector, Request.AssetPath, FailureSnapshot, FailureInspectError)
                && FailureSnapshot == Snapshot)
            {
                Transaction.Cancel();
                bTransactionCancelled = true;
            }
        }
        else if (Request.Operation == TEXT("move_node"))
        {
            TargetNode->Modify();
            TargetNode->NodePosX = Request.X;
            TargetNode->NodePosY = Request.Y;
            Graph->NotifyNodeChanged(TargetNode);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            bApplied = true;
        }
        else
        {
            const bool bStructural = Cast<UK2Node>(TargetNode) != nullptr && CastChecked<UK2Node>(TargetNode)->NodeCausesStructuralBlueprintChange();
            FBlueprintEditorUtils::RemoveNode(Blueprint, TargetNode, true);
            if (bStructural) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            else FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            bApplied = true;
        }
    }
    if (!bApplied)
    {
        OutError = {TEXT("invalid_action"), TEXT("The retained action failed to produce one bounded stable graph node")};
        if (!bTransactionCancelled) RestoreFailedTransaction(OutError);
        return false;
    }
    if (Request.Operation != TEXT("remove_node")) ChangedNode = EncodeNode(Graph, TargetNode);

    FString NewSnapshot;
    if (!ReadSnapshot(Inspector, Request.AssetPath, NewSnapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    const bool bVerified = Request.Operation == TEXT("remove_node")
        ? FindNode(Graph, Request.NodeId) == nullptr
        : TargetNode != nullptr && FindNode(Graph, GuidString(TargetNode->NodeGuid)) == TargetNode
            && (Request.Operation != TEXT("move_node") || (TargetNode->NodePosX == Request.X && TargetNode->NodePosY == Request.Y));
    if (!bVerified)
    {
        OutError = {TEXT("internal_error"), TEXT("Graph node mutation failed authoritative read-back verification")};
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildResult(Blueprint, Request, NewSnapshot, ChangedNode, bCreated, bReturnedExisting, CreatedIdentities);
    return true;
}
