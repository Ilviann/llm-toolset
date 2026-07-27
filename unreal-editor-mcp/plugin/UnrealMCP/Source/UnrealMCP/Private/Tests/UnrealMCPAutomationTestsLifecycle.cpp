#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPLifecycleProtocolTest,
    "UnrealMCP.Lifecycle.Protocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPLifecycleProtocolTest::RunTest(const FString& Parameters)
{
    FString Command;
    TSharedPtr<FJsonObject> Arguments;
    FUnrealMCPError Error;

    const auto Parse = [&Command, &Arguments, &Error](const FString& Json)
    {
        FTCHARToUTF8 Encoded(*Json);
        TArray<uint8> Body(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
        return UnrealMCP::Protocol::ParseCommand(Body, Command, Arguments, Error);
    };

    TestTrue(
        TEXT("graceful shutdown accepts no arguments"),
        Parse(TEXT("{\"command\":\"editor_shutdown\",\"arguments\":{}}")));
    TestEqual(TEXT("shutdown command is preserved"), Command, FString(TEXT("editor_shutdown")));
    TestFalse(
        TEXT("graceful shutdown rejects process selection"),
        Parse(TEXT("{\"command\":\"editor_shutdown\",\"arguments\":{\"process_id\":1}}")));
    TestFalse(
        TEXT("graceful shutdown rejects forced termination"),
        Parse(TEXT("{\"command\":\"editor_shutdown\",\"arguments\":{\"force\":true}}")));
    return true;
}


#endif
