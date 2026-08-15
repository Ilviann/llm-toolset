#include "UnrealMCPAnimationInspectionAdapter.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimStateAliasNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPBlueprintGraphInspection.h"
#include "UnrealMCPVersion.h"
#include "UObject/UnrealType.h"

namespace UnrealMCP::AnimationInspection::Private
{
constexpr int32 MaxAnimationGraphs = 128;
constexpr int32 MaxStateMachines = 128;
constexpr int32 MaxStates = 512;
constexpr int32 MaxTransitions = 1024;
constexpr int32 MaxParentOverrides = 256;

struct FMachine
{
    FString OwnerGraph;
    FString Name;
    FString Key;
    UAnimGraphNode_StateMachineBase* Node = nullptr;
    UAnimationStateMachineGraph* Graph = nullptr;
    TMap<FString, UAnimStateNodeBase*> States;
    TMap<FString, UAnimStateTransitionNode*> Transitions;
};

FString EncodeSegment(const FString& Input)
{
    FTCHARToUTF8 Utf8(*Input);
    FString Result;
    static const TCHAR Hex[] = TEXT("0123456789ABCDEF");
    for (int32 Index = 0; Index < Utf8.Length(); ++Index)
    {
        const uint8 Byte = static_cast<uint8>(Utf8.Get()[Index]);
        if ((Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z')
            || (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '.' || Byte == '_' || Byte == '~')
        {
            Result.AppendChar(static_cast<TCHAR>(Byte));
        }
        else
        {
            Result.AppendChar('%');
            Result.AppendChar(Hex[(Byte >> 4) & 0x0f]);
            Result.AppendChar(Hex[Byte & 0x0f]);
        }
    }
    return Result;
}

FString CanonicalSelector(const TArray<FString>& Segments)
{
    TArray<FString> Encoded;
    for (const FString& Segment : Segments) Encoded.Add(EncodeSegment(Segment));
    return FString::Join(Encoded, TEXT("/"));
}

FString SnakeCase(FString Value)
{
    Value.RemoveFromStart(TEXT("UAnimGraphNode_"));
    Value.RemoveFromStart(TEXT("U"));
    FString Result;
    for (int32 Index = 0; Index < Value.Len(); ++Index)
    {
        const TCHAR Character = Value[Index];
        if (FChar::IsUpper(Character) && Index > 0
            && (FChar::IsLower(Value[Index - 1]) || FChar::IsDigit(Value[Index - 1]))) Result.AppendChar('_');
        Result.AppendChar(FChar::ToLower(Character));
    }
    return Result;
}

FString GuidKey(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

TSharedPtr<FUnrealMCPValue> Scalar(UObject* Object, const TCHAR* Name)
{
    if (Object == nullptr) return nullptr;
    FProperty* Property = Object->GetClass()->FindPropertyByName(Name);
    if (Property == nullptr || Property->ArrayDim != 1
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) return nullptr;
    const void* Address = Property->ContainerPtrToValuePtr<void>(Object);
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        return MakeShared<FUnrealMCPValueBoolean>(Bool->GetPropertyValue(Address));
    if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
        return MakeShared<FUnrealMCPValueNumber>(Number->IsFloatingPoint()
            ? Number->GetFloatingPointPropertyValue(Address)
            : static_cast<double>(Number->GetSignedIntPropertyValue(Address)));
    if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        return MakeShared<FUnrealMCPValueString>(NameProperty->GetPropertyValue(Address).ToString());
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Value = ObjectProperty->GetObjectPropertyValue(Address);
        return MakeShared<FUnrealMCPValueString>(Value != nullptr ? Value->GetPathName() : FString());
    }
    FString Text;
    Property->ExportText_Direct(Text, Address, nullptr, Object, PPF_None);
    if (Text.Len() <= 512) return MakeShared<FUnrealMCPValueString>(SnakeCase(Text));
    return nullptr;
}

void AddScalar(UObject* Object, const TCHAR* Property, const TCHAR* Name, const TSharedRef<FUnrealMCPRecord>& Record)
{
    if (TSharedPtr<FUnrealMCPValue> Value = Scalar(Object, Property)) Record->SetField(Name, Value);
}

TSharedRef<FUnrealMCPRecord> Collection(const TCHAR* ItemType, int32 Count, const FString& Selector)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("array"));
    Result->SetStringField(TEXT("item_type"), ItemType);
    Result->SetNumberField(TEXT("count"), Count);
    Result->SetStringField(TEXT("selector"), Selector);
    return Result;
}

TSharedRef<FUnrealMCPRecord> Page(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    int32 Total,
    int32 Returned)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("size"), Context.PageSize);
    Result->SetNumberField(TEXT("index"), Context.PageIndex);
    Result->SetNumberField(TEXT("count"), Total == 0 ? 0 : (Total + Context.PageSize - 1) / Context.PageSize);
    Result->SetNumberField(TEXT("returned"), Returned);
    Result->SetNumberField(TEXT("total_items"), Total);
    Result->SetBoolField(TEXT("has_previous"), Context.PageIndex > 0 && Total > 0);
    Result->SetBoolField(TEXT("has_next"), static_cast<int64>(Context.PageIndex + 1) * Context.PageSize < Total);
    Result->SetStringField(TEXT("snapshot_id"), Context.Identity.SnapshotId);
    return Result;
}

void PageBounds(const FUnrealMCPAssetFamilyInspectionContext& Context, int32 Total, int32& Start, int32& End)
{
    Start = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Context.PageIndex) * Context.PageSize, Total));
    End = FMath::Min(Start + Context.PageSize, Total);
}

void AddSelection(const FUnrealMCPAssetFamilyInspectionContext& Context, const TSharedRef<FUnrealMCPRecord>& Result)
{
    const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
    Selection->SetStringField(TEXT("selector"), CanonicalSelector(Context.Selector.Segments));
    Result->SetObjectField(TEXT("selection"), Selection);
}

bool AddResult(
    const TSharedRef<FUnrealMCPRecord>& Result,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPError& OutError)
{
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Result->Values)
        if (!Document.Add({Field.Key, TEXT("record"), Field.Value}, OutError)) return false;
    return true;
}

TArray<FUnrealMCPAssetFamilySelectorRoute> Routes()
{
    return {
        {TEXT("animation_graphs"), {TEXT("animation_graphs")}, false, true},
        {TEXT("animation_state_machines"), {TEXT("state_machines")}, false, false},
        {TEXT("animation_states"), {TEXT("states")}, false, true},
        {TEXT("animation_transitions"), {TEXT("transitions")}, false, true},
        {TEXT("animation_transition_blends"), {TEXT("transition_blends")}, false, true},
        {TEXT("animation_properties"), {TEXT("properties"), TEXT("animation_blueprint")}, true, false},
        {TEXT("animation_parent_overrides"), {TEXT("parent_asset_overrides")}, true, false}};
}

bool RegisterRoutes(FUnrealMCPAssetFamilySelectorRouter& Router, FUnrealMCPError& OutError)
{
    for (const FUnrealMCPAssetFamilySelectorRoute& Route : Routes())
        if (!Router.Register(Route, OutError)) return false;
    return Router.Freeze(OutError);
}

TArray<UAnimationGraph*> AnimationGraphs(UAnimBlueprint* Blueprint)
{
    TArray<UAnimationGraph*> Result;
    if (Blueprint == nullptr) return Result;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(Graph)) Result.Add(AnimationGraph);
    Result.Sort([](const UAnimationGraph& Left, const UAnimationGraph& Right)
    {
        return Left.GetName() + GuidKey(Left.GraphGuid) < Right.GetName() + GuidKey(Right.GraphGuid);
    });
    return Result;
}

UEdGraphNode* PreferredRoot(UEdGraph* Graph);

TSharedRef<FUnrealMCPRecord> PinTypeRecord(const UEdGraphPin* Pin)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
    if (!Pin->PinType.PinSubCategory.IsNone())
        Result->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
    if (Pin->PinType.PinSubCategoryObject.IsValid())
        Result->SetStringField(TEXT("object_path"), Pin->PinType.PinSubCategoryObject->GetPathName());
    Result->SetStringField(TEXT("container"), Pin->PinType.ContainerType == EPinContainerType::Array
        ? TEXT("array") : Pin->PinType.ContainerType == EPinContainerType::Set
            ? TEXT("set") : Pin->PinType.ContainerType == EPinContainerType::Map ? TEXT("map") : TEXT("scalar"));
    Result->SetBoolField(TEXT("reference"), Pin->PinType.bIsReference);
    Result->SetBoolField(TEXT("const"), Pin->PinType.bIsConst);
    return Result;
}

TSharedRef<FUnrealMCPRecord> SignaturePin(const UEdGraphPin* Pin)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("name"), Pin->PinFriendlyName.IsEmpty()
        ? Pin->PinName.ToString() : Pin->PinFriendlyName.ToString());
    Result->SetObjectField(TEXT("type"), PinTypeRecord(Pin));
    return Result;
}

TSharedRef<FUnrealMCPRecord> GraphSignature(UAnimationGraph* Graph)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    TArray<const UEdGraphPin*> Inputs;
    TArray<const UEdGraphPin*> Outputs;
    if (Graph != nullptr)
    {
        UEdGraphNode* Root = PreferredRoot(Graph);
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) continue;
            const FString ClassName = Node->GetClass()->GetName();
            if (ClassName.Contains(TEXT("LinkedInputPose")) || ClassName.Contains(TEXT("FunctionEntry")))
                for (const UEdGraphPin* Pin : Node->Pins)
                    if (Pin != nullptr && Pin->Direction == EGPD_Output
                        && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) Inputs.Add(Pin);
        }
        if (Root != nullptr)
            for (const UEdGraphPin* Pin : Root->Pins)
                if (Pin != nullptr && Pin->Direction == EGPD_Input
                    && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) Outputs.Add(Pin);
    }
    const auto SortPins = [](const UEdGraphPin& Left, const UEdGraphPin& Right)
    {
        return Left.PinName.LexicalLess(Right.PinName);
    };
    Inputs.Sort(SortPins);
    Outputs.Sort(SortPins);
    TArray<TSharedPtr<FUnrealMCPValue>> InputValues;
    TArray<TSharedPtr<FUnrealMCPValue>> OutputValues;
    for (const UEdGraphPin* Pin : Inputs) InputValues.Add(MakeShared<FUnrealMCPValueObject>(SignaturePin(Pin)));
    for (const UEdGraphPin* Pin : Outputs) OutputValues.Add(MakeShared<FUnrealMCPValueObject>(SignaturePin(Pin)));
    Result->SetArrayField(TEXT("inputs"), InputValues);
    Result->SetArrayField(TEXT("outputs"), OutputValues);
    return Result;
}

TSharedRef<FUnrealMCPRecord> AnimationGraphHeader(UAnimationGraph* Graph)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("name"), Graph != nullptr ? Graph->GetName() : FString());
    Result->SetStringField(TEXT("kind"), Graph != nullptr && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph
        ? TEXT("main_pose") : TEXT("animation_layer"));
    Result->SetObjectField(TEXT("signature"), GraphSignature(Graph));
    return Result;
}

TSharedRef<FUnrealMCPRecord> NotifyRecord(const FAnimNotifyEvent& Event)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("name"), Event.NotifyName.ToString());
    if (Event.Notify != nullptr)
        Result->SetStringField(TEXT("notify_class"), Event.Notify->GetClass()->GetPathName());
    if (Event.NotifyStateClass != nullptr)
        Result->SetStringField(TEXT("notify_state_class"), Event.NotifyStateClass->GetClass()->GetPathName());
    return Result;
}

TSharedRef<FUnrealMCPRecord> StateNotifications(const UAnimStateNode* State)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetObjectField(TEXT("entered"), NotifyRecord(State->StateEntered));
    Result->SetObjectField(TEXT("left"), NotifyRecord(State->StateLeft));
    Result->SetObjectField(TEXT("fully_blended"), NotifyRecord(State->StateFullyBlended));
    return Result;
}

TSharedRef<FUnrealMCPRecord> TransitionNotifications(const UAnimStateTransitionNode* Transition)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetObjectField(TEXT("started"), NotifyRecord(Transition->TransitionStart));
    Result->SetObjectField(TEXT("ended"), NotifyRecord(Transition->TransitionEnd));
    Result->SetObjectField(TEXT("interrupted"), NotifyRecord(Transition->TransitionInterrupt));
    return Result;
}

FString UniqueKey(const FString& Name, const FGuid& Guid, TSet<FString>& Used)
{
    FString Key = Name.IsEmpty() ? TEXT("unnamed") : Name;
    if (Used.Contains(Key)) Key += TEXT("~") + GuidKey(Guid).Left(8);
    int32 Suffix = 2;
    const FString Base = Key;
    while (Used.Contains(Key)) Key = Base + TEXT("~") + LexToString(Suffix++);
    Used.Add(Key);
    return Key;
}

TArray<FMachine> Machines(UAnimBlueprint* Blueprint, FUnrealMCPError& OutError)
{
    TArray<FMachine> Result;
    for (UAnimationGraph* Owner : AnimationGraphs(Blueprint))
    {
        TArray<UAnimGraphNode_StateMachineBase*> Nodes;
        for (UEdGraphNode* Node : Owner->Nodes)
            if (UAnimGraphNode_StateMachineBase* Machine = Cast<UAnimGraphNode_StateMachineBase>(Node)) Nodes.Add(Machine);
        Nodes.Sort([](const UAnimGraphNode_StateMachineBase& Left, const UAnimGraphNode_StateMachineBase& Right)
        {
            return Left.GetNodeTitle(ENodeTitleType::ListView).ToString() + GuidKey(Left.NodeGuid)
                < Right.GetNodeTitle(ENodeTitleType::ListView).ToString() + GuidKey(Right.NodeGuid);
        });
        TSet<FString> MachineKeys;
        for (UAnimGraphNode_StateMachineBase* Node : Nodes)
        {
            FMachine Model;
            Model.OwnerGraph = Owner->GetName();
            Model.Name = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
            Model.Key = UniqueKey(Model.Name, Node->NodeGuid, MachineKeys);
            Model.Node = Node;
            Model.Graph = Node->EditorStateMachineGraph;
            if (Model.Graph == nullptr) continue;
            TArray<UAnimStateNodeBase*> States;
            TArray<UAnimStateTransitionNode*> Transitions;
            for (UEdGraphNode* GraphNode : Model.Graph->Nodes)
            {
                if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(GraphNode)) Transitions.Add(Transition);
                else if (UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(GraphNode)) States.Add(State);
            }
            States.Sort([](const UAnimStateNodeBase& Left, const UAnimStateNodeBase& Right)
            {
                return Left.GetStateName() + GuidKey(Left.NodeGuid) < Right.GetStateName() + GuidKey(Right.NodeGuid);
            });
            Transitions.Sort([](const UAnimStateTransitionNode& Left, const UAnimStateTransitionNode& Right)
            {
                return Left.GetStateName() + GuidKey(Left.NodeGuid) < Right.GetStateName() + GuidKey(Right.NodeGuid);
            });
            TSet<FString> StateKeys;
            for (UAnimStateNodeBase* State : States)
                Model.States.Add(UniqueKey(State->GetStateName(), State->NodeGuid, StateKeys), State);
            TSet<FString> TransitionKeys;
            for (UAnimStateTransitionNode* Transition : Transitions)
            {
                const FString From = Transition->GetPreviousState() != nullptr ? Transition->GetPreviousState()->GetStateName() : TEXT("unknown");
                const FString To = Transition->GetNextState() != nullptr ? Transition->GetNextState()->GetStateName() : TEXT("unknown");
                Model.Transitions.Add(UniqueKey(From + TEXT("_to_") + To, Transition->NodeGuid, TransitionKeys), Transition);
            }
            if (Model.States.Num() > MaxStates || Model.Transitions.Num() > MaxTransitions)
            {
                OutError = {TEXT("data_limit_exceeded"), TEXT("An Animation Blueprint state machine exceeds its safety limit")};
                return {};
            }
            Result.Add(MoveTemp(Model));
        }
    }
    if (Result.Num() > MaxStateMachines)
        OutError = {TEXT("data_limit_exceeded"), TEXT("The Animation Blueprint exceeds the state-machine safety limit")};
    return Result;
}

const FMachine* FindMachine(const TArray<FMachine>& Models, const FString& Owner, const FString& Key)
{
    return Models.FindByPredicate([&](const FMachine& Model) { return Model.OwnerGraph == Owner && Model.Key == Key; });
}

UEdGraphNode* PreferredRoot(UEdGraph* Graph)
{
    if (Graph == nullptr) return nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr) continue;
        const FString ClassName = Node->GetClass()->GetName();
        if (ClassName.Contains(TEXT("Root")) || ClassName.Contains(TEXT("Result"))
            || ClassName.Contains(TEXT("SaveCachedPose"))) return Node;
    }
    return nullptr;
}

void CollectAnimationAssets(const UStruct* Struct, const void* Data, TArray<FString>& Paths, int32 Depth = 0)
{
    if (Struct == nullptr || Data == nullptr || Depth > 2 || Paths.Num() >= 16) return;
    for (TFieldIterator<FProperty> It(Struct); It && Paths.Num() < 16; ++It)
    {
        FProperty* Property = *It;
        if (Property->ArrayDim != 1 || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
        const void* Address = Property->ContainerPtrToValuePtr<void>(Data);
        if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
        {
            if (const UAnimationAsset* Asset = Cast<UAnimationAsset>(Object->GetObjectPropertyValue(Address)))
                Paths.AddUnique(Asset->GetPathName());
        }
        else if (const FStructProperty* Nested = CastField<FStructProperty>(Property))
        {
            if (Property->GetFName() == TEXT("Node") || Depth > 0)
                CollectAnimationAssets(Nested->Struct, Address, Paths, Depth + 1);
        }
    }
}

void DecorateAnimationNode(UEdGraphNode* Node, const TSharedRef<FUnrealMCPRecord>& Record)
{
    if (Node == nullptr || !Node->IsA<UAnimGraphNode_Base>()) return;
    Record->SetStringField(TEXT("animation_node_type"), SnakeCase(Node->GetClass()->GetName()));
    TArray<FString> Assets;
    CollectAnimationAssets(Node->GetClass(), Node, Assets);
    Assets.Sort();
    if (!Assets.IsEmpty())
    {
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        for (const FString& Path : Assets) Values.Add(MakeShared<FUnrealMCPValueString>(Path));
        Record->SetArrayField(TEXT("animation_assets"), Values);
    }
    if (const UAnimGraphNode_SaveCachedPose* Save = Cast<UAnimGraphNode_SaveCachedPose>(Node))
        Record->SetStringField(TEXT("cache_pose"), Save->CacheName);
    if (const UAnimGraphNode_UseCachedPose* Use = Cast<UAnimGraphNode_UseCachedPose>(Node))
        if (Use->SaveCachedPoseNode.IsValid()) Record->SetStringField(TEXT("cache_pose"), Use->SaveCachedPoseNode->CacheName);
    if (const UAnimGraphNode_LinkedAnimLayer* Layer = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
        Record->SetStringField(TEXT("layer"), Layer->Node.Layer.ToString());
}

FString BuildSnapshot(UObject* Asset)
{
    UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Asset);
    TArray<FString> Lines;
    Lines.Add(TEXT("asset|") + GetPathNameSafe(Asset));
    if (Blueprint != nullptr)
    {
        Lines.Add(TEXT("mode|") + LexToString(static_cast<int32>(Blueprint->BlueprintType))
            + TEXT("|") + LexToString(Blueprint->bIsTemplate)
            + TEXT("|") + GetPathNameSafe(Blueprint->TargetSkeleton)
            + TEXT("|") + LexToString(Blueprint->bUseMultiThreadedAnimationUpdate)
            + TEXT("|") + LexToString(Blueprint->bWarnAboutBlueprintUsage));
        for (const FAnimGroupInfo& Group : Blueprint->Groups) Lines.Add(TEXT("group|") + Group.Name.ToString());
        for (const FAnimParentNodeAssetOverride& Override : Blueprint->ParentAssetOverrides)
            Lines.Add(TEXT("override|") + GuidKey(Override.ParentNodeGuid) + TEXT("|") + GetPathNameSafe(Override.NewAsset));
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph == nullptr) continue;
            Lines.Add(TEXT("graph|") + GuidKey(Graph->GraphGuid) + TEXT("|") + Graph->GetName());
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node == nullptr) continue;
                Lines.Add(TEXT("node|") + GuidKey(Node->NodeGuid) + TEXT("|") + Node->GetClass()->GetPathName());
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (Pin == nullptr || Pin->bOrphanedPin) continue;
                    Lines.Add(TEXT("pin|") + GuidKey(Pin->PinId) + TEXT("|") + Pin->PinName.ToString()
                        + TEXT("|") + Pin->DefaultValue + TEXT("|") + GetPathNameSafe(Pin->DefaultObject));
                }
            }
        }
    }
    Lines.Sort();
    const FTCHARToUTF8 Utf8(*FString::Join(Lines, TEXT("\n")));
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

class FAnimationInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Context.Asset);
        if (Blueprint == nullptr)
        {
            OutError = {TEXT("unsupported_type"), TEXT("The animation inspection overlay requires an Animation Blueprint")};
            return false;
        }
        if (AnimationGraphs(Blueprint).Num() > MaxAnimationGraphs || Blueprint->ParentAssetOverrides.Num() > MaxParentOverrides)
        {
            OutError = {TEXT("data_limit_exceeded"), TEXT("The Animation Blueprint exceeds its semantic safety limit")};
            return false;
        }
        TArray<FMachine> MachineModels = Machines(Blueprint, OutError);
        if (!OutError.Code.IsEmpty()) return false;
        const bool bGraphRoute = !Context.Selector.IsRoot()
            && (Context.Selector.Segments[0] == TEXT("animation_graphs")
                || Context.Selector.Segments[0] == TEXT("states")
                || Context.Selector.Segments[0] == TEXT("transitions")
                || Context.Selector.Segments[0] == TEXT("transition_blends"));
        if (Context.bHasPartialGraphFlag && !bGraphRoute)
        {
            OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to animation graph selectors")};
            return false;
        }
        if (Context.Selector.IsRoot())
        {
            if (Context.bHasPaging)
            {
                OutError = {TEXT("invalid_argument"), TEXT("Paging parameters require a pageable animation collection selector")};
                return false;
            }
            if (!BuildRoot(Blueprint, MachineModels, Document, OutError)) return false;
        }
        else if (!BuildSelection(Context, Blueprint, MachineModels, Document, OutError)) return false;
        return Snapshot.Add(TEXT("animation_snapshot"), BuildSnapshot(Blueprint), OutError)
            && RegisterRoutes(Selectors, OutError);
    }

private:
    static bool BuildRoot(
        UAnimBlueprint* Blueprint,
        const TArray<FMachine>& Machines,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        const bool bInterface = Blueprint->BlueprintType == BPTYPE_Interface;
        const TSharedRef<FUnrealMCPRecord> Animation = MakeShared<FUnrealMCPRecord>();
        Animation->SetStringField(TEXT("mode"), bInterface ? TEXT("interface")
            : Blueprint->bIsTemplate ? TEXT("template") : TEXT("regular"));
        if (!bInterface && !Blueprint->bIsTemplate && Blueprint->TargetSkeleton != nullptr)
            Animation->SetStringField(TEXT("target_skeleton"), Blueprint->TargetSkeleton->GetPathName());
        UObject* Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
        AddScalar(Defaults, TEXT("RootMotionMode"), TEXT("root_motion_mode"), Animation);
        const TSharedRef<FUnrealMCPRecord> Threaded = MakeShared<FUnrealMCPRecord>();
        Threaded->SetBoolField(TEXT("requested"), Blueprint->bUseMultiThreadedAnimationUpdate);
        Threaded->SetBoolField(TEXT("project_setting_required"), true);
        Threaded->SetBoolField(TEXT("warn_about_blueprint_usage"), Blueprint->bWarnAboutBlueprintUsage);
        Animation->SetObjectField(TEXT("threaded_update"), Threaded);
        const TSharedRef<FUnrealMCPRecord> Linked = MakeShared<FUnrealMCPRecord>();
        Linked->SetBoolField(TEXT("share_instances"), Blueprint->bEnableLinkedAnimLayerInstanceSharing);
        AddScalar(Defaults, TEXT("bReceiveNotifiesFromLinkedInstances"), TEXT("receive_notifies"), Linked);
        AddScalar(Defaults, TEXT("bPropagateNotifiesToLinkedInstances"), TEXT("propagate_notifies"), Linked);
        AddScalar(Defaults, TEXT("bUseMainInstanceMontageEvaluationData"), TEXT("use_main_instance_montage_data"), Linked);
        Animation->SetObjectField(TEXT("linked_layers"), Linked);
        const TSharedRef<FUnrealMCPRecord> Compiled = MakeShared<FUnrealMCPRecord>();
        AddScalar(Defaults, TEXT("bUsingCopyPoseFromMesh"), TEXT("uses_copy_pose_from_mesh"), Compiled);
        Animation->SetObjectField(TEXT("compiled_features"), Compiled);
        Animation->SetObjectField(TEXT("sync_groups"), Collection(TEXT("name"), Blueprint->Groups.Num(),
            TEXT("properties/animation_blueprint/sync_groups")));
        Animation->SetObjectField(TEXT("implemented_interfaces"), Collection(TEXT("asset_reference"),
            Blueprint->ImplementedInterfaces.Num(), TEXT("properties/animation_blueprint/implemented_interfaces")));
        Animation->SetObjectField(TEXT("parent_asset_overrides"), Collection(TEXT("animation_asset_override"),
            Blueprint->ParentAssetOverrides.Num(), TEXT("parent_asset_overrides")));
        Result->SetObjectField(TEXT("animation_blueprint"), Animation);

        TArray<TSharedPtr<FUnrealMCPValue>> GraphValues;
        for (UAnimationGraph* Graph : AnimationGraphs(Blueprint))
        {
            const TSharedRef<FUnrealMCPRecord> Entry = AnimationGraphHeader(Graph);
            Entry->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
            Entry->SetStringField(TEXT("selector"), TEXT("animation_graphs/") + EncodeSegment(Graph->GetName()));
            GraphValues.Add(MakeShared<FUnrealMCPValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("animation_graphs"), GraphValues);
        TArray<TSharedPtr<FUnrealMCPValue>> MachineValues;
        for (const FMachine& Machine : Machines)
        {
            const TSharedRef<FUnrealMCPRecord> Entry = MakeShared<FUnrealMCPRecord>();
            Entry->SetStringField(TEXT("name"), Machine.Name);
            Entry->SetStringField(TEXT("owner_graph"), Machine.OwnerGraph);
            Entry->SetNumberField(TEXT("states"), Machine.States.Num());
            Entry->SetNumberField(TEXT("transitions"), Machine.Transitions.Num());
            Entry->SetStringField(TEXT("selector"), TEXT("state_machines/")
                + EncodeSegment(Machine.OwnerGraph) + TEXT("/") + EncodeSegment(Machine.Key));
            MachineValues.Add(MakeShared<FUnrealMCPValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("state_machines"), MachineValues);
        TArray<TSharedPtr<FUnrealMCPValue>> SelectorValues;
        for (const TCHAR* Name : {TEXT("animation_graphs"), TEXT("state_machines"),
            TEXT("parent_asset_overrides"), TEXT("properties/animation_blueprint")})
            SelectorValues.Add(MakeShared<FUnrealMCPValueString>(Name));
        Result->SetArrayField(TEXT("selectors"), SelectorValues);
        return AddResult(Result, Document, OutError);
    }

    static bool BuildSelection(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        UAnimBlueprint* Blueprint,
        const TArray<FMachine>& Machines,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        const TArray<FString>& S = Context.Selector.Segments;
        if (S[0] == TEXT("animation_graphs") && S.Num() == 2)
        {
            UAnimationGraph* Graph = nullptr;
            for (UAnimationGraph* Candidate : AnimationGraphs(Blueprint))
                if (Candidate->GetName() == S[1]) { Graph = Candidate; break; }
            if (Graph == nullptr)
            {
                OutError = {TEXT("not_found"), TEXT("The selected animation graph was not found")};
                return false;
            }
            if (Blueprint->BlueprintType == BPTYPE_Interface)
            {
                if (Context.bHasPaging || Context.bHasPartialGraphFlag)
                {
                    OutError = {TEXT("invalid_argument"),
                        TEXT("Animation Layer Interface selections expose signatures, not graph bodies")};
                    return false;
                }
                const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
                AddSelection(Context, Result);
                Result->SetObjectField(TEXT("animation_graph"), AnimationGraphHeader(Graph));
                return AddResult(Result, Document, OutError);
            }
            return InspectAtomicGraph(Context, Graph, TEXT("animation_pose"), S[1], Document, OutError);
        }
        if (S[0] == TEXT("state_machines") && S.Num() == 3)
        {
            if (Context.bHasPaging)
            {
                OutError = {TEXT("invalid_argument"), TEXT("State-machine topology does not accept paging")}; return false;
            }
            const FMachine* Machine = FindMachine(Machines, S[1], S[2]);
            if (Machine == nullptr) { OutError = {TEXT("not_found"), TEXT("The selected state machine was not found")}; return false; }
            const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
            AddSelection(Context, Result);
            Result->SetObjectField(TEXT("state_machine"), StateMachineRecord(*Machine));
            return AddResult(Result, Document, OutError);
        }
        if ((S[0] == TEXT("states") || S[0] == TEXT("transitions") || S[0] == TEXT("transition_blends"))
            && S.Num() == 4)
        {
            const FMachine* Machine = FindMachine(Machines, S[1], S[2]);
            if (Machine == nullptr) { OutError = {TEXT("not_found"), TEXT("The selected state machine was not found")}; return false; }
            if (S[0] == TEXT("states"))
            {
                UAnimStateNodeBase* const* Found = Machine->States.Find(S[3]);
                if (Found == nullptr) { OutError = {TEXT("not_found"), TEXT("The selected state was not found")}; return false; }
                if (UAnimStateAliasNode* Alias = Cast<UAnimStateAliasNode>(*Found)) return InspectAlias(Context, Alias, Document, OutError);
                return InspectAtomicGraph(Context, (*Found)->GetBoundGraph(),
                    (*Found)->IsA<UAnimStateConduitNode>() ? TEXT("conduit_rule") : TEXT("state_pose"),
                    (*Found)->GetStateName(), Document, OutError);
            }
            UAnimStateTransitionNode* const* Found = Machine->Transitions.Find(S[3]);
            if (Found == nullptr) { OutError = {TEXT("not_found"), TEXT("The selected transition was not found")}; return false; }
            if (S[0] == TEXT("transitions"))
                return InspectTransition(Context, *Found, false, Document, OutError);
            return InspectTransition(Context, *Found, true, Document, OutError);
        }
        if (S[0] == TEXT("properties") && S.Num() == 3 && S[1] == TEXT("animation_blueprint"))
            return InspectPropertyPage(Context, Blueprint, S[2], Document, OutError);
        if (S[0] == TEXT("parent_asset_overrides") && S.Num() == 1)
            return InspectOverrides(Context, Blueprint, Document, OutError);
        OutError = {TEXT("not_found"), TEXT("The selected Animation Blueprint semantic child was not found")};
        return false;
    }

    static bool InspectAtomicGraph(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        UEdGraph* Graph,
        const FString& Kind,
        const FString& Name,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        BlueprintGraphInspection::FSelection Selection;
        Selection.Graph = Graph;
        Selection.PreferredRoot = PreferredRoot(Graph);
        Selection.Kind = Kind;
        Selection.Name = Name;
        Selection.Selector = CanonicalSelector(Context.Selector.Segments);
        Selection.bTraverseInputsFromRoot = true;
        Selection.DecorateNode = DecorateAnimationNode;
        if (!BlueprintGraphInspection::InspectGraph(Context, Selection, Document, OutError)) return false;
        if (UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(Graph))
            return Document.Add({TEXT("animation_graph"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(AnimationGraphHeader(AnimationGraph))}, OutError);
        return true;
    }

    static TSharedRef<FUnrealMCPRecord> StateMachineRecord(const FMachine& Machine)
    {
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        Result->SetStringField(TEXT("name"), Machine.Name);
        Result->SetStringField(TEXT("owner_graph"), Machine.OwnerGraph);
        if (Machine.Graph != nullptr && Machine.Graph->EntryNode != nullptr)
        {
            UEdGraphPin* Output = Machine.Graph->EntryNode->GetOutputPin();
            if (Output != nullptr && !Output->LinkedTo.IsEmpty() && Output->LinkedTo[0] != nullptr)
                if (UAnimStateNodeBase* Entry = Cast<UAnimStateNodeBase>(Output->LinkedTo[0]->GetOwningNodeUnchecked()))
                    Result->SetStringField(TEXT("entry_state"), Entry->GetStateName());
        }
        TArray<FString> StateKeys;
        Machine.States.GetKeys(StateKeys); StateKeys.Sort();
        TArray<TSharedPtr<FUnrealMCPValue>> States;
        for (const FString& Key : StateKeys)
        {
            UAnimStateNodeBase* Node = Machine.States[Key];
            const TSharedRef<FUnrealMCPRecord> Entry = MakeShared<FUnrealMCPRecord>();
            Entry->SetStringField(TEXT("name"), Node->GetStateName());
            Entry->SetStringField(TEXT("kind"), Node->IsA<UAnimStateConduitNode>() ? TEXT("conduit")
                : Node->IsA<UAnimStateAliasNode>() ? TEXT("alias") : TEXT("state"));
            if (const UAnimStateNode* State = Cast<UAnimStateNode>(Node))
            {
                Entry->SetBoolField(TEXT("always_reset_on_entry"), State->bAlwaysResetOnEntry);
                Entry->SetObjectField(TEXT("notifications"), StateNotifications(State));
            }
            Entry->SetStringField(TEXT("selector"), TEXT("states/") + EncodeSegment(Machine.OwnerGraph)
                + TEXT("/") + EncodeSegment(Machine.Key) + TEXT("/") + EncodeSegment(Key));
            States.Add(MakeShared<FUnrealMCPValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("states"), States);
        TArray<FString> TransitionKeys;
        Machine.Transitions.GetKeys(TransitionKeys); TransitionKeys.Sort();
        TArray<TSharedPtr<FUnrealMCPValue>> Transitions;
        for (const FString& Key : TransitionKeys)
        {
            UAnimStateTransitionNode* Node = Machine.Transitions[Key];
            const TSharedRef<FUnrealMCPRecord> Entry = TransitionRecord(Node);
            Entry->SetStringField(TEXT("key"), Key);
            Entry->SetStringField(TEXT("rule_selector"), TEXT("transitions/") + EncodeSegment(Machine.OwnerGraph)
                + TEXT("/") + EncodeSegment(Machine.Key) + TEXT("/") + EncodeSegment(Key));
            if (Node->GetCustomTransitionGraph() != nullptr)
                Entry->SetStringField(TEXT("blend_selector"), TEXT("transition_blends/") + EncodeSegment(Machine.OwnerGraph)
                    + TEXT("/") + EncodeSegment(Machine.Key) + TEXT("/") + EncodeSegment(Key));
            Transitions.Add(MakeShared<FUnrealMCPValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("transitions"), Transitions);
        return Result;
    }

    static TSharedRef<FUnrealMCPRecord> TransitionRecord(UAnimStateTransitionNode* Node)
    {
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        Result->SetStringField(TEXT("from"), Node->GetPreviousState() != nullptr ? Node->GetPreviousState()->GetStateName() : TEXT("unknown"));
        Result->SetStringField(TEXT("to"), Node->GetNextState() != nullptr ? Node->GetNextState()->GetStateName() : TEXT("unknown"));
        Result->SetNumberField(TEXT("priority"), Node->PriorityOrder);
        Result->SetBoolField(TEXT("disabled"), Node->bDisabled);
        Result->SetBoolField(TEXT("bidirectional"), Node->Bidirectional);
        Result->SetBoolField(TEXT("active_only"), Node->bOnlyEvaluateWhenActive);
        Result->SetBoolField(TEXT("automatic_remaining_time_rule"), Node->bAutomaticRuleBasedOnSequencePlayerInState);
        Result->SetNumberField(TEXT("automatic_trigger_time_seconds"), Node->AutomaticRuleTriggerTime);
        Result->SetNumberField(TEXT("minimum_reentry_time_seconds"), Node->MinTimeBeforeReentry);
        Result->SetStringField(TEXT("required_marker_sync_group"),
            Node->SyncGroupNameToRequireValidMarkersRule.ToString());
        Result->SetStringField(TEXT("logic"), SnakeCase(UEnum::GetValueAsString(Node->LogicType)));
        Result->SetBoolField(TEXT("allow_inertialization_for_self_transition"), Node->bAllowInertializationForSelfTransitions);
        Result->SetBoolField(TEXT("shared_rule"), Node->bSharedRules);
        Result->SetBoolField(TEXT("shared_crossfade"), Node->bSharedCrossfade);
        if (Node->bSharedRules) Result->SetStringField(TEXT("shared_rule_name"), Node->SharedRulesName);
        if (Node->bSharedCrossfade) Result->SetStringField(TEXT("shared_crossfade_name"), Node->SharedCrossfadeName);
        const TSharedRef<FUnrealMCPRecord> Blend = MakeShared<FUnrealMCPRecord>();
        Blend->SetNumberField(TEXT("duration_seconds"), Node->CrossfadeDuration);
        Blend->SetStringField(TEXT("mode"), SnakeCase(UEnum::GetValueAsString(Node->BlendMode)));
        if (Node->CustomBlendCurve != nullptr)
            Blend->SetStringField(TEXT("custom_curve"), Node->CustomBlendCurve->GetPathName());
        if (const UBlendProfile* Profile = Node->BlendProfileWrapper.GetBlendProfile())
            Blend->SetStringField(TEXT("profile"), Profile->GetPathName());
        Result->SetObjectField(TEXT("blend"), Blend);
        Result->SetObjectField(TEXT("notifications"), TransitionNotifications(Node));
        Result->SetBoolField(TEXT("custom_blend"), Node->GetCustomTransitionGraph() != nullptr);
        return Result;
    }

    static bool InspectTransition(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        UAnimStateTransitionNode* Node,
        bool bBlend,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        UEdGraph* Graph = bBlend ? Node->GetCustomTransitionGraph() : Node->GetBoundGraph();
        if (Graph == nullptr)
        {
            OutError = {TEXT("not_found"), bBlend
                ? TEXT("The selected transition has no custom blend graph")
                : TEXT("The selected transition rule graph is unavailable")};
            return false;
        }
        if (!InspectAtomicGraph(Context, Graph, bBlend ? TEXT("transition_blend") : TEXT("transition_rule"),
            Node->GetStateName(), Document, OutError)) return false;
        if (!bBlend) return Document.Add({TEXT("transition"), TEXT("record"),
            MakeShared<FUnrealMCPValueObject>(TransitionRecord(Node))}, OutError);
        return true;
    }

    static bool InspectAlias(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        UAnimStateAliasNode* Alias,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        if (Context.bHasPaging || Context.bHasPartialGraphFlag)
        {
            OutError = {TEXT("invalid_argument"), TEXT("State aliases are bounded policies, not graphs")}; return false;
        }
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        AddSelection(Context, Result);
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("name"), Alias->GetStateName());
        Record->SetBoolField(TEXT("global"), Alias->bGlobalAlias);
        TArray<FString> Names;
        for (const TWeakObjectPtr<UAnimStateNodeBase>& State : Alias->GetAliasedStates())
            if (State.IsValid()) Names.Add(State->GetStateName());
        Names.Sort();
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        for (const FString& Name : Names) Values.Add(MakeShared<FUnrealMCPValueString>(Name));
        Record->SetArrayField(TEXT("target_states"), Values);
        Result->SetObjectField(TEXT("state_alias"), Record);
        return AddResult(Result, Document, OutError);
    }

    static bool InspectPropertyPage(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        UAnimBlueprint* Blueprint,
        const FString& Kind,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        if (Context.bHasPartialGraphFlag) { OutError = {TEXT("invalid_argument"), TEXT("Animation properties are not graphs")}; return false; }
        TArray<TSharedPtr<FUnrealMCPValue>> All;
        if (Kind == TEXT("sync_groups"))
        {
            TArray<FString> Names;
            for (const FAnimGroupInfo& Group : Blueprint->Groups) Names.Add(Group.Name.ToString());
            Names.Sort();
            for (const FString& Name : Names) All.Add(MakeShared<FUnrealMCPValueString>(Name));
        }
        else if (Kind == TEXT("implemented_interfaces"))
        {
            TArray<FString> Paths;
            for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
                if (Interface.Interface != nullptr) Paths.Add(Interface.Interface->GetPathName());
            Paths.Sort();
            for (const FString& Path : Paths) All.Add(MakeShared<FUnrealMCPValueString>(Path));
        }
        else { OutError = {TEXT("not_found"), TEXT("The selected animation property collection was not found")}; return false; }
        int32 Start = 0, End = 0; PageBounds(Context, All.Num(), Start, End);
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        for (int32 Index = Start; Index < End; ++Index) Values.Add(All[Index]);
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        AddSelection(Context, Result);
        Result->SetArrayField(Kind, Values);
        Result->SetObjectField(TEXT("page"), Page(Context, All.Num(), Values.Num()));
        return AddResult(Result, Document, OutError);
    }

    static bool InspectOverrides(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        UAnimBlueprint* Blueprint,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPError& OutError)
    {
        if (Context.bHasPartialGraphFlag) { OutError = {TEXT("invalid_argument"), TEXT("Parent overrides are not graphs")}; return false; }
        TArray<const FAnimParentNodeAssetOverride*> Overrides;
        for (const FAnimParentNodeAssetOverride& Override : Blueprint->ParentAssetOverrides) Overrides.Add(&Override);
        Overrides.Sort([](const FAnimParentNodeAssetOverride& Left, const FAnimParentNodeAssetOverride& Right)
            { return GuidKey(Left.ParentNodeGuid) < GuidKey(Right.ParentNodeGuid); });
        int32 Start = 0, End = 0; PageBounds(Context, Overrides.Num(), Start, End);
        TArray<TSharedPtr<FUnrealMCPValue>> Values;
        UAnimBlueprint* Parent = UAnimBlueprint::GetParentAnimBlueprint(Blueprint);
        TArray<UEdGraph*> ParentGraphs;
        if (Parent != nullptr) Parent->GetAllGraphs(ParentGraphs);
        for (int32 Index = Start; Index < End; ++Index)
        {
            const FAnimParentNodeAssetOverride& Override = *Overrides[Index];
            const TSharedRef<FUnrealMCPRecord> Entry = MakeShared<FUnrealMCPRecord>();
            Entry->SetStringField(TEXT("animation_asset"), GetPathNameSafe(Override.NewAsset));
            bool bResolved = false;
            for (UEdGraph* Graph : ParentGraphs)
            {
                if (Graph == nullptr) continue;
                for (UEdGraphNode* Node : Graph->Nodes)
                    if (Node != nullptr && Node->NodeGuid == Override.ParentNodeGuid)
                    {
                        Entry->SetStringField(TEXT("owner_graph"), Graph->GetName());
                        Entry->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                        bResolved = true; break;
                    }
                if (bResolved) break;
            }
            Entry->SetBoolField(TEXT("parent_node_resolved"), bResolved);
            if (Context.bVerbose) Entry->SetStringField(TEXT("parent_node_guid"), GuidKey(Override.ParentNodeGuid));
            Values.Add(MakeShared<FUnrealMCPValueObject>(Entry));
        }
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        AddSelection(Context, Result);
        Result->SetArrayField(TEXT("parent_asset_overrides"), Values);
        Result->SetObjectField(TEXT("page"), Page(Context, Overrides.Num(), Values.Num()));
        return AddResult(Result, Document, OutError);
    }
};
}

bool UnrealMCP::AnimationInspection::RegisterAdapter(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError)
{
    using namespace Private;
    FUnrealMCPAssetFamilyDescriptor Descriptor;
    Descriptor.FamilyId = TEXT("animation_blueprint");
    Descriptor.NativeClass = UAnimInstance::StaticClass();
    Descriptor.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Descriptor.Priority = 160;
    Descriptor.RequiredModules = {
        TEXT("UnrealMCPAnimation"), TEXT("UnrealMCPBlueprint"), TEXT("AnimGraph")};
    Descriptor.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Descriptor.Bounds.MaxValueNodes = 65536;
    Descriptor.Limits = {
        {TEXT("page_size"), UnrealMCP::MaxAssetInspectPageSize},
        {TEXT("selector_bytes"), UnrealMCP::MaxAssetInspectSelectorBytes},
        {TEXT("complete_graph_bytes"), UnrealMCP::MaxAssetInspectCompleteGraphBytes},
        {TEXT("animation_graphs"), MaxAnimationGraphs},
        {TEXT("state_machines"), MaxStateMachines},
        {TEXT("states"), MaxStates},
        {TEXT("transitions"), MaxTransitions},
        {TEXT("parent_overrides"), MaxParentOverrides}};
    Descriptor.Capabilities.bInspection = true;
    Descriptor.SelectorRoutes = Routes();
    Descriptor.bComposableInspectionOverlay = true;
    Descriptor.InspectionAdapter = MakeShared<FAnimationInspectionAdapter>();
    Descriptor.SnapshotBuilder = BuildSnapshot;
    return Registry.Register(MoveTemp(Descriptor), OutError);
}
