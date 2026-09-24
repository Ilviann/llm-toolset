#include "UnrealMCPInspectionBudget.h"

#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"
#include "Blueprint/BlueprintExtension.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWireTypes.h"

namespace UnrealMCP::InspectionBudget
{
bool Check(int32 Records, int32 Fingerprints, FUnrealMCPError& OutError)
{
    if (Records > MaxInspectRecords)
    {
        OutError = {TEXT("response_too_large"), TEXT("Inspection exceeds the configured result record limit")};
        return false;
    }
    if (Fingerprints > MaxInspectFingerprintEntries)
    {
        OutError = {TEXT("response_too_large"), TEXT("Inspection exceeds the configured internal fingerprint limit")};
        return false;
    }
    return true;
}

bool CollectGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs, int32& InOutWork,
    FUnrealMCPError& OutError)
{
    if (Blueprint == nullptr) return true;
    const auto Consume = [&](int32 Count)
    {
        if (Count > MaxInspectInternalWork - InOutWork)
        {
            OutError = {TEXT("response_too_large"), Blueprint->IsA<UAnimBlueprint>()
                ? TEXT("Animation graph inspection exceeds the structural work limit")
                : TEXT("Graph inspection exceeds the internal work limit")};
            return false;
        }
        InOutWork += Count;
        return true;
    };
    if (const UAnimBlueprint* Animation = Cast<UAnimBlueprint>(Blueprint))
    {
        if (!Consume(Animation->Groups.Num()) || !Consume(Animation->ParentAssetOverrides.Num())) return false;
    }
    TArray<UEdGraph*> Roots;
    const auto Append = [&](const auto& Source)
    {
        if (!Consume(Source.Num())) return false;
        for (UEdGraph* Graph : Source) Roots.Add(Graph);
        return true;
    };
    if (!Append(Blueprint->UbergraphPages) || !Append(Blueprint->FunctionGraphs)
        || !Append(Blueprint->MacroGraphs) || !Append(Blueprint->DelegateSignatureGraphs)
        || !Consume(Blueprint->ImplementedInterfaces.Num())) return false;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        if (!Append(Interface.Graphs)) return false;
    if (!Consume(Blueprint->GetExtensions().Num())) return false;
    for (const UBlueprintExtension* Extension : Blueprint->GetExtensions())
    {
        if (Extension == nullptr) continue;
        TArray<UEdGraph*> ExtensionGraphs;
        Extension->GetAllGraphs(ExtensionGraphs);
        if (!Append(ExtensionGraphs)) return false;
    }

    TSet<UEdGraph*> Visited;
    TSet<UEdGraph*> Active;
    TArray<TPair<UEdGraph*, bool>> Stack;
    for (UEdGraph* Root : Roots)
    {
        Stack.Emplace(Root, false);
        while (!Stack.IsEmpty())
        {
            const TPair<UEdGraph*, bool> Frame = Stack.Pop(EAllowShrinking::No);
            UEdGraph* Graph = Frame.Key;
            if (Graph == nullptr) continue;
            if (Frame.Value) { Active.Remove(Graph); continue; }
            if (Active.Contains(Graph))
            {
                OutError = {TEXT("invalid_asset"), TEXT("Inspection encountered a graph cycle")};
                return false;
            }
            if (Visited.Contains(Graph)) continue;
            Visited.Add(Graph);
            Active.Add(Graph);
            OutGraphs.Add(Graph);
            if (!Consume(Graph->Nodes.Num()) || !Consume(Graph->SubGraphs.Num())) return false;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node == nullptr) continue;
                if (!Consume(Node->Pins.Num())) return false;
                for (UEdGraphPin* Pin : Node->Pins)
                    if (Pin != nullptr && !Consume(Pin->LinkedTo.Num())) return false;
            }
            Stack.Emplace(Graph, true);
            for (UEdGraph* Child : Graph->SubGraphs) Stack.Emplace(Child, false);
        }
    }
    return true;
}
}
