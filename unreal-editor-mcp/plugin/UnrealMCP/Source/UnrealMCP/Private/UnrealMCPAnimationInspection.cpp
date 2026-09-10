#include "UnrealMCPAnimationInspection.h"

#include "Animation/AnimationAsset.h"
#include "AnimationGraph.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"

namespace UnrealMCP::AnimationInspection
{
FString GraphKind(const UEdGraph* Graph)
{
    if (Graph->IsA<UAnimationStateMachineGraph>()) return TEXT("state_machine");
    if (Graph->IsA<UAnimationTransitionGraph>()) return TEXT("transition");
    if (Graph->IsA<UAnimationStateGraph>()) return TEXT("animation_state");
    if (Graph->IsA<UAnimationGraph>()) return TEXT("animation");
    return FString();
}

TSharedRef<FJsonObject> NodeRelationships(const UEdGraphNode* Node)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    auto AddGraph = [&Result](const TCHAR* Field, const UEdGraph* Graph)
    {
        if (Graph != nullptr)
            Result->SetStringField(Field, Graph->GraphGuid.IsValid()
                ? Graph->GraphGuid.ToString(EGuidFormats::Digits).ToLower() : FString());
    };
    if (const UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Node))
        AddGraph(TEXT("bound_graph_id"), Machine->EditorStateMachineGraph);
    if (const UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(Node))
        AddGraph(TEXT("bound_graph_id"), State->GetBoundGraph());
    if (const UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
        AddGraph(TEXT("custom_transition_graph_id"), Transition->CustomTransitionGraph);
    if (const UAnimGraphNode_Base* Animation = Cast<UAnimGraphNode_Base>(Node))
    {
        if (const UAnimationAsset* Asset = Animation->GetAnimationAsset())
            Result->SetStringField(TEXT("animation_asset"), Asset->GetPathName());
    }
    return Result;
}
}
