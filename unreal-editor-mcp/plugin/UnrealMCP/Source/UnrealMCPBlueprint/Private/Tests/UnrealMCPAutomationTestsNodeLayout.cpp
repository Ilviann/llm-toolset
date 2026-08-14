#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPBlueprintAutomationTestSupport.h"

#include "EdGraphNode_Comment.h"
#include "K2Node_IfThenElse.h"
#include "UnrealMCPBlueprintNodeLayout.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPNodeLayoutTest,
    "UnrealMCP.NodeLayout.DeterministicChangedNodes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPNodeLayoutTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::BlueprintNodeLayout;

    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_NodeLayout");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("Node-layout Blueprint fixture is created"), Blueprint)) return false;
    UEdGraph* Graph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Node-layout fixture has an event graph"), Graph)) return false;

    auto AddBranch = [&](int32 X, int32 Y)
    {
        UK2Node_IfThenElse* Node = NewObject<UK2Node_IfThenElse>(Graph);
        Node->SetFlags(RF_Transactional);
        Graph->AddNode(Node, true, false);
        Node->CreateNewGuid();
        Node->AllocateDefaultPins();
        Node->NodePosX = X;
        Node->NodePosY = Y;
        for (UEdGraphPin* Pin : Node->Pins)
            if (Pin != nullptr && !Pin->PinId.IsValid()) Pin->PinId = FGuid::NewGuid();
        return Node;
    };

    UK2Node_IfThenElse* Entry = AddBranch(0, 0);
    UK2Node_IfThenElse* First = AddBranch(0, 0);
    UK2Node_IfThenElse* Second = AddBranch(0, 0);
    UK2Node_IfThenElse* Join = AddBranch(0, 0);
    UK2Node_IfThenElse* Conversion = AddBranch(0, 0);
    UK2Node_IfThenElse* Untouched = AddBranch(480, -80);
    Entry->GetThenPin()->MakeLinkTo(First->GetExecPin());
    First->GetThenPin()->MakeLinkTo(Second->GetExecPin());
    First->GetElsePin()->MakeLinkTo(Join->GetExecPin());
    Second->GetThenPin()->MakeLinkTo(First->GetExecPin());
    Second->GetElsePin()->MakeLinkTo(Join->GetExecPin());
    Join->GetThenPin()->MakeLinkTo(Conversion->GetExecPin());

    UEdGraphNode_Comment* Container = NewObject<UEdGraphNode_Comment>(Graph);
    Graph->AddNode(Container, true, false);
    Container->CreateNewGuid();
    Container->NodePosX = -256;
    Container->NodePosY = -2048;
    Container->NodeWidth = 4096;
    Container->NodeHeight = 4096;

    UEdGraphNode_Comment* ObstacleComment = NewObject<UEdGraphNode_Comment>(Graph);
    Graph->AddNode(ObstacleComment, true, false);
    ObstacleComment->CreateNewGuid();
    ObstacleComment->NodePosX = 320;
    ObstacleComment->NodePosY = 160;
    ObstacleComment->NodeWidth = 480;
    ObstacleComment->NodeHeight = 320;

    TMap<FString, UEdGraphNode*> NodesByKey;
    NodesByKey.Add(TEXT("$entry"), Entry);
    NodesByKey.Add(TEXT("first"), First);
    NodesByKey.Add(TEXT("second"), Second);
    NodesByKey.Add(TEXT("join"), Join);
    NodesByKey.Add(TEXT("$conversion_0"), Conversion);

    const FIntPoint UntouchedPosition(Untouched->NodePosX, Untouched->NodePosY);
    const FIntRect ContainerBounds(Container->NodePosX, Container->NodePosY,
        Container->NodePosX + Container->NodeWidth, Container->NodePosY + Container->NodeHeight);
    const FIntRect ObstacleBounds(ObstacleComment->NodePosX, ObstacleComment->NodePosY,
        ObstacleComment->NodePosX + ObstacleComment->NodeWidth,
        ObstacleComment->NodePosY + ObstacleComment->NodeHeight);

    FResult FirstResult;
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("Layered layout accepts a bounded branch, join, cycle, comments, and obstacle"),
        PlanAndApply(Graph, NodesByKey, FirstResult, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(TEXT("Entry remains the fixed layout anchor"), Entry->NodePosX, 0);
    TestEqual(TEXT("Entry Y remains fixed"), Entry->NodePosY, 0);
    TestEqual(TEXT("Untouched node X is preserved"), Untouched->NodePosX, UntouchedPosition.X);
    TestEqual(TEXT("Untouched node Y is preserved"), Untouched->NodePosY, UntouchedPosition.Y);
    TestEqual(TEXT("Containing comment remains fixed"),
        FIntRect(Container->NodePosX, Container->NodePosY,
            Container->NodePosX + Container->NodeWidth, Container->NodePosY + Container->NodeHeight),
        ContainerBounds);
    TestEqual(TEXT("Obstacle comment remains fixed"),
        FIntRect(ObstacleComment->NodePosX, ObstacleComment->NodePosY,
            ObstacleComment->NodePosX + ObstacleComment->NodeWidth,
            ObstacleComment->NodePosY + ObstacleComment->NodeHeight),
        ObstacleBounds);
    TestEqual(TEXT("Every changed and inserted conversion node receives one resolved position"),
        FirstResult.Positions.Num(), 5);
    TestEqual(TEXT("Crossing reduction uses the published fixed iteration count"),
        FirstResult.Iterations, UnrealMCP::MaxLogicUnitLayoutIterations);
    TestEqual(TEXT("Layout fingerprint is stable-sized"), FirstResult.Fingerprint.Len(), 40);

    for (const TPair<FString, UEdGraphNode*>& Pair : NodesByKey)
        if (Pair.Key != TEXT("$entry"))
        {
            Pair.Value->NodePosX = -1500;
            Pair.Value->NodePosY = -1500;
        }
    FResult SecondResult;
    Error = FUnrealMCPError();
    TestTrue(TEXT("Equivalent layout can be repeated"), PlanAndApply(Graph, NodesByKey, SecondResult, Error));
    TestEqual(TEXT("Equivalent layout fingerprint is deterministic"),
        SecondResult.Fingerprint, FirstResult.Fingerprint);
    for (const TPair<FString, UnrealMCP::BlueprintBlockReplacement::FPosition>& Pair : FirstResult.Positions)
    {
        const UnrealMCP::BlueprintBlockReplacement::FPosition* Repeated = SecondResult.Positions.Find(Pair.Key);
        TestTrue(TEXT("Repeated layout retains every semantic key"), Repeated != nullptr);
        if (Repeated != nullptr)
        {
            TestEqual(TEXT("Repeated layout X is identical"), Repeated->X, Pair.Value.X);
            TestEqual(TEXT("Repeated layout Y is identical"), Repeated->Y, Pair.Value.Y);
        }
    }

    First->NodePosX = -3000;
    First->NodePosY = -3000;
    Error = FUnrealMCPError();
    TestTrue(TEXT("Resolved scratch positions can be applied without replanning"),
        ApplyResolved(NodesByKey, FirstResult, Error));
    TestEqual(TEXT("Resolved live X matches scratch"), First->NodePosX,
        FirstResult.Positions.FindChecked(TEXT("first")).X);
    TestEqual(TEXT("Resolved live Y matches scratch"), First->NodePosY,
        FirstResult.Positions.FindChecked(TEXT("first")).Y);

    Container->NodePosX = -32;
    Container->NodePosY = -32;
    Container->NodeWidth = 320;
    Container->NodeHeight = 192;
    const FIntPoint BeforeRejectedLayout(First->NodePosX, First->NodePosY);
    FResult RejectedResult;
    Error = FUnrealMCPError();
    TestFalse(TEXT("A containing comment with no bounded placement space rejects the layout"),
        PlanAndApply(Graph, NodesByKey, RejectedResult, Error));
    TestEqual(TEXT("Bounded layout rejection reports the graph limit"),
        Error.Code, FString(TEXT("graph_limit_exceeded")));
    TestEqual(TEXT("Rejected planning does not partially move a changed node"),
        FIntPoint(First->NodePosX, First->NodePosY), BeforeRejectedLayout);
    return true;
}

#endif
