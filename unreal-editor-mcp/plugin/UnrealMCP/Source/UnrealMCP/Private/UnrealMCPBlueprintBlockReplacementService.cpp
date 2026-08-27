#include "UnrealMCPBlueprintBlockReplacementService.h"

#include "UnrealMCPBlueprintBlockReplacementRequest.h"
#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintFunctionFingerprint.h"
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
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
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
using UnrealMCP::BlueprintInspectionPrivate::IsStructuralGraphPin;
using UnrealMCP::BlueprintInspectionPrivate::StructuralGraphPinCount;
using UnrealMCP::BlueprintInspectionPrivate::VariableTypeFingerprint;

struct FAppliedPlan
{
    TMap<FString, UEdGraphNode*> NodesByKey;
    TArray<UEdGraphNode*> CreatedNodes;
    FString SemanticFingerprint;
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

UEdGraph* FindFunction(UBlueprint* Blueprint, const FString& FunctionId)
{
    if (Blueprint == nullptr) return nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == FunctionId) return Graph;
    return nullptr;
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

bool BuildSemanticFingerprint(
    UEdGraph* Graph,
    const TMap<FString, UEdGraphNode*>& NodesByKey,
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
    if (KeysByNode.Num() != Graph->Nodes.Num())
    {
        OutError = {TEXT("internal_error"),
            TEXT("The replacement produced nodes outside its complete declared plan")};
        return false;
    }
    TArray<FString> Lines;
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
                    OutError = {TEXT("internal_error"),
                        TEXT("The replacement produced a link outside its complete function boundary")};
                    return false;
                }
                Lines.Add(TEXT("link|") + Pair.Key + TEXT("|") + Pin->PinName.ToString()
                    + TEXT("|") + *LinkedKey + TEXT("|") + Linked->PinName.ToString());
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
    const TMap<FString, FUnrealMCPBlueprintActionCatalog::FResolvedAction>& Actions,
    const FUnrealMCPBlueprintBlockReplacementService::FNodeInvoker& NodeInvoker,
    const FUnrealMCPBlueprintBlockReplacementService::FConnectionInvoker& ConnectionInvoker,
    FAppliedPlan& Out,
    FUnrealMCPError& OutError)
{
    UnrealMCP::BlueprintFunctionFingerprint::FBoundary Boundary;
    const UEdGraphSchema_K2* Schema =
        Cast<UEdGraphSchema_K2>(Graph != nullptr ? Graph->GetSchema() : nullptr);
    if (!UnrealMCP::BlueprintFunctionFingerprint::Describe(Graph, Boundary) || Schema == nullptr)
    {
        OutError = {TEXT("stale_precondition"),
            TEXT("The replacement function no longer has one supported entry/result boundary")};
        return false;
    }
    for (UEdGraphNode* Node : Boundary.OwnedNodes)
        FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    Boundary.Entry->Modify();
    Boundary.Result->Modify();
    for (UEdGraphPin* Pin : Boundary.Entry->Pins)
        if (Pin != nullptr) Pin->BreakAllPinLinks(true);
    for (UEdGraphPin* Pin : Boundary.Result->Pins)
        if (Pin != nullptr) Pin->BreakAllPinLinks(true);
    Boundary.Entry->NodePosX = Request.EntryPosition.X;
    Boundary.Entry->NodePosY = Request.EntryPosition.Y;
    Boundary.Result->NodePosX = Request.ResultPosition.X;
    Boundary.Result->NodePosY = Request.ResultPosition.Y;
    Out.NodesByKey.Add(TEXT("$entry"), Boundary.Entry);
    Out.NodesByKey.Add(TEXT("$result"), Boundary.Result);

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
        UEdGraphNode* Created =
            NodeInvoker(*Action, Graph, FVector2D(Plan.Position.X, Plan.Position.Y));
        if (Created == nullptr || Before.Contains(Created) || !Graph->Nodes.Contains(Created))
        {
            OutError = {TEXT("invalid_action"),
                TEXT("A replacement action did not create one new function node")};
            return false;
        }
        if (!Created->NodeGuid.IsValid()) Created->CreateNewGuid();
        for (UEdGraphPin* Pin : Created->Pins)
            if (IsStructuralGraphPin(Created, Pin) && !Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
        Created->NodePosX = Plan.Position.X;
        Created->NodePosY = Plan.Position.Y;
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
            Conversion->NodePosX = Plan.ConversionPosition.X;
            Conversion->NodePosY = Plan.ConversionPosition.Y;
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

    if (Graph->Nodes.Num() > UnrealMCP::MaxGraphNodes
        || Graph->Nodes.Num() != 2 + Request.Nodes.Num() + Out.ConversionNodeCount)
    {
        OutError = {TEXT("graph_limit_exceeded"),
            TEXT("The replacement result does not match its complete bounded node plan")};
        return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return BuildSemanticFingerprint(Graph, Out.NodesByKey, Out.SemanticFingerprint, OutError);
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
            Blueprint, UnrealMCP::BlueprintFamilyPolicy::EOperation::GraphEdit))
    {
        OutError = {TEXT("not_found"),
            TEXT("The requested Blueprint family is unavailable for function replacement")};
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
            TEXT("The Blueprint structural snapshot changed before function replacement")};
        OutError.Details->SetStringField(TEXT("current_snapshot"), Snapshot);
        return false;
    }
    UEdGraph* Graph = FindFunction(Blueprint, Request.FunctionId);
    UnrealMCP::BlueprintFunctionFingerprint::FBoundary Boundary;
    if (Graph == nullptr || !IsUserOwnedFunction(Blueprint, Graph)
        || !UnrealMCP::BlueprintFunctionFingerprint::Describe(Graph, Boundary))
    {
        OutError = {TEXT("stale_precondition"),
            TEXT("The exact user-owned function boundary is unavailable or has multiple result nodes")};
        return false;
    }
    if (GuidString(Boundary.Entry->NodeGuid) != Request.EntryNodeId
        || GuidString(Boundary.Result->NodeGuid) != Request.ResultNodeId
        || !SameStrings(Boundary.OwnedNodeIds, Request.OwnedNodeIds)
        || !SameStrings(Boundary.LocalVariableIds, Request.LocalVariableIds)
        || Boundary.Fingerprint != Request.ExpectedFunctionFingerprint)
    {
        OutError = {TEXT("stale_precondition"),
            TEXT("The supplied entry, result, owned-node, local, or function fingerprint boundary is stale")};
        OutError.Details->SetStringField(TEXT("current_function_fingerprint"), Boundary.Fingerprint);
        return false;
    }
    if (2 + Request.Nodes.Num() + Request.Connections.Num() > UnrealMCP::MaxGraphNodes)
    {
        OutError = {TEXT("graph_limit_exceeded"),
            TEXT("The replacement cannot fit within the published function graph limit")};
        return false;
    }

    TArray<FString> ActionIds;
    for (const FNodePlan& Node : Request.Nodes) ActionIds.AddUnique(Node.ActionId);
    TMap<FString, FUnrealMCPBlueprintActionCatalog::FResolvedAction> Actions;
    if (!ActionCatalog.ResolveManyForReplacement(
        ActionIds, Blueprint, Graph, Request.AssetPath, Request.FunctionId, Snapshot, Actions, OutError))
        return false;

    const FString ScratchName =
        TEXT("UnrealMCPFunctionReplace_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FScratchPackage Scratch;
    Scratch.Package = CreatePackage(*(TEXT("/Temp/") + ScratchName));
    Scratch.Blueprint = Scratch.Package != nullptr
        ? DuplicateObject<UBlueprint>(Blueprint, Scratch.Package, *ScratchName) : nullptr;
    UEdGraph* ScratchGraph = FindFunction(Scratch.Blueprint, Request.FunctionId);
    if (Scratch.Package == nullptr || Scratch.Blueprint == nullptr || ScratchGraph == nullptr
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
            FText::FromString(TEXT("Unreal MCP scratch function replacement")));
        bScratchApplied = ApplyPlan(
            Scratch.Blueprint, ScratchGraph, Request, Actions, NodeInvoker, ConnectionInvoker,
            ScratchApplied, OutError);
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
        ScratchGraph, ScratchApplied.NodesByKey, SemanticAfterCompile, OutError)
        || SemanticAfterCompile != SemanticBeforeCompile)
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("compile_failed"),
                TEXT("Candidate compilation changed the planned replacement structure")};
        return false;
    }
    CleanupFailedCreation(Scratch.Package, Scratch.Blueprint, FString(), false);
    Scratch.Package = nullptr;
    Scratch.Blueprint = nullptr;
    ScratchGraph = nullptr;

    FString SnapshotAfterPreflight;
    if (!ReadSnapshot(Inspector, Request.AssetPath, SnapshotAfterPreflight, OutError)
        || SnapshotAfterPreflight != Snapshot
        || UnrealMCP::BlueprintFunctionFingerprint::Build(Graph)
            != Request.ExpectedFunctionFingerprint)
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("internal_error"),
                TEXT("Scratch preflight unexpectedly changed the live Blueprint")};
        return false;
    }

    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    FAppliedPlan LiveApplied;
    bool bApplied = false;
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP replace function")));
        Blueprint->Modify();
        Graph->Modify();
        Boundary.Entry->Modify();
        Boundary.Result->Modify();
        for (UEdGraphNode* Node : Boundary.OwnedNodes) Node->Modify();
        bApplied = ApplyPlan(Blueprint, Graph, Request, Actions, NodeInvoker, ConnectionInvoker,
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

    FString NewSnapshot;
    UnrealMCP::BlueprintFunctionFingerprint::FBoundary NewBoundary;
    if (!ReadSnapshot(Inspector, Request.AssetPath, NewSnapshot, OutError)
        || NewSnapshot == Snapshot
        || !UnrealMCP::BlueprintFunctionFingerprint::Describe(Graph, NewBoundary)
        || GuidString(NewBoundary.Entry->NodeGuid) != Request.EntryNodeId
        || GuidString(NewBoundary.Result->NodeGuid) != Request.ResultNodeId
        || !SameStrings(NewBoundary.LocalVariableIds, Request.LocalVariableIds))
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("internal_error"),
                TEXT("The live replacement failed authoritative function read-back")};
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
    Changed->SetStringField(TEXT("function_id"), Request.FunctionId);
    Changed->SetStringField(TEXT("function_fingerprint"), NewBoundary.Fingerprint);
    Changed->SetStringField(TEXT("entry_node_id"), Request.EntryNodeId);
    Changed->SetStringField(TEXT("result_node_id"), Request.ResultNodeId);
    Changed->SetStringField(TEXT("semantic_fingerprint"), LiveApplied.SemanticFingerprint);
    Changed->SetBoolField(TEXT("scratch_preflight"), true);
    Changed->SetBoolField(TEXT("compile_succeeded"), true);
    Changed->SetNumberField(TEXT("removed_node_count"), Request.OwnedNodeIds.Num());
    Changed->SetNumberField(TEXT("created_node_count"), LiveApplied.CreatedNodes.Num());
    Changed->SetNumberField(TEXT("conversion_node_count"), LiveApplied.ConversionNodeCount);
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
    OutResult->SetStringField(TEXT("edit"), TEXT("replace_function"));
    OutResult->SetStringField(TEXT("function_id"), Request.FunctionId);
    OutResult->SetStringField(TEXT("snapshot_id"), NewSnapshot);
    OutResult->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    OutResult->SetObjectField(TEXT("changed"), Changed);
    AddDiagnostics(CompileLog, OutResult.ToSharedRef());
    return true;
}
