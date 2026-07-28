#include "UnrealMCPBlueprintGraphEditor.h"

#include "UnrealMCPBlueprintGraphPinOperationHandler.h"
#include "UnrealMCPBlueprintGraphRequestValidation.h"
#include "UnrealMCPBlueprintGraphResultBuilder.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPBlueprintMutationCommon.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UnrealMCPVersion.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCP::BlueprintGraphOperationHandlersPrivate
{
using namespace UnrealMCP::BlueprintMutationPrivate;
using UnrealMCP::BlueprintInspectionPrivate::IsStructuralGraphPin;
using UnrealMCP::BlueprintInspectionPrivate::StructuralGraphPinCount;
using UnrealMCP::BlueprintGraphRequestValidation::FRequest;
using UnrealMCP::BlueprintGraphResultBuilder::EncodeNode;

UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphId)
{
    if (Blueprint == nullptr) return nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        if (Graph != nullptr && GuidString(Graph->GraphGuid) == GraphId) return Graph;
    return nullptr;
}

bool IsProtectedGraph(UBlueprint* Blueprint, UEdGraph* Graph)
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

UEdGraphNode* FindNode(UEdGraph* Graph, const FString& NodeId)
{
    if (Graph == nullptr) return nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node != nullptr && GuidString(Node->NodeGuid) == NodeId) return Node;
    return nullptr;
}

bool IsProtectedNode(UEdGraphNode* Node)
{
    return Node == nullptr || Node->IsIntermediateNode() || !Node->CanUserDeleteNode()
        || Node->GetGraph() == nullptr || Node->GetOuter() != Node->GetGraph();
}

void MarkForNode(UBlueprint* Blueprint, UEdGraphNode* Node)
{
    const UK2Node* K2Node = Cast<UK2Node>(Node);
    if (K2Node != nullptr && K2Node->NodeCausesStructuralBlueprintChange())
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    else
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
}
}

bool FUnrealMCPBlueprintGraphEditor::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintGraphOperationHandlersPrivate;
    using namespace UnrealMCP::BlueprintMutationPrivate;
    check(IsInGameThread());
    FRequest Request;
    if (!UnrealMCP::BlueprintGraphRequestValidation::Decode(Arguments, Request, OutError)
        || !ValidateMutationScope(Request.PackageName, OutError)) return false;
    const FAssetData Asset = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(Request.AssetPath));
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr
        || !UnrealMCP::BlueprintFamilyPolicy::Supports(
            Blueprint->GeneratedClass, UnrealMCP::BlueprintFamilyPolicy::EOperation::GraphEdit))
    {
        OutError = {TEXT("not_found"), TEXT("The requested Blueprint family is unavailable for graph editing")};
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
        return UnrealMCP::BlueprintGraphPinOperationHandler::Execute(
            Blueprint, Graph, Request, Inspector, ConnectionInvoker, Snapshot, OutResult, OutError);
    }

    UEdGraphNode* TargetNode = nullptr;
    FUnrealMCPBlueprintActionCatalog::FResolvedAction ResolvedAction;
    if (Request.Operation == TEXT("add_node"))
    {
        if (!ActionResolver(Request.ActionId, Blueprint, Graph, Request.AssetPath, Request.GraphId, Snapshot, ResolvedAction, OutError))
            return false;
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
    TSet<FString> CreatedIdentities;
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
            for (UEdGraphNode* Node : Graph->Nodes)
                if (Node != nullptr) ExistingNodes.Add(Node);
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
                        if (!IsStructuralGraphPin(TargetNode, Pin)) continue;
                        if (!Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
                        CreatedIdentities.Add(GuidString(Pin->PinId));
                    }
                }
                if (TargetNode->NodeGuid.IsValid()
                    && StructuralGraphPinCount(TargetNode) <= UnrealMCP::MaxGraphPinsPerNode
                    && Graph->Nodes.Num() <= UnrealMCP::MaxGraphNodes)
                {
                    bool bPinsStable = true;
                    for (const UEdGraphPin* Pin : TargetNode->Pins)
                    {
                        if (Pin == nullptr)
                            bPinsStable = false;
                        else if (IsStructuralGraphPin(TargetNode, Pin))
                            bPinsStable &= Pin->PinId.IsValid();
                    }
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
            const bool bStructural = Cast<UK2Node>(TargetNode) != nullptr
                && CastChecked<UK2Node>(TargetNode)->NodeCausesStructuralBlueprintChange();
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
            && (Request.Operation != TEXT("move_node")
                || (TargetNode->NodePosX == Request.X && TargetNode->NodePosY == Request.Y));
    if (!bVerified)
    {
        OutError = {TEXT("internal_error"), TEXT("Graph node mutation failed authoritative read-back verification")};
        RestoreFailedTransaction(OutError);
        return false;
    }
    const TSharedRef<FJsonObject> Changed = MakeShared<FJsonObject>();
    Changed->SetStringField(TEXT("operation"), Request.Operation);
    Changed->SetObjectField(TEXT("node"), ChangedNode);
    Changed->SetBoolField(TEXT("created"), bCreated);
    Changed->SetBoolField(TEXT("returned_existing"), bReturnedExisting);
    OutResult = UnrealMCP::BlueprintGraphResultBuilder::Build(
        Blueprint, Request, NewSnapshot, Changed, CreatedIdentities, TSet<FString>());
    return true;
}
