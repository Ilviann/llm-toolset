#include "UnrealMCPProtocol.h"

#include "HttpServerResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPVersion.h"

namespace
{
FString Bounded(const FString& Value, int32 Maximum)
{
    FString Result = Value.Left(Maximum);
    Result.ReplaceInline(TEXT("\0"), TEXT(""));
    return Result;
}

TUniquePtr<FHttpServerResponse> SerializeEnvelope(const TSharedRef<FJsonObject>& Envelope, EHttpServerResponseCodes Status)
{
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    if (!FJsonSerializer::Serialize(Envelope, Writer) || Text.Len() > UnrealMCP::MaxResponseBytes)
    {
        Text = TEXT("{\"ok\":false,\"error\":{\"code\":\"response_too_large\",\"message\":\"Response exceeded the configured limit\",\"details\":{},\"retryable\":false}}");
        Status = EHttpServerResponseCodes::ServerError;
    }
    TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Text, TEXT("application/json; charset=utf-8"));
    Response->Code = Status;
    Response->Headers.Add(TEXT("Cache-Control"), {TEXT("no-store")});
    return Response;
}

bool HasBoundedJsonShape(const TArray<uint8>& Body)
{
    int32 Depth = 0;
    int32 StringBytes = 0;
    bool bInString = false;
    bool bEscaped = false;
    for (const uint8 Byte : Body)
    {
        if (bInString)
        {
            if (bEscaped)
            {
                bEscaped = false;
                continue;
            }
            if (Byte == '\\')
            {
                bEscaped = true;
                continue;
            }
            if (Byte == '"')
            {
                bInString = false;
                StringBytes = 0;
                continue;
            }
            if (++StringBytes > UnrealMCP::MaxStringLength)
            {
                return false;
            }
            continue;
        }
        if (Byte == '"')
        {
            bInString = true;
        }
        else if (Byte == '{' || Byte == '[')
        {
            if (++Depth > UnrealMCP::MaxJsonDepth)
            {
                return false;
            }
        }
        else if (Byte == '}' || Byte == ']')
        {
            if (--Depth < 0)
            {
                return false;
            }
        }
    }
    return !bInString && Depth == 0;
}
}

bool UnrealMCP::Protocol::ConstantTimeEquals(const FString& Left, const FString& Right)
{
    const int32 Maximum = FMath::Max(Left.Len(), Right.Len());
    uint32 Difference = static_cast<uint32>(Left.Len() ^ Right.Len());
    for (int32 Index = 0; Index < Maximum; ++Index)
    {
        const uint32 A = Index < Left.Len() ? static_cast<uint32>(Left[Index]) : 0U;
        const uint32 B = Index < Right.Len() ? static_cast<uint32>(Right[Index]) : 0U;
        Difference |= A ^ B;
    }
    return Difference == 0U;
}

bool UnrealMCP::Protocol::ParseCommand(const TArray<uint8>& Body, FString& OutCommand, TSharedPtr<FJsonObject>& OutArguments, FUnrealMCPError& OutError)
{
    if (Body.Num() <= 0 || Body.Num() > UnrealMCP::MaxRequestBytes)
    {
        OutError = {TEXT("request_too_large"), TEXT("Request body is empty or exceeds the configured limit")};
        return false;
    }
    if (!HasBoundedJsonShape(Body))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Request exceeds JSON depth or string limits")};
        return false;
    }
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Body.Num());
    const FString Text(Converted.Length(), Converted.Get());
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("Request body must be a JSON object")};
        return false;
    }
    const TSharedPtr<FJsonObject>* ArgumentsPointer = nullptr;
    if (Root->Values.Num() != 2 || !Root->TryGetStringField(TEXT("command"), OutCommand)
        || OutCommand.Len() < 1 || OutCommand.Len() > 64
        || !Root->TryGetObjectField(TEXT("arguments"), ArgumentsPointer)
        || ArgumentsPointer == nullptr || !ArgumentsPointer->IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("Request must contain only command and arguments")};
        return false;
    }
    OutArguments = *ArgumentsPointer;
    if ((OutCommand == TEXT("capabilities") || OutCommand == TEXT("editor_state")) && !OutArguments->Values.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("This command does not accept arguments")};
        return false;
    }
    return true;
}

TUniquePtr<FHttpServerResponse> UnrealMCP::Protocol::Success(const TSharedPtr<FJsonObject>& Result)
{
    const TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetBoolField(TEXT("ok"), true);
    Envelope->SetObjectField(TEXT("result"), Result);
    return SerializeEnvelope(Envelope, EHttpServerResponseCodes::Ok);
}

TUniquePtr<FHttpServerResponse> UnrealMCP::Protocol::Error(EHttpServerResponseCodes Status, const FUnrealMCPError& ErrorValue)
{
    const TSharedRef<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
    ErrorObject->SetStringField(TEXT("code"), Bounded(ErrorValue.Code, 64));
    ErrorObject->SetStringField(TEXT("message"), Bounded(ErrorValue.Message, 512));
    ErrorObject->SetObjectField(TEXT("details"), ErrorValue.Details.IsValid() ? ErrorValue.Details : MakeShared<FJsonObject>());
    ErrorObject->SetBoolField(TEXT("retryable"), ErrorValue.bRetryable);
    const TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetBoolField(TEXT("ok"), false);
    Envelope->SetObjectField(TEXT("error"), ErrorObject);
    return SerializeEnvelope(Envelope, Status);
}

TUniquePtr<FHttpServerResponse> UnrealMCP::Protocol::Error(EHttpServerResponseCodes Status, const FString& Code, const FString& Message, bool bRetryable)
{
    return Error(Status, FUnrealMCPError{Code, Message, MakeShared<FJsonObject>(), bRetryable});
}
