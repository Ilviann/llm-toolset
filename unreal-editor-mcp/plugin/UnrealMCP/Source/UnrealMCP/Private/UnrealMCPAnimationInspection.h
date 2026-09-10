#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UEdGraph;
class UEdGraphNode;

namespace UnrealMCP::AnimationInspection
{
FString GraphKind(const UEdGraph* Graph);
TSharedRef<FJsonObject> NodeRelationships(const UEdGraphNode* Node);
}
