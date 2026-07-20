#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "HAL/FileManager.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMCPCompatibility.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPTokenStore.h"
#include "UnrealMCPVersion.h"

namespace
{
const FHttpPath Phase1RoutePath(TEXT("/unreal-mcp/v1/command"));

TSharedPtr<FHttpServerRequest> MakeRequest(const FString& Authorization, int32 BodyBytes = INDEX_NONE)
{
    const TSharedPtr<FHttpServerRequest> Request = MakeShared<FHttpServerRequest>();
    Request->RelativePath = Phase1RoutePath;
    Request->Verb = EHttpServerRequestVerbs::VERB_POST;
    Request->Headers.FindOrAdd(TEXT("authorization")).Add(Authorization);
    if (BodyBytes == INDEX_NONE)
    {
        const FString Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}");
        FTCHARToUTF8 Encoded(*Json);
        Request->Body.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    }
    else
    {
        Request->Body.SetNumZeroed(BodyBytes);
    }
    return Request;
}

bool LoadLiveToken(FString& OutToken)
{
    if (!FFileHelper::LoadFileToString(OutToken, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("bridge.token"))))
    {
        return false;
    }
    OutToken.TrimStartAndEndInline();
    return FUnrealMCPTokenStore::IsValidToken(OutToken);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPTokenPersistenceTest, "UnrealMCP.Phase1.TokenPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPTokenPersistenceTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString First;
    FString Error;
    TestTrue(TEXT("token is created"), FUnrealMCPTokenStore::LoadOrCreate(Directory, First, Error));
    TestTrue(TEXT("token format is valid"), FUnrealMCPTokenStore::IsValidToken(First));
    FString Second;
    TestTrue(TEXT("token is reloaded"), FUnrealMCPTokenStore::LoadOrCreate(Directory, Second, Error));
    TestEqual(TEXT("persisted token is stable"), Second, First);
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPInvalidTokenFailsClosedTest, "UnrealMCP.Phase1.InvalidTokenFailsClosed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPInvalidTokenFailsClosedTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FFileHelper::SaveStringToFile(TEXT("weak"), *FPaths::Combine(Directory, TEXT("bridge.token")));
    FString Token;
    FString Error;
    TestFalse(TEXT("invalid persisted token is rejected"), FUnrealMCPTokenStore::LoadOrCreate(Directory, Token, Error));
    TestTrue(TEXT("token is not returned"), Token.IsEmpty());
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPProtocolBoundsTest, "UnrealMCP.Phase1.ProtocolBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPProtocolBoundsTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("matching secrets compare"), UnrealMCP::Protocol::ConstantTimeEquals(TEXT("abc"), TEXT("abc")));
    TestFalse(TEXT("different secrets reject"), UnrealMCP::Protocol::ConstantTimeEquals(TEXT("abc"), TEXT("abd")));
    TestFalse(TEXT("different lengths reject"), UnrealMCP::Protocol::ConstantTimeEquals(TEXT("abc"), TEXT("ab")));
    const FString Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}");
    FTCHARToUTF8 Encoded(*Json);
    TArray<uint8> Body(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    FString Command;
    TSharedPtr<FJsonObject> Arguments;
    FUnrealMCPError Error;
    TestTrue(TEXT("valid command parses"), UnrealMCP::Protocol::ParseCommand(Body, Command, Arguments, Error));
    TestEqual(TEXT("command preserved"), Command, FString(TEXT("capabilities")));
    TArray<uint8> Large;
    Large.SetNumZeroed(UnrealMCP::MaxRequestBytes + 1);
    TestFalse(TEXT("oversized body rejects"), UnrealMCP::Protocol::ParseCommand(Large, Command, Arguments, Error));
    const FString DeepJson = TEXT("{\"command\":\"capabilities\",\"arguments\":") + FString::ChrN(20, TEXT('[')) + FString::ChrN(20, TEXT(']')) + TEXT("}");
    FTCHARToUTF8 DeepEncoded(*DeepJson);
    TArray<uint8> DeepBody(reinterpret_cast<const uint8*>(DeepEncoded.Get()), DeepEncoded.Length());
    TestFalse(TEXT("excessive JSON depth rejects"), UnrealMCP::Protocol::ParseCommand(DeepBody, Command, Arguments, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPErrorEnvelopeTest, "UnrealMCP.Phase1.ErrorEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPErrorEnvelopeTest::RunTest(const FString& Parameters)
{
    TUniquePtr<FHttpServerResponse> Response = UnrealMCP::Protocol::Error(EHttpServerResponseCodes::Denied, TEXT("authentication_failed"), FString::ChrN(2000, TEXT('x')));
    TestTrue(TEXT("error response is bounded"), Response->Body.Num() <= UnrealMCP::MaxResponseBytes);
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Response->Body.GetData()), Response->Body.Num());
    TSharedPtr<FJsonObject> Root;
    TestTrue(TEXT("error response is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Converted.Length(), Converted.Get())), Root));
    TestFalse(TEXT("error envelope reports failure"), Root->GetBoolField(TEXT("ok")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCompatibilityBranchTest, "UnrealMCP.Phase1.CompatibilityBranch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPCompatibilityBranchTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("current public APIs are supported"), UnrealMCP::Compatibility::SupportsCurrentEngine());
    TestFalse(TEXT("API line is reported"), UnrealMCP::Compatibility::EngineApiLine().IsEmpty());
    TestTrue(TEXT("automation executes on Game thread"), IsInGameThread());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPRouteGuardsTest, "UnrealMCP.Phase1.RouteGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPRouteGuardsTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IHttpRouter> Router = FHttpServerModule::Get().GetHttpRouter(UnrealMCP::DefaultPort, true);
    TestTrue(TEXT("live bridge router is available"), Router.IsValid());
    if (!Router.IsValid())
    {
        return false;
    }

    const FHttpRequestHandler DuplicateHandler = FHttpRequestHandler::CreateLambda([](const FHttpServerRequest&, const FHttpResultCallback&)
    {
        return true;
    });
    const FHttpRouteHandle DuplicateRoute = Router->BindRoute(Phase1RoutePath, EHttpServerRequestVerbs::VERB_POST, DuplicateHandler);
    TestFalse(TEXT("duplicate route ownership is rejected"), DuplicateRoute.IsValid());

    TUniquePtr<FHttpServerResponse> AuthenticationResponse;
    const bool bAuthenticationMatched = Router->Query(
        MakeRequest(TEXT("Bearer invalid")),
        [&AuthenticationResponse](TUniquePtr<FHttpServerResponse>&& Response)
        {
            AuthenticationResponse = MoveTemp(Response);
        });
    TestTrue(TEXT("authentication request reaches bridge route"), bAuthenticationMatched);
    TestTrue(TEXT("authentication rejection completes synchronously"), AuthenticationResponse.IsValid());
    if (AuthenticationResponse.IsValid())
    {
        TestEqual(TEXT("invalid authentication is denied"), static_cast<int32>(AuthenticationResponse->Code), static_cast<int32>(EHttpServerResponseCodes::Denied));
    }

    FString Token;
    TestTrue(TEXT("live token is readable"), LoadLiveToken(Token));
    TUniquePtr<FHttpServerResponse> BoundsResponse;
    const bool bBoundsMatched = Router->Query(
        MakeRequest(FString(TEXT("Bearer ")) + Token, UnrealMCP::MaxRequestBytes + 1),
        [&BoundsResponse](TUniquePtr<FHttpServerResponse>&& Response)
        {
            BoundsResponse = MoveTemp(Response);
        });
    TestTrue(TEXT("oversized request reaches bridge route"), bBoundsMatched);
    TestTrue(TEXT("oversized request completes synchronously"), BoundsResponse.IsValid());
    if (BoundsResponse.IsValid())
    {
        TestEqual(TEXT("oversized authenticated request is rejected"), static_cast<int32>(BoundsResponse->Code), static_cast<int32>(EHttpServerResponseCodes::RequestTooLarge));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPGameThreadDispatchTest, "UnrealMCP.Phase1.GameThreadDispatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPGameThreadDispatchTest::RunTest(const FString& Parameters)
{
    struct FDispatchState
    {
        bool bCompleted = false;
        bool bOnGameThread = false;
        int32 ResponseCode = static_cast<int32>(EHttpServerResponseCodes::Unknown);
    };

    const TSharedPtr<IHttpRouter> Router = FHttpServerModule::Get().GetHttpRouter(UnrealMCP::DefaultPort, true);
    TestTrue(TEXT("live bridge router is available"), Router.IsValid());
    FString Token;
    TestTrue(TEXT("live token is readable"), LoadLiveToken(Token));
    if (!Router.IsValid() || !FUnrealMCPTokenStore::IsValidToken(Token))
    {
        return false;
    }

    const TSharedRef<FDispatchState> State = MakeShared<FDispatchState>();
    const bool bMatched = Router->Query(
        MakeRequest(FString(TEXT("Bearer ")) + Token),
        [State](TUniquePtr<FHttpServerResponse>&& Response)
        {
            State->bOnGameThread = IsInGameThread();
            State->ResponseCode = Response.IsValid() ? static_cast<int32>(Response->Code) : static_cast<int32>(EHttpServerResponseCodes::Unknown);
            State->bCompleted = true;
        });
    TestTrue(TEXT("valid request reaches bridge route"), bMatched);
    TestFalse(TEXT("valid request is queued before callback"), State->bCompleted);

    AddCommand(new FDelayedFunctionLatentCommand(
        [this, State]()
        {
            TestTrue(TEXT("queued request completes"), State->bCompleted);
            TestTrue(TEXT("bridge callback runs on Game thread"), State->bOnGameThread);
            TestEqual(TEXT("valid request succeeds"), State->ResponseCode, static_cast<int32>(EHttpServerResponseCodes::Ok));
        },
        0.1f));
    return true;
}

#endif
