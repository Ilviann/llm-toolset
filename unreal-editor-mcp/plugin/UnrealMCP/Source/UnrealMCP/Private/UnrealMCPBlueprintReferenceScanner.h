#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

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

FScanResult ScanMemberVariable(UBlueprint* Blueprint, FName VariableName);
FScanResult ScanFunction(UBlueprint* Blueprint, UEdGraph* FunctionGraph);
FScanResult ScanLocalVariable(UBlueprint* Blueprint, UEdGraph* FunctionGraph, FName VariableName);
FScanResult ScanMacro(UBlueprint* Blueprint, UEdGraph* MacroGraph);
FScanResult ScanCustomEvent(UBlueprint* Blueprint, UK2Node_CustomEvent* Event);

TSharedRef<FJsonObject> Encode(const FScanResult& Result);
}
