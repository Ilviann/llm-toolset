#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPHostAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPLifecycleProtocolTest,
    "UnrealMCP.Lifecycle.Protocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPLifecycleProtocolTest::RunTest(const FString& Parameters)
{
    FUnrealMCPCommandRequest Request;
    FUnrealMCPError Error;

    const auto Parse = [&Request, &Error](const FString& Json)
    {
        FTCHARToUTF8 Encoded(*Json);
        TArray<uint8> Body(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
        return UnrealMCP::Protocol::ParseCommand(Body, Request, Error);
    };

    TestTrue(
        TEXT("graceful shutdown accepts no arguments"),
        Parse(TEXT("{\"command\":\"editor_shutdown\",\"arguments\":{}}")));
    TestEqual(TEXT("shutdown command is preserved"), Request.Command, FString(TEXT("editor_shutdown")));
    TestFalse(
        TEXT("graceful shutdown rejects process selection"),
        Parse(TEXT("{\"command\":\"editor_shutdown\",\"arguments\":{\"process_id\":1}}")));
    TestFalse(
        TEXT("graceful shutdown rejects forced termination"),
        Parse(TEXT("{\"command\":\"editor_shutdown\",\"arguments\":{\"force\":true}}")));
    return true;
}


#endif
