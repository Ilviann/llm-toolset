#include "UnrealMCPBlueprintNodeLayout.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "HAL/PlatformTime.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::BlueprintNodeLayout
{
namespace
{
constexpr int32 Grid = 16;
constexpr int32 HorizontalGap = 160;
constexpr int32 VerticalGap = 80;
constexpr int32 ObstaclePaddingX = 64;
constexpr int32 ObstaclePaddingY = 48;
constexpr int32 CommentInset = 32;

struct FRect
{
    int32 Left = 0;
    int32 Top = 0;
    int32 Right = 0;
    int32 Bottom = 0;

    bool Intersects(const FRect& Other) const
    {
        return Left < Other.Right && Right > Other.Left
            && Top < Other.Bottom && Bottom > Other.Top;
    }

    bool Contains(const FRect& Other) const
    {
        return Other.Left >= Left && Other.Right <= Right
            && Other.Top >= Top && Other.Bottom <= Bottom;
    }

    int64 Area() const
    {
        return static_cast<int64>(FMath::Max(0, Right - Left))
            * static_cast<int64>(FMath::Max(0, Bottom - Top));
    }
};

struct FNode
{
    FString Key;
    UEdGraphNode* Object = nullptr;
    int32 Width = 0;
    int32 Height = 0;
    int32 Layer = 0;
};

struct FEdge
{
    int32 From = INDEX_NONE;
    int32 To = INDEX_NONE;
    bool bExecution = false;
};

int32 Snap(int32 Value)
{
    return FMath::RoundToInt(static_cast<double>(Value) / Grid) * Grid;
}

int32 SnapUp(int32 Value)
{
    return FMath::DivideAndRoundUp(Value, Grid) * Grid;
}

FIntPoint EstimateNodeExtent(const UEdGraphNode* Node)
{
    if (Node == nullptr) return FIntPoint(224, 80);
    if (Node->GetClass()->GetName().Contains(TEXT("Knot"))) return FIntPoint(48, 48);

    int32 MaximumText = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Len();
    int32 Inputs = 0;
    int32 Outputs = 0;
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr) continue;
        MaximumText = FMath::Max(MaximumText, Pin->PinName.ToString().Len());
        if (Pin->Direction == EGPD_Input) ++Inputs;
        else if (Pin->Direction == EGPD_Output) ++Outputs;
    }
    const int32 Width = SnapUp(FMath::Clamp(192 + FMath::Min(MaximumText, 96) * 8, 224, 1024));
    const int32 Height = SnapUp(FMath::Clamp(48 + FMath::Max(Inputs, Outputs) * 24, 64, 8192));
    return FIntPoint(Width, Height);
}

FRect NodeRect(const UEdGraphNode* Node)
{
    if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
    {
        const int32 Width = FMath::Max(Comment->NodeWidth, 128);
        const int32 Height = FMath::Max(Comment->NodeHeight, 96);
        return {Comment->NodePosX, Comment->NodePosY,
            Comment->NodePosX + Width, Comment->NodePosY + Height};
    }
    const FIntPoint Extent = EstimateNodeExtent(Node);
    return {Node->NodePosX, Node->NodePosY,
        Node->NodePosX + Extent.X, Node->NodePosY + Extent.Y};
}

FRect Inflate(const FRect& Rect, int32 X, int32 Y)
{
    return {Rect.Left - X, Rect.Top - Y, Rect.Right + X, Rect.Bottom + Y};
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

bool ValidateCoordinate(int32 X, int32 Y, int32 Width, int32 Height)
{
    const int64 Maximum = UnrealMCP::MaxGraphCoordinate;
    return FMath::Abs(static_cast<int64>(X)) <= Maximum
        && FMath::Abs(static_cast<int64>(Y)) <= Maximum
        && FMath::Abs(static_cast<int64>(X) + Width) <= Maximum
        && FMath::Abs(static_cast<int64>(Y) + Height) <= Maximum;
}

bool BuildFingerprint(
    const TArray<FNode>& Nodes,
    const TMap<FString, FPosition>& Positions,
    FString& OutFingerprint)
{
    TArray<FString> Lines{TEXT("policy|layered_v1")};
    for (const FNode& Node : Nodes)
    {
        const FPosition* Position = Positions.Find(Node.Key);
        if (Position == nullptr) return false;
        Lines.Add(FString::Printf(TEXT("node|%s|%d|%d|%d|%d"),
            *Node.Key, Position->X, Position->Y, Node.Width, Node.Height));
    }
    OutFingerprint = HashLines(MoveTemp(Lines));
    return true;
}
}

bool ApplyResolved(
    const TMap<FString, UEdGraphNode*>& NodesByKey,
    const FResult& Result,
    FUnrealMCPError& OutError)
{
    if (NodesByKey.Num() != Result.Positions.Num())
    {
        OutError = {TEXT("internal_error"), TEXT("Resolved layout does not match the complete changed-node set")};
        return false;
    }
    for (const TPair<FString, UEdGraphNode*>& Pair : NodesByKey)
    {
        const FPosition* Position = Result.Positions.Find(Pair.Key);
        const FIntPoint Extent = EstimateNodeExtent(Pair.Value);
        if (Pair.Value == nullptr || Position == nullptr
            || !ValidateCoordinate(Position->X, Position->Y, Extent.X, Extent.Y))
        {
            OutError = {TEXT("graph_limit_exceeded"), TEXT("Resolved layout contains a missing or out-of-bounds node")};
            return false;
        }
    }
    for (const TPair<FString, UEdGraphNode*>& Pair : NodesByKey)
    {
        const FPosition& Position = Result.Positions.FindChecked(Pair.Key);
        Pair.Value->Modify();
        Pair.Value->NodePosX = Position.X;
        Pair.Value->NodePosY = Position.Y;
    }
    return true;
}

bool PlanAndApply(
    UEdGraph* Graph,
    const TMap<FString, UEdGraphNode*>& NodesByKey,
    FResult& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    OutResult = FResult();
    const double StartedAt = FPlatformTime::Seconds();
    int32 Work = 0;
    auto Consume = [&](int32 Amount = 1)
    {
        Work += Amount;
        return Work <= UnrealMCP::MaxLogicUnitLayoutWork
            && FPlatformTime::Seconds() - StartedAt <= UnrealMCP::MaxLogicUnitLayoutSeconds;
    };

    UEdGraphNode* const* EntryObject = NodesByKey.Find(TEXT("$entry"));
    if (Graph == nullptr || EntryObject == nullptr || *EntryObject == nullptr
        || NodesByKey.Num() < 1 || NodesByKey.Num() > UnrealMCP::MaxLogicUnitLayoutNodes)
    {
        OutError = {TEXT("graph_limit_exceeded"), TEXT("Automatic layout requires one bounded complete changed-node set with an entry")};
        return false;
    }

    TArray<FString> Keys;
    NodesByKey.GetKeys(Keys);
    Keys.Sort();
    Keys.Remove(TEXT("$entry"));
    Keys.Insert(TEXT("$entry"), 0);

    TArray<FNode> Nodes;
    TMap<UEdGraphNode*, int32> IndexByObject;
    for (const FString& Key : Keys)
    {
        UEdGraphNode* const* Object = NodesByKey.Find(Key);
        if (Object == nullptr || *Object == nullptr || IndexByObject.Contains(*Object))
        {
            OutError = {TEXT("internal_error"), TEXT("Automatic layout received a missing or duplicate changed node")};
            return false;
        }
        const FIntPoint Extent = EstimateNodeExtent(*Object);
        IndexByObject.Add(*Object, Nodes.Num());
        Nodes.Add(FNode{Key, *Object, Extent.X, Extent.Y, 0});
    }

    TMap<uint64, bool> EdgeKinds;
    for (int32 FromIndex = 0; FromIndex < Nodes.Num(); ++FromIndex)
    {
        for (UEdGraphPin* Pin : Nodes[FromIndex].Object->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Output) continue;
            const bool bExecution = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                const int32* ToIndex = Linked != nullptr
                    ? IndexByObject.Find(Linked->GetOwningNodeUnchecked()) : nullptr;
                if (ToIndex == nullptr) continue;
                const uint64 Identity = (static_cast<uint64>(static_cast<uint32>(FromIndex)) << 32)
                    | static_cast<uint32>(*ToIndex);
                EdgeKinds.FindOrAdd(Identity) = EdgeKinds.FindRef(Identity) || bExecution;
                if (!Consume())
                {
                    OutError = {TEXT("timeout"), TEXT("Automatic layout exceeded its bounded graph-analysis work"), MakeShared<FUnrealMCPRecord>(), true};
                    return false;
                }
            }
        }
    }
    if (EdgeKinds.Num() > UnrealMCP::MaxLogicUnitLayoutEdges)
    {
        OutError = {TEXT("graph_limit_exceeded"), TEXT("Automatic layout exceeds its published edge limit")};
        return false;
    }

    TArray<FEdge> Edges;
    for (const TPair<uint64, bool>& Pair : EdgeKinds)
        Edges.Add(FEdge{static_cast<int32>(Pair.Key >> 32), static_cast<int32>(Pair.Key & 0xffffffffu), Pair.Value});
    Edges.Sort([&](const FEdge& A, const FEdge& B)
    {
        if (Nodes[A.From].Key != Nodes[B.From].Key) return Nodes[A.From].Key < Nodes[B.From].Key;
        if (Nodes[A.To].Key != Nodes[B.To].Key) return Nodes[A.To].Key < Nodes[B.To].Key;
        return A.bExecution && !B.bExecution;
    });

    TArray<TArray<int32>> Adjacency;
    Adjacency.SetNum(Nodes.Num());
    for (const FEdge& Edge : Edges) Adjacency[Edge.From].AddUnique(Edge.To);
    for (TArray<int32>& Values : Adjacency)
        Values.Sort([&](int32 A, int32 B) { return Nodes[A].Key < Nodes[B].Key; });

    TArray<int32> Indices;
    TArray<int32> LowLinks;
    TArray<int32> ComponentByNode;
    TArray<int32> Stack;
    TBitArray<> OnStack(false, Nodes.Num());
    Indices.Init(INDEX_NONE, Nodes.Num());
    LowLinks.Init(INDEX_NONE, Nodes.Num());
    ComponentByNode.Init(INDEX_NONE, Nodes.Num());
    TArray<TArray<int32>> Components;
    int32 NextIndex = 0;
    TFunction<bool(int32)> StrongConnect = [&](int32 NodeIndex)
    {
        Indices[NodeIndex] = NextIndex;
        LowLinks[NodeIndex] = NextIndex++;
        Stack.Add(NodeIndex);
        OnStack[NodeIndex] = true;
        for (int32 Target : Adjacency[NodeIndex])
        {
            if (!Consume()) return false;
            if (Indices[Target] == INDEX_NONE)
            {
                if (!StrongConnect(Target)) return false;
                LowLinks[NodeIndex] = FMath::Min(LowLinks[NodeIndex], LowLinks[Target]);
            }
            else if (OnStack[Target])
            {
                LowLinks[NodeIndex] = FMath::Min(LowLinks[NodeIndex], Indices[Target]);
            }
        }
        if (LowLinks[NodeIndex] == Indices[NodeIndex])
        {
            TArray<int32> Component;
            while (!Stack.IsEmpty())
            {
                const int32 Member = Stack.Pop(EAllowShrinking::No);
                OnStack[Member] = false;
                ComponentByNode[Member] = Components.Num();
                Component.Add(Member);
                if (Member == NodeIndex) break;
            }
            Component.Sort([&](int32 A, int32 B)
            {
                if (A == 0 || B == 0) return A == 0;
                return Nodes[A].Key < Nodes[B].Key;
            });
            Components.Add(MoveTemp(Component));
        }
        return true;
    };
    for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
        if (Indices[NodeIndex] == INDEX_NONE && !StrongConnect(NodeIndex))
        {
            OutError = {TEXT("timeout"), TEXT("Automatic layout exceeded its bounded cycle-analysis work"), MakeShared<FUnrealMCPRecord>(), true};
            return false;
        }

    TArray<FString> ComponentKeys;
    TArray<int32> ComponentSpan;
    TArray<int32> ComponentBase;
    TArray<int32> ComponentIndegree;
    ComponentKeys.SetNum(Components.Num());
    ComponentSpan.SetNum(Components.Num());
    ComponentBase.Init(1, Components.Num());
    ComponentIndegree.Init(0, Components.Num());
    const int32 EntryComponent = ComponentByNode[0];
    ComponentBase[EntryComponent] = 0;
    for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
    {
        ComponentKeys[ComponentIndex] = Nodes[Components[ComponentIndex][0]].Key;
        ComponentSpan[ComponentIndex] = FMath::Max(1, Components[ComponentIndex].Num());
    }
    TArray<TSet<int32>> ComponentAdjacency;
    ComponentAdjacency.SetNum(Components.Num());
    for (const FEdge& Edge : Edges)
    {
        const int32 From = ComponentByNode[Edge.From];
        const int32 To = ComponentByNode[Edge.To];
        if (From != To && !ComponentAdjacency[From].Contains(To))
        {
            ComponentAdjacency[From].Add(To);
            ++ComponentIndegree[To];
        }
    }
    TArray<int32> Ready;
    for (int32 Index = 0; Index < Components.Num(); ++Index)
        if (ComponentIndegree[Index] == 0) Ready.Add(Index);
    auto SortReady = [&]()
    {
        Ready.Sort([&](int32 A, int32 B) { return ComponentKeys[A] < ComponentKeys[B]; });
    };
    SortReady();
    int32 VisitedComponents = 0;
    while (!Ready.IsEmpty())
    {
        const int32 Current = Ready[0];
        Ready.RemoveAt(0, 1, EAllowShrinking::No);
        ++VisitedComponents;
        TArray<int32> Targets = ComponentAdjacency[Current].Array();
        Targets.Sort([&](int32 A, int32 B) { return ComponentKeys[A] < ComponentKeys[B]; });
        for (int32 Target : Targets)
        {
            ComponentBase[Target] = FMath::Max(
                ComponentBase[Target], ComponentBase[Current] + ComponentSpan[Current]);
            if (--ComponentIndegree[Target] == 0) Ready.Add(Target);
            if (!Consume())
            {
                OutError = {TEXT("timeout"), TEXT("Automatic layout exceeded its bounded layer-assignment work"), MakeShared<FUnrealMCPRecord>(), true};
                return false;
            }
        }
        SortReady();
    }
    if (VisitedComponents != Components.Num())
    {
        OutError = {TEXT("internal_error"), TEXT("Automatic layout could not condense the changed-node graph")};
        return false;
    }
    int32 MaximumLayer = 0;
    for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
        for (int32 Offset = 0; Offset < Components[ComponentIndex].Num(); ++Offset)
        {
            const int32 NodeIndex = Components[ComponentIndex][Offset];
            Nodes[NodeIndex].Layer = ComponentBase[ComponentIndex] + Offset;
            MaximumLayer = FMath::Max(MaximumLayer, Nodes[NodeIndex].Layer);
        }

    TArray<TArray<int32>> Layers;
    Layers.SetNum(MaximumLayer + 1);
    for (int32 Index = 0; Index < Nodes.Num(); ++Index) Layers[Nodes[Index].Layer].Add(Index);
    for (TArray<int32>& Layer : Layers)
        Layer.Sort([&](int32 A, int32 B) { return Nodes[A].Key < Nodes[B].Key; });

    auto Sweep = [&](bool bForward)
    {
        TArray<int32> Rank;
        Rank.Init(0, Nodes.Num());
        for (const TArray<int32>& Layer : Layers)
            for (int32 Position = 0; Position < Layer.Num(); ++Position) Rank[Layer[Position]] = Position;
        const int32 Start = bForward ? 1 : MaximumLayer - 1;
        const int32 End = bForward ? MaximumLayer + 1 : -1;
        const int32 Step = bForward ? 1 : -1;
        for (int32 LayerIndex = Start; LayerIndex != End; LayerIndex += Step)
        {
            TMap<int32, TPair<int64, int64>> Scores;
            for (int32 NodeIndex : Layers[LayerIndex]) Scores.Add(NodeIndex, {0, 0});
            for (const FEdge& Edge : Edges)
            {
                const int32 Candidate = bForward ? Edge.To : Edge.From;
                const int32 Neighbor = bForward ? Edge.From : Edge.To;
                if (Nodes[Candidate].Layer != LayerIndex) continue;
                TPair<int64, int64>& Score = Scores.FindChecked(Candidate);
                Score.Key += Rank[Neighbor] * (Edge.bExecution ? 2 : 1);
                Score.Value += Edge.bExecution ? 2 : 1;
            }
            Layers[LayerIndex].StableSort([&](int32 A, int32 B)
            {
                const TPair<int64, int64>& SA = Scores.FindChecked(A);
                const TPair<int64, int64>& SB = Scores.FindChecked(B);
                if (SA.Value > 0 && SB.Value > 0 && SA.Key * SB.Value != SB.Key * SA.Value)
                    return SA.Key * SB.Value < SB.Key * SA.Value;
                if (SA.Value != SB.Value) return SA.Value > SB.Value;
                return Nodes[A].Key < Nodes[B].Key;
            });
        }
    };
    for (int32 Iteration = 0; Iteration < UnrealMCP::MaxLogicUnitLayoutIterations; ++Iteration)
    {
        Sweep(Iteration % 2 == 0);
        ++OutResult.Iterations;
        if (!Consume(Nodes.Num() + Edges.Num()))
        {
            OutError = {TEXT("timeout"), TEXT("Automatic layout exceeded its bounded crossing-reduction work"), MakeShared<FUnrealMCPRecord>(), true};
            return false;
        }
    }

    const FRect EntryRect = NodeRect(Nodes[0].Object);
    TOptional<FRect> Container;
    TArray<FRect> Obstacles;
    TSet<UEdGraphNode*> ChangedObjects;
    for (const FNode& Node : Nodes) ChangedObjects.Add(Node.Object);
    for (UEdGraphNode* Candidate : Graph->Nodes)
    {
        if (Candidate == nullptr || ChangedObjects.Contains(Candidate)) continue;
        const FRect Rect = NodeRect(Candidate);
        if (Cast<UEdGraphNode_Comment>(Candidate) != nullptr && Rect.Contains(EntryRect))
        {
            const FRect Inner{Rect.Left + CommentInset, Rect.Top + CommentInset,
                Rect.Right - CommentInset, Rect.Bottom - CommentInset};
            if (!Container.IsSet() || Inner.Area() < Container->Area()) Container = Inner;
            continue;
        }
        Obstacles.Add(Inflate(Rect, ObstaclePaddingX, ObstaclePaddingY));
    }

    TArray<int32> LayerWidths;
    TArray<int32> LayerX;
    LayerWidths.Init(0, Layers.Num());
    LayerX.Init(0, Layers.Num());
    for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
        for (int32 NodeIndex : Layers[LayerIndex])
            LayerWidths[LayerIndex] = FMath::Max(LayerWidths[LayerIndex], Nodes[NodeIndex].Width);
    LayerX[0] = Nodes[0].Object->NodePosX;
    for (int32 LayerIndex = 1; LayerIndex < Layers.Num(); ++LayerIndex)
        LayerX[LayerIndex] = Snap(LayerX[LayerIndex - 1] + LayerWidths[LayerIndex - 1] + HorizontalGap);

    TArray<FRect> Occupied;
    OutResult.Positions.Add(TEXT("$entry"), FPosition{Nodes[0].Object->NodePosX, Nodes[0].Object->NodePosY});
    Occupied.Add(Inflate(EntryRect, ObstaclePaddingX, ObstaclePaddingY));
    for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
    {
        int32 TotalHeight = 0;
        for (int32 NodeIndex : Layers[LayerIndex])
            if (NodeIndex != 0) TotalHeight += Nodes[NodeIndex].Height + VerticalGap;
        if (TotalHeight > 0) TotalHeight -= VerticalGap;
        int32 IdealY = Snap(Nodes[0].Object->NodePosY + Nodes[0].Height / 2 - TotalHeight / 2);
        for (int32 NodeIndex : Layers[LayerIndex])
        {
            if (NodeIndex == 0) continue;
            const FNode& Node = Nodes[NodeIndex];
            bool bPlaced = false;
            for (int32 Probe = 0; Probe < UnrealMCP::MaxLogicUnitLayoutCollisionProbes; ++Probe)
            {
                const int32 Distance = (Probe + 1) / 2;
                const int32 Sign = Probe == 0 ? 0 : (Probe % 2 == 1 ? -1 : 1);
                const int32 X = LayerX[LayerIndex];
                const int32 Y = Snap(IdealY + Sign * Distance * VerticalGap);
                const FRect Rect{X, Y, X + Node.Width, Y + Node.Height};
                bool bCollision = Container.IsSet() && !Container->Contains(Rect);
                for (const FRect& Obstacle : Obstacles)
                    if (Rect.Intersects(Obstacle)) { bCollision = true; break; }
                if (!bCollision)
                    for (const FRect& Existing : Occupied)
                        if (Rect.Intersects(Existing)) { bCollision = true; break; }
                if (!Consume(1 + Obstacles.Num() + Occupied.Num()))
                {
                    OutError = {TEXT("timeout"), TEXT("Automatic layout exceeded its bounded collision-search work"), MakeShared<FUnrealMCPRecord>(), true};
                    return false;
                }
                if (bCollision || !ValidateCoordinate(X, Y, Node.Width, Node.Height)) continue;
                OutResult.Positions.Add(Node.Key, FPosition{X, Y});
                Occupied.Add(Inflate(Rect, ObstaclePaddingX, ObstaclePaddingY));
                IdealY = Y + Node.Height + VerticalGap;
                bPlaced = true;
                break;
            }
            if (!bPlaced)
            {
                OutError = {TEXT("graph_limit_exceeded"),
                    TEXT("Automatic layout could not place every changed node within graph, comment, collision, and work bounds")};
                return false;
            }
        }
    }
    if (OutResult.Positions.Num() != Nodes.Num() || !BuildFingerprint(Nodes, OutResult.Positions, OutResult.Fingerprint))
    {
        OutError = {TEXT("internal_error"), TEXT("Automatic layout did not resolve one exact position per changed node")};
        return false;
    }

    OutResult.MinX = TNumericLimits<int32>::Max();
    OutResult.MinY = TNumericLimits<int32>::Max();
    OutResult.MaxX = TNumericLimits<int32>::Lowest();
    OutResult.MaxY = TNumericLimits<int32>::Lowest();
    for (const FNode& Node : Nodes)
    {
        const FPosition& Position = OutResult.Positions.FindChecked(Node.Key);
        OutResult.MinX = FMath::Min(OutResult.MinX, Position.X);
        OutResult.MinY = FMath::Min(OutResult.MinY, Position.Y);
        OutResult.MaxX = FMath::Max(OutResult.MaxX, Position.X + Node.Width);
        OutResult.MaxY = FMath::Max(OutResult.MaxY, Position.Y + Node.Height);
    }
    return ApplyResolved(NodesByKey, OutResult, OutError);
}
}
