#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPCompatibility.h"
#include "UnrealMCPTokenStore.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::Tests
{
inline const FHttpPath Phase1RoutePath(TEXT("/unreal-mcp/v1/command"));

inline TSharedPtr<FHttpServerRequest> MakeRequest(const FString& Authorization, int32 BodyBytes = INDEX_NONE)
{
    const TSharedPtr<FHttpServerRequest> Request = MakeShared<FHttpServerRequest>();
    Request->RelativePath = Phase1RoutePath;
    Request->Verb = EHttpServerRequestVerbs::VERB_POST;
    Request->Headers.FindOrAdd(TEXT("authorization")).Add(Authorization);
    if (BodyBytes == INDEX_NONE)
    {
        const FString Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}");
        const FTCHARToUTF8 Encoded(*Json);
        Request->Body.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    }
    else
    {
        Request->Body.SetNumZeroed(BodyBytes);
    }
    return Request;
}

inline TSharedPtr<FHttpServerRequest> MakeJsonRequest(const FString& Authorization, const FString& Json)
{
    const TSharedPtr<FHttpServerRequest> Request = MakeShared<FHttpServerRequest>();
    Request->RelativePath = Phase1RoutePath;
    Request->Verb = EHttpServerRequestVerbs::VERB_POST;
    Request->Headers.FindOrAdd(TEXT("authorization")).Add(Authorization);
    const FTCHARToUTF8 Encoded(*Json);
    Request->Body.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    return Request;
}

inline bool LoadLiveToken(FString& OutToken)
{
    if (!FFileHelper::LoadFileToString(
        OutToken,
        *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("bridge.token"))))
    {
        return false;
    }
    OutToken.TrimStartAndEndInline();
    return FUnrealMCPTokenStore::IsValidToken(OutToken);
}
}

#endif
