#include "UnrealMCPBlueprintFunctionFingerprint.h"

#include "UnrealMCPBlueprintInspectionSupport.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/SecureHash.h"

namespace UnrealMCP::BlueprintFunctionFingerprint
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
}

FString Build(UEdGraph* Graph)
{
    if (Graph == nullptr) return FString();
    TArray<FString> Lines;
    Lines.Add(TEXT("graph|") + GuidString(Graph->GraphGuid) + TEXT("|") + Graph->GetName());

    UK2Node_FunctionEntry* Entry =
        Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph));
    if (Entry != nullptr)
    {
        Lines.Add(TEXT("entry|") + GuidString(Entry->NodeGuid) + TEXT("|")
            + LexToString(Entry->GetFunctionFlags()) + TEXT("|")
            + Entry->MetaData.Category.ToString() + TEXT("|")
            + Entry->MetaData.ToolTip.ToString() + TEXT("|")
            + Entry->MetaData.Keywords.ToString() + TEXT("|")
            + LexToString(Entry->MetaData.bCallInEditor));
        for (const FBPVariableDescription& Local : Entry->LocalVariables)
        {
            Lines.Add(TEXT("local|") + GuidString(Local.VarGuid) + TEXT("|")
                + Local.VarName.ToString() + TEXT("|")
                + VariableTypeFingerprint(Local.VarType) + TEXT("|") + Local.DefaultValue);
        }
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr) continue;
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
            if (Pin->Direction == EGPD_Output)
            {
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    UEdGraphNode* LinkedNode = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                    if (LinkedNode != nullptr)
                    {
                        Lines.Add(TEXT("link|") + NodeId + TEXT("|") + PinId + TEXT("|")
                            + GuidString(LinkedNode->NodeGuid) + TEXT("|") + GuidString(Linked->PinId));
                    }
                }
            }
        }
    }
    return HashLines(MoveTemp(Lines));
}

bool Describe(UEdGraph* Graph, FBoundary& Out)
{
    Out = FBoundary();
    if (Graph == nullptr) return false;
    Out.Entry = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph));
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Results);
    if (Out.Entry == nullptr || Results.Num() != 1 || Results[0] == nullptr) return false;
    Out.Result = Results[0];
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr || Node == Out.Entry || Node == Out.Result) continue;
        if (!Node->NodeGuid.IsValid()) return false;
        Out.OwnedNodes.Add(Node);
        Out.OwnedNodeIds.Add(GuidString(Node->NodeGuid));
    }
    for (const FBPVariableDescription& Local : Out.Entry->LocalVariables)
    {
        if (!Local.VarGuid.IsValid()) return false;
        Out.LocalVariableIds.Add(GuidString(Local.VarGuid));
    }
    Out.OwnedNodeIds.Sort();
    Out.LocalVariableIds.Sort();
    Out.Fingerprint = Build(Graph);
    return Out.Fingerprint.Len() == 40
        && Out.Entry->NodeGuid.IsValid() && Out.Result->NodeGuid.IsValid();
}
}
