#pragma once

#include "CoreMinimal.h"
#include "HttpServerConstants.h"
#include "UnrealMCPWireTypes.h"

struct FHttpServerResponse;

namespace UnrealMCP::Protocol
{
bool ConstantTimeEquals(const FString& Left, const FString& Right);
bool ParseCommand(const TArray<uint8>& Body, FUnrealMCPCommandRequest& OutRequest, FUnrealMCPError& OutError);
TUniquePtr<FHttpServerResponse> Success(const TSharedPtr<FUnrealMCPRecord>& Result);
TUniquePtr<FHttpServerResponse> Error(EHttpServerResponseCodes Status, const FUnrealMCPError& Error);
TUniquePtr<FHttpServerResponse> Error(EHttpServerResponseCodes Status, const FString& Code, const FString& Message, bool bRetryable = false);
}
