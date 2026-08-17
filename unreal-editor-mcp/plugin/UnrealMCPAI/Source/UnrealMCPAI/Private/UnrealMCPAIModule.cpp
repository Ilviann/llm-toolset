#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPAIVersion.h"

#include "Algo/Reverse.h"
#include "AIGraphNode.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Decorators/BTDecorator_BlueprintBase.h"
#include "BehaviorTree/Services/BTService_BlueprintBase.h"
#include "BehaviorTree/Tasks/BTTask_BlueprintBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_BlueprintBase.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_BlueprintBase.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Services/BTService_DefaultFocus.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_ActorsOfClass.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
constexpr TCHAR BehaviorTreeSection[] = TEXT("behavior_tree");
constexpr TCHAR BlackboardSection[] = TEXT("blackboard");
constexpr TCHAR EnvironmentQuerySection[] = TEXT("environment_query");
constexpr TCHAR TaskBlueprintSection[] = TEXT("bt_task_blueprint");
constexpr TCHAR DecoratorBlueprintSection[] = TEXT("bt_decorator_blueprint");
constexpr TCHAR ServiceBlueprintSection[] = TEXT("bt_service_blueprint");
constexpr TCHAR GeneratorBlueprintSection[] = TEXT("eqs_generator_blueprint");
constexpr TCHAR ContextBlueprintSection[] = TEXT("eqs_context_blueprint");

enum class EAIFamily : uint8
{
    BehaviorTree,
    Blackboard,
    EnvironmentQuery,
    TaskBlueprint,
    DecoratorBlueprint,
    ServiceBlueprint,
    GeneratorBlueprint,
    ContextBlueprint,
};

FString StableIdentity(const FString& Seed)
{
    return FMD5::HashAnsiString(*Seed).ToLower();
}

void SetError(FUnrealMCPError& OutError, const TCHAR* Code, const TCHAR* Message)
{
    OutError = {Code, Message};
}

template <typename T>
FString EnumName(T Value)
{
    const UEnum* Enum = StaticEnum<T>();
    return Enum != nullptr ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString();
}

TSharedRef<FUnrealMCPRecord> ReferenceRecord(const UObject* Object)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("object_path"), Object != nullptr ? Object->GetPathName() : FString());
    Result->SetStringField(TEXT("class_path"),
        Object != nullptr && Object->GetClass() != nullptr
            ? Object->GetClass()->GetPathName() : FString());
    Result->SetBoolField(TEXT("resolved"), Object != nullptr);
    return Result;
}

TSharedRef<FUnrealMCPRecord> ClassReferenceRecord(const UClass* Class)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("class_path"), Class != nullptr ? Class->GetPathName() : FString());
    Result->SetBoolField(TEXT("resolved"), Class != nullptr);
    return Result;
}

bool IsAIModuleClass(const UClass* Class)
{
    return Class != nullptr && Class->GetOutermost()->GetName() == TEXT("/Script/AIModule");
}

FString SupportKind(const UClass* Class, const UClass* SupportedBlueprintBase = nullptr)
{
    if (Class == nullptr)
    {
        return TEXT("unresolved");
    }
    if (IsAIModuleClass(Class))
    {
        return TEXT("ai_module_builtin");
    }
    if (SupportedBlueprintBase != nullptr && Class->IsChildOf(SupportedBlueprintBase)
        && Cast<UBlueprint>(Class->ClassGeneratedBy) != nullptr)
    {
        return TEXT("custom_blueprint");
    }
    return TEXT("unknown_plugin_subclass");
}

const TSet<FName>& PersistedPropertyAllowlist()
{
    static const TSet<FName> Names = {
        TEXT("ActorClass"), TEXT("ActorToCheck"), TEXT("ArithmeticOperation"),
        TEXT("BasicOperation"), TEXT("BehaviorAsset"), TEXT("BlackboardKey"),
        TEXT("BlackboardKeyA"), TEXT("BlackboardKeyB"), TEXT("BoolValue"),
        TEXT("bApplyDecoratorScope"), TEXT("bCallTickOnSearchStart"),
        TEXT("bCanRunAsync"), TEXT("bDefineReferenceValue"), TEXT("bDiscardFailedItems"),
        TEXT("bInverseCondition"), TEXT("bRestartTimerOnEachActivation"),
        TEXT("bUseDefaultValue"), TEXT("CenterActor"), TEXT("ClampMaxType"),
        TEXT("ClampMinType"), TEXT("ConeDirection"), TEXT("ConeOrigin"),
        TEXT("Context"), TEXT("DefaultValue"), TEXT("DistanceTo"),
        TEXT("EnvQueryResultNormalizationOption"), TEXT("EQSQueryBlackboardKey"),
        TEXT("FilterType"), TEXT("FloatValueMax"), TEXT("FloatValueMin"),
        TEXT("FlowAbortMode"), TEXT("GeneratedItemType"), TEXT("GenerateAround"),
        TEXT("GeneratorsActionDescription"), TEXT("InjectionTag"), TEXT("Interval"),
        TEXT("ItemType"), TEXT("ListenerContext"), TEXT("MultipleContextFilterOp"),
        TEXT("MultipleContextScoreOp"), TEXT("NavDataOverrideContext"),
        TEXT("NodeName"), TEXT("NormalizationType"), TEXT("NotifyObserver"),
        TEXT("NotifyTick"), TEXT("Observed"), TEXT("OptionName"),
        TEXT("QueryContext"), TEXT("RandomDeviation"), TEXT("ReferenceValue"),
        TEXT("RunMode"), TEXT("ScoreClampMax"), TEXT("ScoreClampMin"),
        TEXT("ScoringEquation"), TEXT("ScoringFactor"), TEXT("SearchCenter"),
        TEXT("SearchRadius"), TEXT("SearchedActorClass"), TEXT("SpaceBetween"),
        TEXT("TestComment"), TEXT("TestOrder"), TEXT("TestPurpose"),
        TEXT("bDefaultValue"), TEXT("BaseClass"), TEXT("EnumName"),
        TEXT("EnumType")};
    return Names;
}

bool EncodeAllowlistedProperties(
    const UObject& Object,
    const TSharedRef<FUnrealMCPRecord>& OutProperties,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    TArray<const FProperty*> Properties;
    for (TFieldIterator<FProperty> It(Object.GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property != nullptr && PersistedPropertyAllowlist().Contains(Property->GetFName())
            && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            Properties.Add(Property);
        }
    }
    Properties.Sort([](const FProperty& Left, const FProperty& Right)
    {
        return Left.GetName() < Right.GetName();
    });
    if (Properties.Num() > UnrealMCPAI::MaxPersistedProperties)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("AI persisted properties exceed the output bound"));
        return false;
    }
    const UObject* Defaults = Object.GetClass()->GetDefaultObject(false);
    for (const FProperty* Property : Properties)
    {
        FString Exported;
        Property->ExportText_InContainer(0, Exported, &Object, Defaults,
            const_cast<UObject*>(&Object), PPF_None);
        if (Exported.Len() * sizeof(TCHAR) > UnrealMCPAI::MaxExportedPropertyBytes)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("An AI property exceeds the encoded-value bound"));
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        Value->SetStringField(TEXT("type"), Property->GetCPPType());
        Value->SetStringField(TEXT("value"), Exported);
        Value->SetStringField(TEXT("source"),
            Defaults != nullptr && Property->Identical_InContainer(&Object, Defaults)
                ? TEXT("default") : TEXT("local"));
        OutProperties->SetObjectField(Property->GetName(), Value);
        OutFingerprintMaterial += Property->GetName() + TEXT("=") + Exported + TEXT(";");
    }
    return true;
}

bool BuildBlackboardSelectors(
    const UObject& Object,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutSelectors,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    TArray<const FStructProperty*> Properties;
    for (TFieldIterator<FStructProperty> It(Object.GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        if (It->Struct == FBlackboardKeySelector::StaticStruct()
            && !It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            Properties.Add(*It);
        }
    }
    Properties.Sort([](const FStructProperty& Left, const FStructProperty& Right)
    {
        return Left.GetName() < Right.GetName();
    });
    if (Properties.Num() > UnrealMCPAI::MaxSelectorsPerObject)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("AI Blackboard selectors exceed the per-object bound"));
        return false;
    }
    for (const FStructProperty* Property : Properties)
    {
        const FBlackboardKeySelector* Selector =
            Property->ContainerPtrToValuePtr<FBlackboardKeySelector>(&Object);
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("property"), Property->GetName());
        Record->SetStringField(TEXT("selected_key_name"),
            Selector != nullptr ? Selector->SelectedKeyName.ToString() : FString());
        Record->SetNumberField(TEXT("selected_key_id"),
            Selector != nullptr ? static_cast<int32>(Selector->GetSelectedKeyID()) : -1);
        Record->SetStringField(TEXT("selected_key_type"),
            Selector != nullptr && Selector->SelectedKeyType != nullptr
                ? Selector->SelectedKeyType->GetPathName() : FString());
        Record->SetBoolField(TEXT("none_selected"), Selector != nullptr && Selector->IsNone());
        OutSelectors.Add(MakeShared<FUnrealMCPValueObject>(Record));
        OutFingerprintMaterial += Property->GetName() + TEXT(":")
            + (Selector != nullptr ? Selector->SelectedKeyName.ToString() : FString()) + TEXT(";");
    }
    return true;
}

bool BuildContextClasses(
    const UObject& Object,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutContexts,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    TArray<const FClassProperty*> Properties;
    for (TFieldIterator<FClassProperty> It(Object.GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        if (It->MetaClass != nullptr && It->MetaClass->IsChildOf(UEnvQueryContext::StaticClass())
            && !It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            Properties.Add(*It);
        }
    }
    Properties.Sort([](const FClassProperty& Left, const FClassProperty& Right)
    {
        return Left.GetName() < Right.GetName();
    });
    if (Properties.Num() > UnrealMCPAI::MaxContextsPerObject)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("EQS contexts exceed the per-object bound"));
        return false;
    }
    for (const FClassProperty* Property : Properties)
    {
        const UClass* ContextClass = Cast<UClass>(Property->GetObjectPropertyValue_InContainer(&Object));
        const FString Kind = SupportKind(ContextClass, UEnvQueryContext_BlueprintBase::StaticClass());
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("property"), Property->GetName());
        Record->SetObjectField(TEXT("context_class"), ClassReferenceRecord(ContextClass));
        Record->SetStringField(TEXT("support_kind"), Kind);
        Record->SetBoolField(TEXT("supported"), Kind != TEXT("unknown_plugin_subclass")
            && Kind != TEXT("unresolved"));
        if (const UBlueprint* Blueprint = ContextClass != nullptr
            ? Cast<UBlueprint>(ContextClass->ClassGeneratedBy) : nullptr)
        {
            Record->SetStringField(TEXT("blueprint_asset"), Blueprint->GetPathName());
        }
        OutContexts.Add(MakeShared<FUnrealMCPValueObject>(Record));
        OutFingerprintMaterial += Property->GetName() + TEXT(":")
            + (ContextClass != nullptr ? ContextClass->GetPathName() : FString()) + TEXT(";");
    }
    return true;
}

struct FTreeBuildState
{
    const UBehaviorTree& Tree;
    TMap<const UObject*, const UEdGraphNode*> EditorNodes;
    TMap<const UBTNode*, FString> NodeIds;
    TSet<const UBTNode*> ActiveNodes;
    TArray<TSharedPtr<FUnrealMCPValue>> Nodes;
    TArray<TSharedPtr<FUnrealMCPValue>> ChildEdges;
    TArray<TSharedPtr<FUnrealMCPValue>> Diagnostics;
    FString FingerprintMaterial;
    FUnrealMCPError& Error;
};

void CollectEditorNode(UAIGraphNode* GraphNode, TMap<const UObject*, const UEdGraphNode*>& OutNodes)
{
    if (GraphNode == nullptr)
    {
        return;
    }
    if (GraphNode->NodeInstance != nullptr)
    {
        OutNodes.FindOrAdd(GraphNode->NodeInstance, GraphNode);
    }
    for (UAIGraphNode* SubNode : GraphNode->SubNodes)
    {
        CollectEditorNode(SubNode, OutNodes);
    }
}

bool AddDiagnostic(FTreeBuildState& State, const FString& Code, const FString& Detail)
{
    if (State.Diagnostics.Num() >= UnrealMCPAI::MaxDiagnostics)
    {
        SetError(State.Error, TEXT("response_too_large"),
            TEXT("Behavior Tree diagnostics exceed the output bound"));
        return false;
    }
    const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
    Record->SetStringField(TEXT("code"), Code);
    Record->SetStringField(TEXT("detail"), Detail);
    State.Diagnostics.Add(MakeShared<FUnrealMCPValueObject>(Record));
    State.FingerprintMaterial += TEXT("diagnostic:") + Code + TEXT(":") + Detail + TEXT(";");
    return true;
}

bool AppendTreeNode(
    FTreeBuildState& State,
    UBTNode* Node,
    const FString& StructuralPath,
    const FString& ParentId,
    const FString& Relation,
    int32 ChildIndex,
    int32 Depth,
    FString& OutId)
{
    if (Node == nullptr)
    {
        OutId.Reset();
        return AddDiagnostic(State, TEXT("unresolved_node"), StructuralPath);
    }
    if (const FString* Existing = State.NodeIds.Find(Node))
    {
        OutId = *Existing;
        return AddDiagnostic(State, TEXT("cycle_or_reused_node"), StructuralPath);
    }
    if (Depth > UnrealMCPAI::MaxTreeDepth || State.Nodes.Num() >= UnrealMCPAI::MaxTreeNodes)
    {
        SetError(State.Error, TEXT("response_too_large"),
            TEXT("Behavior Tree topology exceeds its node or depth bound"));
        return false;
    }
    OutId = StableIdentity(State.Tree.GetPathName() + TEXT("|") + StructuralPath);
    State.NodeIds.Add(Node, OutId);
    const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
    Record->SetStringField(TEXT("node_id"), OutId);
    Record->SetStringField(TEXT("parent_id"), ParentId);
    Record->SetStringField(TEXT("relation"), Relation);
    Record->SetNumberField(TEXT("child_index"), ChildIndex);
    Record->SetNumberField(TEXT("structural_depth"), Depth);
    Record->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
    Record->SetStringField(TEXT("node_name"), Node->GetNodeName());
    Record->SetStringField(TEXT("static_description"), Node->GetStaticDescription());
    Record->SetNumberField(TEXT("execution_index"), Node->GetExecutionIndex());
    Record->SetNumberField(TEXT("compiled_tree_depth"), Node->GetTreeDepth());
    FString Kind = TEXT("node");
    const UClass* BlueprintBase = nullptr;
    if (Cast<UBTCompositeNode>(Node) != nullptr) Kind = TEXT("composite");
    else if (Cast<UBTTaskNode>(Node) != nullptr)
    {
        Kind = TEXT("task");
        BlueprintBase = UBTTask_BlueprintBase::StaticClass();
    }
    else if (Cast<UBTDecorator>(Node) != nullptr)
    {
        Kind = TEXT("decorator");
        BlueprintBase = UBTDecorator_BlueprintBase::StaticClass();
    }
    else if (Cast<UBTService>(Node) != nullptr)
    {
        Kind = TEXT("service");
        BlueprintBase = UBTService_BlueprintBase::StaticClass();
    }
    Record->SetStringField(TEXT("kind"), Kind);
    const FString NodeSupport = SupportKind(Node->GetClass(), BlueprintBase);
    Record->SetStringField(TEXT("support_kind"), NodeSupport);
    Record->SetBoolField(TEXT("supported"), NodeSupport != TEXT("unknown_plugin_subclass"));
    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Node->GetClass()->ClassGeneratedBy))
    {
        Record->SetStringField(TEXT("blueprint_asset"), Blueprint->GetPathName());
    }
    if (const UBTDecorator* Decorator = Cast<UBTDecorator>(Node))
    {
        Record->SetStringField(TEXT("flow_abort_mode"), EnumName(Decorator->GetFlowAbortMode()));
        Record->SetBoolField(TEXT("condition_inversed"), Decorator->IsInversed());
    }
    const UEdGraphNode* EditorNode = State.EditorNodes.FindRef(Node);
    const TSharedRef<FUnrealMCPRecord> Editor = MakeShared<FUnrealMCPRecord>();
    Editor->SetBoolField(TEXT("present"), EditorNode != nullptr);
    if (EditorNode != nullptr)
    {
        Editor->SetNumberField(TEXT("x"), EditorNode->NodePosX);
        Editor->SetNumberField(TEXT("y"), EditorNode->NodePosY);
        Editor->SetStringField(TEXT("comment"), EditorNode->NodeComment);
    }
    Record->SetObjectField(TEXT("editor"), Editor);
    const TSharedRef<FUnrealMCPRecord> Properties = MakeShared<FUnrealMCPRecord>();
    TArray<TSharedPtr<FUnrealMCPValue>> Selectors;
    if (!EncodeAllowlistedProperties(*Node, Properties, State.FingerprintMaterial, State.Error)
        || !BuildBlackboardSelectors(*Node, Selectors, State.FingerprintMaterial, State.Error))
    {
        return false;
    }
    Record->SetObjectField(TEXT("persisted_properties"), Properties);
    Record->SetArrayField(TEXT("blackboard_selectors"), Selectors);
    State.Nodes.Add(MakeShared<FUnrealMCPValueObject>(Record));
    State.FingerprintMaterial += StructuralPath + TEXT("|") + Node->GetClass()->GetPathName()
        + TEXT("|") + Node->GetNodeName() + TEXT(";");
    return true;
}

bool WalkComposite(
    FTreeBuildState& State,
    UBTCompositeNode* Composite,
    const FString& StructuralPath,
    const FString& ParentId,
    const FString& Relation,
    int32 ChildIndex,
    int32 Depth,
    FString& OutId)
{
    if (!AppendTreeNode(State, Composite, StructuralPath, ParentId, Relation,
            ChildIndex, Depth, OutId) || Composite == nullptr)
    {
        return Composite == nullptr ? State.Error.Code.IsEmpty() : false;
    }
    if (State.ActiveNodes.Contains(Composite))
    {
        return AddDiagnostic(State, TEXT("composite_cycle"), StructuralPath);
    }
    State.ActiveNodes.Add(Composite);
    for (int32 ServiceIndex = 0; ServiceIndex < Composite->Services.Num(); ++ServiceIndex)
    {
        FString ServiceId;
        if (!AppendTreeNode(State, Composite->Services[ServiceIndex],
                StructuralPath + FString::Printf(TEXT("/service:%d"), ServiceIndex),
                OutId, TEXT("service"), ServiceIndex, Depth + 1, ServiceId))
        {
            State.ActiveNodes.Remove(Composite);
            return false;
        }
    }
    for (int32 Index = 0; Index < Composite->Children.Num(); ++Index)
    {
        const FBTCompositeChild& Child = Composite->Children[Index];
        TArray<TSharedPtr<FUnrealMCPValue>> DecoratorIds;
        for (int32 DecoratorIndex = 0; DecoratorIndex < Child.Decorators.Num(); ++DecoratorIndex)
        {
            FString DecoratorId;
            if (!AppendTreeNode(State, Child.Decorators[DecoratorIndex],
                    StructuralPath + FString::Printf(TEXT("/child:%d/decorator:%d"), Index, DecoratorIndex),
                    OutId, TEXT("decorator"), DecoratorIndex, Depth + 1, DecoratorId))
            {
                State.ActiveNodes.Remove(Composite);
                return false;
            }
            DecoratorIds.Add(MakeShared<FUnrealMCPValueString>(DecoratorId));
        }
        FString ChildId;
        FString ChildKind;
        const FString ChildPath = StructuralPath + FString::Printf(TEXT("/child:%d"), Index);
        if (Child.ChildComposite != nullptr && Child.ChildTask != nullptr)
        {
            if (!AddDiagnostic(State, TEXT("ambiguous_child"), ChildPath))
            {
                State.ActiveNodes.Remove(Composite);
                return false;
            }
        }
        if (Child.ChildComposite != nullptr)
        {
            ChildKind = TEXT("composite");
            if (!WalkComposite(State, Child.ChildComposite, ChildPath + TEXT("/composite"),
                    OutId, TEXT("child"), Index, Depth + 1, ChildId))
            {
                State.ActiveNodes.Remove(Composite);
                return false;
            }
        }
        else if (Child.ChildTask != nullptr)
        {
            ChildKind = TEXT("task");
            if (!AppendTreeNode(State, Child.ChildTask, ChildPath + TEXT("/task"),
                    OutId, TEXT("child"), Index, Depth + 1, ChildId))
            {
                State.ActiveNodes.Remove(Composite);
                return false;
            }
        }
        else
        {
            ChildKind = TEXT("unresolved");
            if (!AddDiagnostic(State, TEXT("missing_child_node"), ChildPath))
            {
                State.ActiveNodes.Remove(Composite);
                return false;
            }
        }
        const TSharedRef<FUnrealMCPRecord> Edge = MakeShared<FUnrealMCPRecord>();
        Edge->SetStringField(TEXT("parent_id"), OutId);
        Edge->SetStringField(TEXT("child_id"), ChildId);
        Edge->SetNumberField(TEXT("child_index"), Index);
        Edge->SetStringField(TEXT("child_kind"), ChildKind);
        Edge->SetArrayField(TEXT("decorator_ids"), DecoratorIds);
        TArray<TSharedPtr<FUnrealMCPValue>> Logic;
        for (const FBTDecoratorLogic& Operation : Child.DecoratorOps)
        {
            const TSharedRef<FUnrealMCPRecord> LogicRecord = MakeShared<FUnrealMCPRecord>();
            LogicRecord->SetStringField(TEXT("operation"), EnumName(Operation.Operation.GetValue()));
            LogicRecord->SetNumberField(TEXT("number"), Operation.Number);
            Logic.Add(MakeShared<FUnrealMCPValueObject>(LogicRecord));
            State.FingerprintMaterial += EnumName(Operation.Operation.GetValue())
                + TEXT(":") + LexToString(Operation.Number) + TEXT(";");
        }
        Edge->SetArrayField(TEXT("decorator_logic"), Logic);
        State.ChildEdges.Add(MakeShared<FUnrealMCPValueObject>(Edge));
    }
    State.ActiveNodes.Remove(Composite);
    return true;
}

bool BuildBehaviorTreeBlock(
    const UBehaviorTree& Tree,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    FTreeBuildState State{Tree, {}, {}, {}, {}, {}, {}, Tree.GetPathName(), OutError};
#if WITH_EDITORONLY_DATA
    if (Tree.BTGraph != nullptr)
    {
        for (UEdGraphNode* GraphNode : Tree.BTGraph->Nodes)
        {
            CollectEditorNode(Cast<UAIGraphNode>(GraphNode), State.EditorNodes);
        }
    }
#endif
    OutBlock->SetObjectField(TEXT("blackboard"), ReferenceRecord(Tree.BlackboardAsset));
    OutBlock->SetBoolField(TEXT("root_resolved"), Tree.RootNode != nullptr);
    FString RootId;
    if (Tree.RootNode == nullptr)
    {
        if (!AddDiagnostic(State, TEXT("missing_root"), Tree.GetPathName()))
        {
            return false;
        }
    }
    else if (!WalkComposite(State, Tree.RootNode, TEXT("root"), FString(), TEXT("root"),
            0, 0, RootId))
    {
        return false;
    }
    TArray<TSharedPtr<FUnrealMCPValue>> RootDecoratorIds;
    for (int32 Index = 0; Index < Tree.RootDecorators.Num(); ++Index)
    {
        FString DecoratorId;
        if (!AppendTreeNode(State, Tree.RootDecorators[Index],
                FString::Printf(TEXT("root_decorator:%d"), Index), RootId,
                TEXT("root_decorator"), Index, 1, DecoratorId))
        {
            return false;
        }
        RootDecoratorIds.Add(MakeShared<FUnrealMCPValueString>(DecoratorId));
    }
    TArray<TSharedPtr<FUnrealMCPValue>> RootLogic;
    for (const FBTDecoratorLogic& Operation : Tree.RootDecoratorOps)
    {
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("operation"), EnumName(Operation.Operation.GetValue()));
        Record->SetNumberField(TEXT("number"), Operation.Number);
        RootLogic.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    OutBlock->SetStringField(TEXT("root_node_id"), RootId);
    OutBlock->SetArrayField(TEXT("root_decorator_ids"), RootDecoratorIds);
    OutBlock->SetArrayField(TEXT("root_decorator_logic"), RootLogic);
    OutBlock->SetArrayField(TEXT("nodes"), State.Nodes);
    OutBlock->SetArrayField(TEXT("child_edges"), State.ChildEdges);
    OutBlock->SetArrayField(TEXT("diagnostics"), State.Diagnostics);
    OutBlock->SetNumberField(TEXT("node_count"), State.Nodes.Num());
    OutBlock->SetBoolField(TEXT("runtime_state_excluded"), true);
    OutFingerprint = StableIdentity(State.FingerprintMaterial);
    return true;
}

bool BuildBlackboardBlock(
    const UBlackboardData& Blackboard,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    TArray<const UBlackboardData*> Chain;
    TSet<const UBlackboardData*> Seen;
    const UBlackboardData* Current = &Blackboard;
    bool bParentCycle = false;
    while (Current != nullptr)
    {
        if (Seen.Contains(Current))
        {
            bParentCycle = true;
            break;
        }
        if (Chain.Num() >= UnrealMCPAI::MaxParentDepth)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("Blackboard parent traversal exceeds the depth bound"));
            return false;
        }
        Seen.Add(Current);
        Chain.Add(Current);
        Current = Current->Parent;
    }
    Algo::Reverse(Chain);
    TArray<TSharedPtr<FUnrealMCPValue>> Parents;
    TArray<TSharedPtr<FUnrealMCPValue>> Keys;
    TSet<FName> KeyNames;
    FString Material = Blackboard.GetPathName();
    int32 KeyId = 0;
    for (const UBlackboardData* Source : Chain)
    {
        Parents.Add(MakeShared<FUnrealMCPValueObject>(ReferenceRecord(Source)));
        Material += Source->GetPathName() + TEXT(";");
        for (int32 LocalIndex = 0; LocalIndex < Source->Keys.Num(); ++LocalIndex)
        {
            if (KeyId >= UnrealMCPAI::MaxBlackboardKeys)
            {
                SetError(OutError, TEXT("response_too_large"),
                    TEXT("Blackboard keys exceed the output bound"));
                return false;
            }
            const FBlackboardEntry& Entry = Source->Keys[LocalIndex];
            const bool bShadowed = KeyNames.Contains(Entry.EntryName);
            KeyNames.Add(Entry.EntryName);
            const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
            Record->SetNumberField(TEXT("key_id"), KeyId++);
            Record->SetNumberField(TEXT("local_index"), LocalIndex);
            Record->SetStringField(TEXT("name"), Entry.EntryName.ToString());
#if WITH_EDITORONLY_DATA
            Record->SetStringField(TEXT("description"), Entry.EntryDescription);
            Record->SetStringField(TEXT("category"), Entry.EntryCategory.ToString());
#endif
            Record->SetStringField(TEXT("source_asset"), Source->GetPathName());
            Record->SetBoolField(TEXT("inherited"), Source != &Blackboard);
            Record->SetBoolField(TEXT("duplicate_or_shadowed"), bShadowed);
            Record->SetBoolField(TEXT("instance_synchronized"), Entry.bInstanceSynced != 0);
            Record->SetObjectField(TEXT("key_type"), ReferenceRecord(Entry.KeyType));
            const FString KeySupport = Entry.KeyType != nullptr
                ? SupportKind(Entry.KeyType->GetClass()) : TEXT("unresolved");
            Record->SetStringField(TEXT("support_kind"), KeySupport);
            Record->SetBoolField(TEXT("supported"), KeySupport == TEXT("ai_module_builtin"));
            const TSharedRef<FUnrealMCPRecord> Settings = MakeShared<FUnrealMCPRecord>();
            if (Entry.KeyType != nullptr
                && !EncodeAllowlistedProperties(*Entry.KeyType, Settings, Material, OutError))
            {
                return false;
            }
            Record->SetObjectField(TEXT("type_settings"), Settings);
            Keys.Add(MakeShared<FUnrealMCPValueObject>(Record));
            Material += Entry.EntryName.ToString() + TEXT("|")
                + (Entry.KeyType != nullptr ? Entry.KeyType->GetClass()->GetPathName() : FString())
                + LexToString(Entry.bInstanceSynced != 0) + TEXT(";");
        }
    }
    OutBlock->SetObjectField(TEXT("parent"), ReferenceRecord(Blackboard.Parent));
    OutBlock->SetArrayField(TEXT("parent_chain"), Parents);
    OutBlock->SetArrayField(TEXT("keys"), Keys);
    OutBlock->SetNumberField(TEXT("key_count"), Keys.Num());
    OutBlock->SetBoolField(TEXT("parent_cycle_detected"), bParentCycle);
    OutBlock->SetBoolField(TEXT("schema_valid"), !bParentCycle && Blackboard.IsValid());
    OutBlock->SetBoolField(TEXT("runtime_values_excluded"), true);
    OutFingerprint = StableIdentity(Material + LexToString(bParentCycle));
    OutBlock->SetStringField(TEXT("schema_snapshot"), OutFingerprint);
    return true;
}

bool BuildQueryObjectRecord(
    const UObject* Object,
    const UClass* SupportedBlueprintBase,
    const FString& IdentitySeed,
    const FString& Kind,
    int32 Index,
    const TSharedRef<FUnrealMCPRecord>& OutRecord,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    OutRecord->SetStringField(TEXT("node_id"), StableIdentity(IdentitySeed));
    OutRecord->SetStringField(TEXT("kind"), Kind);
    OutRecord->SetNumberField(TEXT("index"), Index);
    OutRecord->SetBoolField(TEXT("resolved"), Object != nullptr);
    if (Object == nullptr)
    {
        OutRecord->SetStringField(TEXT("support_kind"), TEXT("unresolved"));
        OutRecord->SetBoolField(TEXT("supported"), false);
        OutFingerprintMaterial += IdentitySeed + TEXT(":null;");
        return true;
    }
    const UClass* Class = Object->GetClass();
    const FString KindValue = SupportKind(Class, SupportedBlueprintBase);
    OutRecord->SetStringField(TEXT("class_path"), Class->GetPathName());
    OutRecord->SetStringField(TEXT("support_kind"), KindValue);
    OutRecord->SetBoolField(TEXT("supported"), KindValue != TEXT("unknown_plugin_subclass"));
    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
    {
        OutRecord->SetStringField(TEXT("blueprint_asset"), Blueprint->GetPathName());
    }
    const TSharedRef<FUnrealMCPRecord> Properties = MakeShared<FUnrealMCPRecord>();
    TArray<TSharedPtr<FUnrealMCPValue>> Selectors;
    TArray<TSharedPtr<FUnrealMCPValue>> Contexts;
    if (!EncodeAllowlistedProperties(*Object, Properties, OutFingerprintMaterial, OutError)
        || !BuildBlackboardSelectors(*Object, Selectors, OutFingerprintMaterial, OutError)
        || !BuildContextClasses(*Object, Contexts, OutFingerprintMaterial, OutError))
    {
        return false;
    }
    OutRecord->SetObjectField(TEXT("persisted_properties"), Properties);
    OutRecord->SetArrayField(TEXT("blackboard_selectors"), Selectors);
    OutRecord->SetArrayField(TEXT("contexts"), Contexts);
    OutFingerprintMaterial += IdentitySeed + TEXT(":") + Class->GetPathName() + TEXT(";");
    return true;
}

bool BuildEnvironmentQueryBlock(
    const UEnvQuery& Query,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    const TArray<UEnvQueryOption*>& Options = Query.GetOptions();
    if (Options.Num() > UnrealMCPAI::MaxQueryOptions)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("EQS options exceed the output bound"));
        return false;
    }
    FString Material = Query.GetPathName() + Query.GetQueryName().ToString();
    TArray<TSharedPtr<FUnrealMCPValue>> OptionValues;
    int32 TotalTests = 0;
    for (int32 OptionIndex = 0; OptionIndex < Options.Num(); ++OptionIndex)
    {
        const UEnvQueryOption* Option = Options[OptionIndex];
        const TSharedRef<FUnrealMCPRecord> OptionRecord = MakeShared<FUnrealMCPRecord>();
        const FString OptionSeed = Query.GetPathName()
            + FString::Printf(TEXT("|option:%d"), OptionIndex);
        OptionRecord->SetStringField(TEXT("option_id"), StableIdentity(OptionSeed));
        OptionRecord->SetNumberField(TEXT("index"), OptionIndex);
        OptionRecord->SetBoolField(TEXT("resolved"), Option != nullptr);
        TArray<TSharedPtr<FUnrealMCPValue>> Tests;
        if (Option != nullptr)
        {
            const TSharedRef<FUnrealMCPRecord> Generator = MakeShared<FUnrealMCPRecord>();
            if (!BuildQueryObjectRecord(Option->Generator,
                    UEnvQueryGenerator_BlueprintBase::StaticClass(),
                    OptionSeed + TEXT("|generator"), TEXT("generator"), 0,
                    Generator, Material, OutError))
            {
                return false;
            }
            if (Option->Generator != nullptr)
            {
                Generator->SetStringField(TEXT("option_name"), Option->Generator->OptionName);
                Generator->SetObjectField(TEXT("item_type"),
                    ClassReferenceRecord(Option->Generator->ItemType));
                Generator->SetBoolField(TEXT("auto_sort_tests"), Option->Generator->bAutoSortTests != 0);
                Generator->SetBoolField(TEXT("can_run_async"), Option->Generator->CanRunAsync());
            }
            OptionRecord->SetObjectField(TEXT("generator"), Generator);
            TotalTests += Option->Tests.Num();
            if (TotalTests > UnrealMCPAI::MaxQueryTests)
            {
                SetError(OutError, TEXT("response_too_large"),
                    TEXT("EQS tests exceed the output bound"));
                return false;
            }
            for (int32 TestIndex = 0; TestIndex < Option->Tests.Num(); ++TestIndex)
            {
                const UEnvQueryTest* Test = Option->Tests[TestIndex];
                const TSharedRef<FUnrealMCPRecord> TestRecord = MakeShared<FUnrealMCPRecord>();
                if (!BuildQueryObjectRecord(Test, nullptr,
                        OptionSeed + FString::Printf(TEXT("|test:%d"), TestIndex),
                        TEXT("test"), TestIndex, TestRecord, Material, OutError))
                {
                    return false;
                }
                if (Test != nullptr)
                {
                    TestRecord->SetNumberField(TEXT("test_order"), Test->TestOrder);
                    TestRecord->SetStringField(TEXT("purpose"), EnumName(Test->TestPurpose.GetValue()));
                    TestRecord->SetStringField(TEXT("filter_type"), EnumName(Test->FilterType.GetValue()));
                    TestRecord->SetStringField(TEXT("scoring_equation"),
                        EnumName(Test->ScoringEquation.GetValue()));
                    TestRecord->SetStringField(TEXT("multiple_context_filter"),
                        EnumName(Test->MultipleContextFilterOp.GetValue()));
                    TestRecord->SetStringField(TEXT("multiple_context_score"),
                        EnumName(Test->MultipleContextScoreOp.GetValue()));
                    TestRecord->SetBoolField(TEXT("filters"), Test->IsFiltering());
                    TestRecord->SetBoolField(TEXT("scores"), Test->IsScoring());
                    TestRecord->SetBoolField(TEXT("works_on_float_values"),
                        Test->GetWorkOnFloatValues());
                    TestRecord->SetStringField(TEXT("comment"), Test->TestComment);
                }
                Tests.Add(MakeShared<FUnrealMCPValueObject>(TestRecord));
            }
        }
        OptionRecord->SetArrayField(TEXT("tests"), Tests);
        OptionRecord->SetNumberField(TEXT("test_count"), Tests.Num());
        OptionValues.Add(MakeShared<FUnrealMCPValueObject>(OptionRecord));
        Material += OptionSeed + TEXT(";");
    }
    OutBlock->SetStringField(TEXT("query_name"), Query.GetQueryName().ToString());
    OutBlock->SetBoolField(TEXT("strip_from_client_builds"), Query.bStripFromClientBuilds != 0);
    OutBlock->SetArrayField(TEXT("options"), OptionValues);
    OutBlock->SetNumberField(TEXT("option_count"), OptionValues.Num());
    OutBlock->SetNumberField(TEXT("test_count"), TotalTests);
    OutBlock->SetBoolField(TEXT("runtime_execution_excluded"), true);
    OutBlock->SetBoolField(TEXT("debugger_state_excluded"), true);
    OutFingerprint = StableIdentity(Material + LexToString(Query.bStripFromClientBuilds != 0));
    return true;
}

UObject* ResolveBlueprintDefaults(UObject* Asset, const UClass* RequiredBase)
{
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    UClass* GeneratedClass = Blueprint != nullptr
        ? (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass)
        : nullptr;
    return GeneratedClass != nullptr && GeneratedClass->IsChildOf(RequiredBase)
        ? GeneratedClass->GetDefaultObject(false) : nullptr;
}

bool BuildBlueprintBlock(
    UBlueprint& Blueprint,
    const UClass* RequiredBase,
    const TArray<FName>& SupportedFunctions,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    UObject* Defaults = ResolveBlueprintDefaults(&Blueprint, RequiredBase);
    if (Defaults == nullptr)
    {
        SetError(OutError, TEXT("invalid_asset"),
            TEXT("The Blueprint does not represent the selected AI class"));
        return false;
    }
    OutBlock->SetStringField(TEXT("generated_class"), Defaults->GetClass()->GetPathName());
    OutBlock->SetStringField(TEXT("native_base"), RequiredBase->GetPathName());
    OutBlock->SetBoolField(TEXT("ordinary_blueprint_semantics_composed"), true);
    OutBlock->SetBoolField(TEXT("runtime_execution_excluded"), true);
    TArray<TSharedPtr<FUnrealMCPValue>> Functions;
    FString Material = Blueprint.GetPathName() + Defaults->GetClass()->GetPathName();
    for (const FName FunctionName : SupportedFunctions)
    {
        const UFunction* Function = Defaults->GetClass()->FindFunctionByName(FunctionName);
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("name"), FunctionName.ToString());
        Record->SetBoolField(TEXT("available"), Function != nullptr);
        Record->SetBoolField(TEXT("implemented_by_blueprint"),
            Function != nullptr && Function->GetOuterUClass() == Defaults->GetClass());
        Functions.Add(MakeShared<FUnrealMCPValueObject>(Record));
        Material += FunctionName.ToString()
            + LexToString(Function != nullptr && Function->GetOuterUClass() == Defaults->GetClass());
    }
    OutBlock->SetArrayField(TEXT("supported_events"), Functions);
    const TSharedRef<FUnrealMCPRecord> Properties = MakeShared<FUnrealMCPRecord>();
    TArray<TSharedPtr<FUnrealMCPValue>> Selectors;
    TArray<TSharedPtr<FUnrealMCPValue>> Contexts;
    if (!EncodeAllowlistedProperties(*Defaults, Properties, Material, OutError)
        || !BuildBlackboardSelectors(*Defaults, Selectors, Material, OutError)
        || !BuildContextClasses(*Defaults, Contexts, Material, OutError))
    {
        return false;
    }
    OutBlock->SetObjectField(TEXT("ai_defaults"), Properties);
    OutBlock->SetArrayField(TEXT("blackboard_selectors"), Selectors);
    OutBlock->SetArrayField(TEXT("contexts"), Contexts);
    OutBlock->SetBoolField(TEXT("custom_defaults_reported_by_base_blueprint"), true);
    OutFingerprint = StableIdentity(Material);
    return true;
}

FString SectionName(EAIFamily Family)
{
    switch (Family)
    {
    case EAIFamily::BehaviorTree: return BehaviorTreeSection;
    case EAIFamily::Blackboard: return BlackboardSection;
    case EAIFamily::EnvironmentQuery: return EnvironmentQuerySection;
    case EAIFamily::TaskBlueprint: return TaskBlueprintSection;
    case EAIFamily::DecoratorBlueprint: return DecoratorBlueprintSection;
    case EAIFamily::ServiceBlueprint: return ServiceBlueprintSection;
    case EAIFamily::GeneratorBlueprint: return GeneratorBlueprintSection;
    case EAIFamily::ContextBlueprint: return ContextBlueprintSection;
    }
    return FString();
}

bool BuildBlock(
    UObject* Asset,
    EAIFamily Family,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    switch (Family)
    {
    case EAIFamily::BehaviorTree:
        if (const UBehaviorTree* Tree = Cast<UBehaviorTree>(Asset))
            return BuildBehaviorTreeBlock(*Tree, OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::Blackboard:
        if (const UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
            return BuildBlackboardBlock(*Blackboard, OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::EnvironmentQuery:
        if (const UEnvQuery* Query = Cast<UEnvQuery>(Asset))
            return BuildEnvironmentQueryBlock(*Query, OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::TaskBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint, UBTTask_BlueprintBase::StaticClass(),
                {TEXT("ReceiveExecute"), TEXT("ReceiveExecuteAI"), TEXT("ReceiveAbort"),
                    TEXT("ReceiveAbortAI"), TEXT("ReceiveTick"), TEXT("ReceiveTickAI")},
                OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::DecoratorBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint, UBTDecorator_BlueprintBase::StaticClass(),
                {TEXT("ReceiveConditionCheck"), TEXT("ReceiveConditionCheckAI"),
                    TEXT("ReceiveExecutionStart"), TEXT("ReceiveExecutionFinish"),
                    TEXT("ReceiveObserverActivated"), TEXT("ReceiveObserverDeactivated"),
                    TEXT("ReceiveTick"), TEXT("ReceiveTickAI")},
                OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::ServiceBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint, UBTService_BlueprintBase::StaticClass(),
                {TEXT("ReceiveTick"), TEXT("ReceiveTickAI"), TEXT("ReceiveSearchStart"),
                    TEXT("ReceiveSearchStartAI"), TEXT("ReceiveActivation"),
                    TEXT("ReceiveActivationAI"), TEXT("ReceiveDeactivation"),
                    TEXT("ReceiveDeactivationAI")}, OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::GeneratorBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint,
                UEnvQueryGenerator_BlueprintBase::StaticClass(),
                {TEXT("DoItemGeneration"), TEXT("DoItemGenerationFromActors")},
                OutBlock, OutFingerprint, OutError);
        break;
    case EAIFamily::ContextBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint, UEnvQueryContext_BlueprintBase::StaticClass(),
                {TEXT("ProvideSingleActor"), TEXT("ProvideSingleLocation"),
                    TEXT("ProvideActorsSet"), TEXT("ProvideLocationsSet")},
                OutBlock, OutFingerprint, OutError);
        break;
    }
    SetError(OutError, TEXT("invalid_asset"),
        TEXT("The asset does not represent the selected AI family"));
    return false;
}

class FAIInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    explicit FAIInspectionAdapter(EAIFamily InFamily) : Family(InFamily) {}

    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        if (Context.bHasPaging || Context.bHasPartialGraphFlag)
        {
            SetError(OutError, TEXT("invalid_argument"),
                TEXT("AI semantic selectors do not support paging or graph flags"));
            return false;
        }
        const FString Section = SectionName(Family);
        if (!Selectors.Register({Section, {Section}, false, false}, OutError)
            || !Selectors.Freeze(OutError))
        {
            return false;
        }
        if (!Context.Selector.IsRoot() && Selectors.Resolve(Context.Selector, OutError) == nullptr)
        {
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        if (!BuildBlock(Context.Asset, Family, Block, Fingerprint, OutError))
        {
            return false;
        }
        if (Context.Selector.IsRoot())
        {
            if (!Document.Add({TEXT("selectors"), TEXT("array"),
                MakeShared<FUnrealMCPValueArray>(TArray<TSharedPtr<FUnrealMCPValue>>{
                    MakeShared<FUnrealMCPValueString>(Section)})}, OutError))
            {
                return false;
            }
        }
        else
        {
            const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
            Selection->SetStringField(TEXT("selector"), Section);
            Selection->SetStringField(TEXT("kind"), TEXT("record"));
            if (!Document.Add({TEXT("selection"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Selection)}, OutError))
            {
                return false;
            }
        }
        return Document.Add({Section, TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Block)}, OutError)
            && Snapshot.Add(Section, Fingerprint, OutError);
    }

private:
    EAIFamily Family;
};

FUnrealMCPCompanionAssetFamily MakeFamily(
    EAIFamily FamilyKind,
    const TCHAR* FamilyId,
    UClass* NativeClass,
    EUnrealMCPAssetFamilyClassPolicy ClassPolicy)
{
    FUnrealMCPCompanionAssetFamily Family;
    const FString Section = SectionName(FamilyKind);
    Family.FamilyId = FamilyId;
    Family.NativeClassPath = NativeClass->GetPathName();
    Family.ClassPolicy = ClassPolicy;
    Family.Priority = 230;
    Family.RequiredModules = {TEXT("AIModule"), TEXT("BehaviorTreeEditor")};
    Family.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Family.Bounds.MaxValueNodes = 65536;
    Family.Limits = {
        {TEXT("tree_nodes"), UnrealMCPAI::MaxTreeNodes},
        {TEXT("tree_depth"), UnrealMCPAI::MaxTreeDepth},
        {TEXT("blackboard_keys"), UnrealMCPAI::MaxBlackboardKeys},
        {TEXT("parent_depth"), UnrealMCPAI::MaxParentDepth},
        {TEXT("query_options"), UnrealMCPAI::MaxQueryOptions},
        {TEXT("query_tests"), UnrealMCPAI::MaxQueryTests},
        {TEXT("selectors_per_object"), UnrealMCPAI::MaxSelectorsPerObject},
        {TEXT("contexts_per_object"), UnrealMCPAI::MaxContextsPerObject},
        {TEXT("diagnostics"), UnrealMCPAI::MaxDiagnostics},
        {TEXT("persisted_properties"), UnrealMCPAI::MaxPersistedProperties},
        {TEXT("exported_property_bytes"), UnrealMCPAI::MaxExportedPropertyBytes}};
    Family.Capabilities.bInspection = true;
    Family.SelectorRoutes = {{Section, {Section}, false, false}};
    Family.StableNestedIdentityKinds = {
        TEXT("behavior_tree_node"), TEXT("behavior_tree_edge"),
        TEXT("blackboard_key"), TEXT("eqs_option"), TEXT("eqs_generator"),
        TEXT("eqs_test"), TEXT("eqs_context")};
    Family.InspectionAdapter = MakeShared<FAIInspectionAdapter>(FamilyKind);
    Family.SnapshotBuilder = [FamilyKind](UObject* Asset)
    {
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        FUnrealMCPError Error;
        return BuildBlock(Asset, FamilyKind, Block, Fingerprint, Error)
            ? Fingerprint : FString();
    };
    return Family;
}

#if WITH_DEV_AUTOMATION_TESTS
bool SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs);
}

bool RemoveFixture(const FString& PackageName)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    return !IFileManager::Get().FileExists(*Filename)
        || IFileManager::Get().Delete(*Filename, false, true);
}

void AddBoolKey(UBlackboardData& Blackboard, const FName Name, bool bSynced)
{
    FBlackboardEntry Entry;
    Entry.EntryName = Name;
    Entry.KeyType = NewObject<UBlackboardKeyType_Bool>(&Blackboard);
    Entry.bInstanceSynced = bSynced;
    Blackboard.Keys.Add(Entry);
}

void AddObjectKey(UBlackboardData& Blackboard, const FName Name)
{
    FBlackboardEntry Entry;
    Entry.EntryName = Name;
    UBlackboardKeyType_Object* Type = NewObject<UBlackboardKeyType_Object>(&Blackboard);
    Type->BaseClass = AActor::StaticClass();
    Entry.KeyType = Type;
    Blackboard.Keys.Add(Entry);
}

void ConfigureBlackboardSelector(UObject& Object, const FName PropertyName,
    const FName KeyName, const UBlackboardData& Blackboard)
{
    FStructProperty* Property = FindFProperty<FStructProperty>(Object.GetClass(), PropertyName);
    if (Property != nullptr && Property->Struct == FBlackboardKeySelector::StaticStruct())
    {
        FBlackboardKeySelector* Selector =
            Property->ContainerPtrToValuePtr<FBlackboardKeySelector>(&Object);
        Selector->SelectedKeyName = KeyName;
        Selector->ResolveSelectedKey(Blackboard);
    }
}

UBlackboardData* CreateBlackboard(UObject* Outer, const FName Name)
{
    UBlackboardData* Blackboard = NewObject<UBlackboardData>(Outer, Name);
    AddBoolKey(*Blackboard, TEXT("CanSeeTarget"), true);
    AddObjectKey(*Blackboard, TEXT("TargetActor"));
    Blackboard->UpdateKeyIDs();
    Blackboard->UpdateIfHasSynchronizedKeys();
    return Blackboard;
}

UBehaviorTree* CreateBehaviorTree(UObject* Outer, const FName Name, UBlackboardData* Blackboard)
{
    UBehaviorTree* Tree = NewObject<UBehaviorTree>(Outer, Name);
    Tree->BlackboardAsset = Blackboard;
    UBTComposite_Sequence* Root = NewObject<UBTComposite_Sequence>(Tree);
    UBTTask_Wait* Wait = NewObject<UBTTask_Wait>(Tree);
    UBTDecorator_Blackboard* Decorator = NewObject<UBTDecorator_Blackboard>(Tree);
    ConfigureBlackboardSelector(*Decorator, TEXT("BlackboardKey"),
        TEXT("CanSeeTarget"), *Blackboard);
    FBTCompositeChild Child;
    Child.ChildTask = Wait;
    Child.Decorators.Add(Decorator);
    Child.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, 0));
    Root->Children.Add(Child);
    Root->Services.Add(NewObject<UBTService_DefaultFocus>(Tree));
    Tree->RootNode = Root;
    return Tree;
}

UEnvQuery* CreateEnvironmentQuery(UObject* Outer, const FName Name)
{
    UEnvQuery* Query = NewObject<UEnvQuery>(Outer, Name);
    UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
    UEnvQueryGenerator_ActorsOfClass* Generator =
        NewObject<UEnvQueryGenerator_ActorsOfClass>(Option);
    Generator->SearchedActorClass = AActor::StaticClass();
    UEnvQueryTest_Distance* Test = NewObject<UEnvQueryTest_Distance>(Option);
    Option->Generator = Generator;
    Option->Tests.Add(Test);
    Query->GetOptionsMutable().Add(Option);
    return Query;
}

UBlueprint* CreateBlueprintAsset(
    UPackage* Package, const FName Name, UClass* ParentClass)
{
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, Name, BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (Blueprint != nullptr)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }
    return Blueprint;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAIAssetInspectionTest,
    "UnrealMCP.AI.AssetInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAIAssetInspectionTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPAIInspection"));
    UBlackboardData* Parent = NewObject<UBlackboardData>(Package, TEXT("BB_Parent"));
    AddBoolKey(*Parent, TEXT("InheritedFlag"), false);
    Parent->UpdateKeyIDs();
    UBlackboardData* Blackboard = CreateBlackboard(Package, TEXT("BB_Test"));
    Blackboard->Parent = Parent;
    Blackboard->UpdateParentKeys();
    Blackboard->UpdateKeyIDs();
    UBehaviorTree* Tree = CreateBehaviorTree(Package, TEXT("BT_Test"), Blackboard);
    UEnvQuery* Query = CreateEnvironmentQuery(Package, TEXT("EQS_Test"));
    const bool bDirtyBefore = Package->IsDirty();

    for (const TPair<UObject*, EAIFamily>& Fixture : {
        TPair<UObject*, EAIFamily>(Tree, EAIFamily::BehaviorTree),
        TPair<UObject*, EAIFamily>(Blackboard, EAIFamily::Blackboard),
        TPair<UObject*, EAIFamily>(Query, EAIFamily::EnvironmentQuery)})
    {
        const TSharedRef<FUnrealMCPRecord> First = MakeShared<FUnrealMCPRecord>();
        const TSharedRef<FUnrealMCPRecord> Second = MakeShared<FUnrealMCPRecord>();
        FString FirstFingerprint;
        FString SecondFingerprint;
        FUnrealMCPError Error;
        TestTrue(TEXT("AI semantic block builds"), BuildBlock(
            Fixture.Key, Fixture.Value, First, FirstFingerprint, Error));
        TestTrue(TEXT("Repeated AI semantic block builds"), BuildBlock(
            Fixture.Key, Fixture.Value, Second, SecondFingerprint, Error));
        TestEqual(TEXT("AI inspection is deterministic"),
            FirstFingerprint, SecondFingerprint);
        TestTrue(TEXT("AI snapshot is non-empty"), !FirstFingerprint.IsEmpty());
    }
    TestEqual(TEXT("AI inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);

    AddExpectedError(TEXT("doesn't override DoItemGeneration or DoItemGenerationFromActors"),
        EAutomationExpectedErrorFlags::Contains, 4);
    const TArray<TPair<UClass*, EAIFamily>> BlueprintFamilies = {
        {UBTTask_BlueprintBase::StaticClass(), EAIFamily::TaskBlueprint},
        {UBTDecorator_BlueprintBase::StaticClass(), EAIFamily::DecoratorBlueprint},
        {UBTService_BlueprintBase::StaticClass(), EAIFamily::ServiceBlueprint},
        {UEnvQueryGenerator_BlueprintBase::StaticClass(), EAIFamily::GeneratorBlueprint},
        {UEnvQueryContext_BlueprintBase::StaticClass(), EAIFamily::ContextBlueprint}};
    for (int32 Index = 0; Index < BlueprintFamilies.Num(); ++Index)
    {
        UBlueprint* Blueprint = CreateBlueprintAsset(Package,
            *FString::Printf(TEXT("BP_AI_%d"), Index), BlueprintFamilies[Index].Key);
        TestNotNull(TEXT("AI Blueprint fixture is created"), Blueprint);
        if (Blueprint != nullptr)
        {
            const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
            FString Fingerprint;
            FUnrealMCPError Error;
            TestTrue(TEXT("AI Blueprint semantic block builds"), BuildBlock(
                Blueprint, BlueprintFamilies[Index].Value, Block, Fingerprint, Error));
        }
    }

    UBlackboardData* Oversized = NewObject<UBlackboardData>(Package, TEXT("BB_Oversized"));
    for (int32 Index = 0; Index <= UnrealMCPAI::MaxBlackboardKeys; ++Index)
    {
        AddBoolKey(*Oversized, *FString::Printf(TEXT("Key_%d"), Index), false);
    }
    const TSharedRef<FUnrealMCPRecord> OversizedBlock = MakeShared<FUnrealMCPRecord>();
    FString OversizedFingerprint;
    FUnrealMCPError OversizedError;
    TestFalse(TEXT("Blackboard key overflow fails closed"), BuildBlackboardBlock(
        *Oversized, OversizedBlock, OversizedFingerprint, OversizedError));
    TestEqual(TEXT("Blackboard overflow uses the stable bound error"),
        OversizedError.Code, FString(TEXT("response_too_large")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAILiveFixtureTest,
    "UnrealMCP.AI.LiveFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAILiveFixtureTest::RunTest(const FString& Parameters)
{
    AddExpectedError(TEXT("doesn't override DoItemGeneration or DoItemGenerationFromActors"),
        EAutomationExpectedErrorFlags::Contains, 4);
    const TArray<FString> PackageNames = {
        TEXT("/Game/UnrealMCPAI/BB_InspectionFixture"),
        TEXT("/Game/UnrealMCPAI/BT_InspectionFixture"),
        TEXT("/Game/UnrealMCPAI/EQS_InspectionFixture"),
        TEXT("/Game/UnrealMCPAI/BP_BTTaskFixture"),
        TEXT("/Game/UnrealMCPAI/BP_BTDecoratorFixture"),
        TEXT("/Game/UnrealMCPAI/BP_BTServiceFixture"),
        TEXT("/Game/UnrealMCPAI/BP_EQSGeneratorFixture"),
        TEXT("/Game/UnrealMCPAI/BP_EQSContextFixture")};
    for (const FString& PackageName : PackageNames)
    {
        if (!TestTrue(TEXT("existing AI fixture is removed"), RemoveFixture(PackageName)))
        {
            return false;
        }
    }

    UPackage* BlackboardPackage = CreatePackage(*PackageNames[0]);
    UBlackboardData* Blackboard = CreateBlackboard(
        BlackboardPackage, TEXT("BB_InspectionFixture"));
    Blackboard->SetFlags(RF_Public | RF_Standalone);
    TestTrue(TEXT("Blackboard fixture persists"), SaveAsset(Blackboard));

    UPackage* TreePackage = CreatePackage(*PackageNames[1]);
    UBehaviorTree* Tree = CreateBehaviorTree(
        TreePackage, TEXT("BT_InspectionFixture"), Blackboard);
    Tree->SetFlags(RF_Public | RF_Standalone);
    TestTrue(TEXT("Behavior Tree fixture persists"), SaveAsset(Tree));

    UPackage* QueryPackage = CreatePackage(*PackageNames[2]);
    UEnvQuery* Query = CreateEnvironmentQuery(QueryPackage, TEXT("EQS_InspectionFixture"));
    Query->SetFlags(RF_Public | RF_Standalone);
    TestTrue(TEXT("EQS fixture persists"), SaveAsset(Query));

    const TArray<TPair<UClass*, FName>> BlueprintFamilies = {
        {UBTTask_BlueprintBase::StaticClass(), TEXT("BP_BTTaskFixture")},
        {UBTDecorator_BlueprintBase::StaticClass(), TEXT("BP_BTDecoratorFixture")},
        {UBTService_BlueprintBase::StaticClass(), TEXT("BP_BTServiceFixture")},
        {UEnvQueryGenerator_BlueprintBase::StaticClass(), TEXT("BP_EQSGeneratorFixture")},
        {UEnvQueryContext_BlueprintBase::StaticClass(), TEXT("BP_EQSContextFixture")}};
    for (int32 Index = 0; Index < BlueprintFamilies.Num(); ++Index)
    {
        UPackage* BlueprintPackage = CreatePackage(*PackageNames[Index + 3]);
        UBlueprint* Blueprint = CreateBlueprintAsset(BlueprintPackage,
            BlueprintFamilies[Index].Value, BlueprintFamilies[Index].Key);
        TestNotNull(TEXT("AI Blueprint live fixture is created"), Blueprint);
        TestTrue(TEXT("AI Blueprint live fixture persists"), SaveAsset(Blueprint));
    }
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_AI_FIXTURES=%s"),
        *FString::Join(PackageNames, TEXT(",")));
    return true;
}
#endif

class FUnrealMCPAIModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        FUnrealMCPCompanionRegistration Registration;
        Registration.PluginName = TEXT("UnrealMCPAI");
        Registration.ExtensionId = TEXT("unreal-mcp-ai");
        Registration.OwningModule = TEXT("UnrealMCPAI");
        Registration.SemanticVersion = UnrealMCPAI::Version;
        Registration.CompanionApiVersion = UnrealMCPAI::CompanionApiVersion;
        Registration.ExtensionSchemaRevision = UnrealMCPAI::ExtensionSchemaRevision;
        Registration.RequiredEnginePlugins = {};
        Registration.RequiredEngineModules = {TEXT("AIModule"), TEXT("BehaviorTreeEditor")};
        Registration.AssetFamilies = {
            MakeFamily(EAIFamily::BehaviorTree, TEXT("behavior_tree"),
                UBehaviorTree::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact),
            MakeFamily(EAIFamily::Blackboard, TEXT("blackboard"),
                UBlackboardData::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact),
            MakeFamily(EAIFamily::EnvironmentQuery, TEXT("environment_query"),
                UEnvQuery::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact),
            MakeFamily(EAIFamily::TaskBlueprint, TEXT("bt_task_blueprint"),
                UBTTask_BlueprintBase::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived),
            MakeFamily(EAIFamily::DecoratorBlueprint, TEXT("bt_decorator_blueprint"),
                UBTDecorator_BlueprintBase::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived),
            MakeFamily(EAIFamily::ServiceBlueprint, TEXT("bt_service_blueprint"),
                UBTService_BlueprintBase::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived),
            MakeFamily(EAIFamily::GeneratorBlueprint, TEXT("eqs_generator_blueprint"),
                UEnvQueryGenerator_BlueprintBase::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived),
            MakeFamily(EAIFamily::ContextBlueprint, TEXT("eqs_context_blueprint"),
                UEnvQueryContext_BlueprintBase::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived)};
        RegistrationResult = IUnrealMCPModule::Get().RegisterCompanion(Registration, *this);
    }

    void ShutdownModule() override
    {
        if (RegistrationResult.bAccepted && IUnrealMCPModule::IsAvailable())
        {
            IUnrealMCPModule::Get().UnregisterCompanion(RegistrationResult.Handle, *this);
        }
        RegistrationResult = {};
    }

private:
    FUnrealMCPRegistrationResult RegistrationResult;
};
}

IMPLEMENT_MODULE(FUnrealMCPAIModule, UnrealMCPAI)
