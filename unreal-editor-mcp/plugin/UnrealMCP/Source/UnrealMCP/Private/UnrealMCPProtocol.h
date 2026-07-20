#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HttpServerConstants.h"

struct FHttpServerResponse;

struct FUnrealMCPError
{
    FString Code;
    FString Message;
    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    bool bRetryable = false;
};

namespace UnrealMCP::Protocol
{
bool ConstantTimeEquals(const FString& Left, const FString& Right);
bool ParseCommand(const TArray<uint8>& Body, FString& OutCommand, TSharedPtr<FJsonObject>& OutArguments, FUnrealMCPError& OutError);
TUniquePtr<FHttpServerResponse> Success(const TSharedPtr<FJsonObject>& Result);
TUniquePtr<FHttpServerResponse> Error(EHttpServerResponseCodes Status, const FUnrealMCPError& Error);
TUniquePtr<FHttpServerResponse> Error(EHttpServerResponseCodes Status, const FString& Code, const FString& Message, bool bRetryable = false);
}
