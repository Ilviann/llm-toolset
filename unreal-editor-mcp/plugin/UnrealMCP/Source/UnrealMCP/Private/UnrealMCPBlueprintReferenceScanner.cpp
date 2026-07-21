#include "UnrealMCPBlueprintReferenceScanner.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::BlueprintReferences
{
namespace
{
FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

FScanResult BuildResult(TArray<UK2Node*> Nodes, const bool bReferenced)
{
    Nodes.Sort([](const UK2Node& Left, const UK2Node& Right)
    {
        const UEdGraph* LeftGraph = Left.GetGraph();
        const UEdGraph* RightGraph = Right.GetGraph();
        return (LeftGraph != nullptr ? GuidString(LeftGraph->GraphGuid) : FString()) + GuidString(Left.NodeGuid)
            < (RightGraph != nullptr ? GuidString(RightGraph->GraphGuid) : FString()) + GuidString(Right.NodeGuid);
    });

    FScanResult Result;
    Result.bReferenced = bReferenced;
    Result.bUnresolvedReferences = bReferenced && Nodes.IsEmpty();
    Result.bTruncated = Nodes.Num() > UnrealMCP::MaxVariableReferences;
    Result.ReferenceCount = Nodes.Num();
    const int32 Count = FMath::Min(Nodes.Num(), UnrealMCP::MaxVariableReferences);
    Result.References.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        UK2Node* Node = Nodes[Index];
        if (Node == nullptr) continue;
        const UEdGraph* Graph = Node->GetGraph();
        Result.References.Add({
            Graph != nullptr ? GuidString(Graph->GraphGuid) : FString(),
            GuidString(Node->NodeGuid),
            Node->GetClass()->GetPathName(),
            Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256)});
    }
    return Result;
}

template <typename Predicate>
TArray<UK2Node*> ScanAllGraphs(UBlueprint* Blueprint, Predicate&& Matches)
{
    TArray<UK2Node*> Nodes;
    if (Blueprint == nullptr) return Nodes;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr) continue;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            UK2Node* Node = Cast<UK2Node>(GraphNode);
            if (Node != nullptr && Matches(Node)) Nodes.Add(Node);
        }
    }
    return Nodes;
}
}

FScanResult ScanMemberVariable(UBlueprint* Blueprint, const FName VariableName)
{
    TArray<UK2Node*> Nodes = ScanAllGraphs(Blueprint, [VariableName](UK2Node* Node)
    {
        return Node->ReferencesVariable(VariableName, nullptr);
    });
    const bool bReferenced = !Nodes.IsEmpty()
        || (Blueprint != nullptr && FBlueprintEditorUtils::IsVariableUsed(Blueprint, VariableName));
    return BuildResult(MoveTemp(Nodes), bReferenced);
}

FScanResult ScanFunction(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
    if (Blueprint == nullptr || FunctionGraph == nullptr) return {};
    const FName FunctionName = FunctionGraph->GetFName();
    TArray<UK2Node*> Nodes = ScanAllGraphs(Blueprint, [Blueprint, FunctionName](UK2Node* Node)
    {
        return !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>()
            && Node->ReferencesFunction(FunctionName, Blueprint->SkeletonGeneratedClass);
    });
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsFunctionUsed(Blueprint, FunctionName);
    return BuildResult(MoveTemp(Nodes), bReferenced);
}

FScanResult ScanLocalVariable(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FName VariableName)
{
    if (Blueprint == nullptr || FunctionGraph == nullptr) return {};
    TArray<UK2Node*> Nodes;
    const UStruct* Scope = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName()) : nullptr;
    for (UEdGraphNode* GraphNode : FunctionGraph->Nodes)
    {
        UK2Node* Node = Cast<UK2Node>(GraphNode);
        if (Node != nullptr && Node->ReferencesVariable(VariableName, Scope)) Nodes.Add(Node);
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsVariableUsed(Blueprint, VariableName, FunctionGraph);
    return BuildResult(MoveTemp(Nodes), bReferenced);
}

FScanResult ScanMacro(UBlueprint* Blueprint, UEdGraph* MacroGraph)
{
    TArray<UK2Node*> Nodes = ScanAllGraphs(Blueprint, [MacroGraph](UK2Node* Node)
    {
        const UK2Node_MacroInstance* Instance = Cast<UK2Node_MacroInstance>(Node);
        return Instance != nullptr && Instance->GetMacroGraph() == MacroGraph;
    });
    const bool bReferenced = !Nodes.IsEmpty();
    return BuildResult(MoveTemp(Nodes), bReferenced);
}

FScanResult ScanCustomEvent(UBlueprint* Blueprint, UK2Node_CustomEvent* Event)
{
    if (Blueprint == nullptr || Event == nullptr) return {};
    TArray<UK2Node*> Nodes = ScanAllGraphs(Blueprint, [Blueprint, Event](UK2Node* Node)
    {
        return Node != Event && !Node->IsA<UK2Node_CustomEvent>()
            && Node->ReferencesFunction(Event->CustomFunctionName, Blueprint->SkeletonGeneratedClass);
    });
    const bool bReferenced = !Nodes.IsEmpty()
        || FBlueprintEditorUtils::IsFunctionUsed(Blueprint, Event->CustomFunctionName);
    return BuildResult(MoveTemp(Nodes), bReferenced);
}

TSharedRef<FJsonObject> Encode(const FScanResult& Result)
{
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("referenced"), Result.bReferenced);
    Summary->SetNumberField(TEXT("reference_count"), Result.ReferenceCount);
    Summary->SetBoolField(TEXT("unresolved_references"), Result.bUnresolvedReferences);
    Summary->SetBoolField(TEXT("references_truncated"), Result.bTruncated);
    TArray<TSharedPtr<FJsonValue>> References;
    References.Reserve(Result.References.Num());
    for (const FNodeReference& Item : Result.References)
    {
        const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
        Reference->SetStringField(TEXT("graph_id"), Item.GraphId);
        Reference->SetStringField(TEXT("node_id"), Item.NodeId);
        Reference->SetStringField(TEXT("node_class"), Item.NodeClass);
        Reference->SetStringField(TEXT("title"), Item.Title);
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Summary->SetArrayField(TEXT("references"), References);
    return Summary;
}
}
