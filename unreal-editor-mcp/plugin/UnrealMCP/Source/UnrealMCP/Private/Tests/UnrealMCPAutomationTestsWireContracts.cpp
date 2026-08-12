#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpServerResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPJsonCodec.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPVersion.h"

namespace
{
FString ResponseText(const TUniquePtr<FHttpServerResponse>& Response)
{
    if (!Response.IsValid()) return FString();
    FUTF8ToTCHAR Converted(
        reinterpret_cast<const ANSICHAR*>(Response->Body.GetData()), Response->Body.Num());
    return FString(Converted.Length(), Converted.Get());
}

FString CanonicalFixture(const FString& Text)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return FString();
    FString Result;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
    return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) ? Result : FString();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPWireRoundTripTest,
    "UnrealMCP.WireContracts.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPWireRoundTripTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FUnrealMCPRecord> Nested = MakeShared<FUnrealMCPRecord>();
    Nested->SetStringField(TEXT("identity"), TEXT("0123456789abcdef"));
    Nested->SetField(TEXT("optional"), MakeShared<FUnrealMCPValueNull>());

    const TSharedRef<FUnrealMCPRecord> Original = MakeShared<FUnrealMCPRecord>();
    Original->SetStringField(TEXT("selector"), TEXT("summary/components"));
    Original->SetNumberField(TEXT("page_size"), 25);
    Original->SetBoolField(TEXT("truncated"), false);
    Original->SetArrayField(TEXT("diagnostics"), {
        MakeShared<FUnrealMCPValueString>(TEXT("first")),
        MakeShared<FUnrealMCPValueNumber>(2.0),
        MakeShared<FUnrealMCPValueBoolean>(true)});
    Original->SetObjectField(TEXT("mutation"), Nested);

    FUnrealMCPError Error;
    TSharedPtr<FUnrealMCPRecord> Decoded;
    TestTrue(TEXT("common wire values decode after JSON encoding"),
        UnrealMCP::JsonCodec::DecodeRecord(
            UnrealMCP::JsonCodec::EncodeRecord(Original), Decoded, Error));
    TestTrue(TEXT("round trip preserves every value and omission"),
        Decoded.IsValid() && FUnrealMCPValue::CompareEqual(
            *MakeShared<FUnrealMCPValueObject>(Original),
            *MakeShared<FUnrealMCPValueObject>(Decoded)));

    FUnrealMCPCommandRequest Request;
    const FString RequestJson = TEXT("{\"command\":\"asset_inspect\",\"arguments\":{\"asset_path\":\"/Game/Test.Test\"}}");
    FTCHARToUTF8 RequestUtf8(*RequestJson);
    TArray<uint8> RequestBody(
        reinterpret_cast<const uint8*>(RequestUtf8.Get()), RequestUtf8.Length());
    TestTrue(TEXT("typed command request decodes"),
        UnrealMCP::Protocol::ParseCommand(RequestBody, Request, Error));
    TestEqual(TEXT("typed request preserves command"), Request.Command, FString(TEXT("asset_inspect")));
    TestEqual(TEXT("typed request preserves arguments"),
        Request.Arguments->GetStringField(TEXT("asset_path")), FString(TEXT("/Game/Test.Test")));

    FUnrealMCPCapabilityRecord Capability{TEXT("asset_inspect"), true, {{TEXT("records"), 4096}}};
    FUnrealMCPIdentityRecord Identity{TEXT("id"), TEXT("asset"), TEXT("/Game/Test.Test"), TEXT("snapshot")};
    FUnrealMCPSelectorRecord Selector{TEXT("summary"), {TEXT("summary")}};
    FUnrealMCPPagingRecord Paging{0, 25, FString(), TEXT("next"), true};
    FUnrealMCPDiagnosticRecord Diagnostic{TEXT("warning"), TEXT("bounded"), TEXT("warning")};
    FUnrealMCPMutationRecord Mutation{TEXT("operation"), TEXT("committed"), TEXT("bridge"), TEXT("digest")};
    FUnrealMCPPersistenceRecord Persistence{TEXT("/Game/Test"), true, true};
    FUnrealMCPResultRecord ResultRecord{Original};
    TestTrue(TEXT("common typed records retain values"),
        Capability.bAvailable && Capability.Limits[TEXT("records")] == 4096
        && Identity.Kind == TEXT("asset") && Selector.Segments.Num() == 1
        && Paging.bTruncated && Diagnostic.Severity == TEXT("warning")
        && Mutation.OperationState == TEXT("committed") && Persistence.bVerified
        && ResultRecord.Data == Original);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPWireInvalidInputTest,
    "UnrealMCP.WireContracts.InvalidInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPWireInvalidInputTest::RunTest(const FString& Parameters)
{
    FUnrealMCPError Error;
    TSharedPtr<FUnrealMCPRecord> Decoded;
    const TSharedRef<FJsonObject> Oversized = MakeShared<FJsonObject>();
    Oversized->SetStringField(TEXT("value"), FString::ChrN(UnrealMCP::MaxStringLength + 1, TEXT('x')));
    TestFalse(TEXT("oversized strings fail closed"),
        UnrealMCP::JsonCodec::DecodeRecord(Oversized, Decoded, Error));
    TestEqual(TEXT("oversized string error is stable"), Error.Code, FString(TEXT("invalid_argument")));

    const TSharedRef<FJsonObject> NonFinite = MakeShared<FJsonObject>();
    NonFinite->SetField(TEXT("value"), MakeShared<FJsonValueNumber>(std::numeric_limits<double>::quiet_NaN()));
    TestFalse(TEXT("non-finite numbers fail closed"),
        UnrealMCP::JsonCodec::DecodeRecord(NonFinite, Decoded, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPWireEnvelopeFixtureTest,
    "UnrealMCP.WireContracts.EnvelopeFixtures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPWireEnvelopeFixtureTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("fixture"));
    Result->SetNumberField(TEXT("count"), 2);
    Result->SetBoolField(TEXT("enabled"), true);
    TestEqual(TEXT("complete success fixture remains byte-for-byte stable"),
        CanonicalFixture(ResponseText(UnrealMCP::Protocol::Success(Result))),
        FString(TEXT("{\"ok\":true,\"result\":{\"kind\":\"fixture\",\"count\":2,\"enabled\":true}}")));

    const TSharedRef<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
    Details->SetStringField(TEXT("field"), TEXT("asset_path"));
    const FUnrealMCPError Error{TEXT("invalid_argument"), TEXT("Invalid fixture"), Details, false};
    TestEqual(TEXT("complete error fixture remains byte-for-byte stable"),
        CanonicalFixture(ResponseText(UnrealMCP::Protocol::Error(EHttpServerResponseCodes::BadRequest, Error))),
        FString(TEXT("{\"ok\":false,\"error\":{\"code\":\"invalid_argument\",\"message\":\"Invalid fixture\",\"details\":{\"field\":\"asset_path\"},\"retryable\":false}}")));
    return true;
}

#endif
