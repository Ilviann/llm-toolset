#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class UBlueprint;
class UEdGraph;
class UK2Node_CustomEvent;

namespace UnrealMCP::BlueprintReferences
{
struct FNodeReference
{
    FString GraphId;
    FString NodeId;
    FString NodeClass;
    FString Title;
};

struct FScanResult
{
    bool bReferenced = false;
    bool bUnresolvedReferences = false;
    bool bTruncated = false;
    int32 ReferenceCount = 0;
    TArray<FNodeReference> References;
};

UNREALMCPBLUEPRINT_API FScanResult ScanMemberVariable(UBlueprint* Blueprint, FName VariableName);
UNREALMCPBLUEPRINT_API FScanResult ScanFunction(UBlueprint* Blueprint, UEdGraph* FunctionGraph);
UNREALMCPBLUEPRINT_API FScanResult ScanLocalVariable(UBlueprint* Blueprint, UEdGraph* FunctionGraph, FName VariableName);
UNREALMCPBLUEPRINT_API FScanResult ScanMacro(UBlueprint* Blueprint, UEdGraph* MacroGraph);
UNREALMCPBLUEPRINT_API FScanResult ScanCustomEvent(UBlueprint* Blueprint, UK2Node_CustomEvent* Event);

UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> Encode(const FScanResult& Result);
}
