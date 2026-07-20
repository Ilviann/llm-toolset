#include "UnrealMCPTokenStore.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UnrealMCPCompatibility.h"

namespace
{
constexpr int32 TokenLength = 64;
const TCHAR* TokenFilename = TEXT("bridge.token");
}

bool FUnrealMCPTokenStore::LoadOrCreate(const FString& StateDirectory, FString& OutToken, FString& OutError)
{
    OutToken.Reset();
    OutError.Reset();
    IFileManager& Files = IFileManager::Get();
    if (!Files.DirectoryExists(*StateDirectory) && !Files.MakeDirectory(*StateDirectory, true))
    {
        OutError = TEXT("could not create generated-state directory");
        return false;
    }

    const FString TokenPath = FPaths::Combine(StateDirectory, TokenFilename);
    if (Files.FileExists(*TokenPath))
    {
        if (!FFileHelper::LoadFileToString(OutToken, *TokenPath))
        {
            OutError = TEXT("could not read bridge token");
            return false;
        }
        OutToken.TrimStartAndEndInline();
        if (!IsValidToken(OutToken))
        {
            OutError = TEXT("persisted bridge token is invalid");
            OutToken.Reset();
            return false;
        }
        if (!UnrealMCP::Compatibility::SecureTokenFile(TokenPath))
        {
            OutError = TEXT("could not restrict bridge token permissions");
            OutToken.Reset();
            return false;
        }
        return true;
    }

    const FString Generated = GenerateToken();
    if (!IsValidToken(Generated))
    {
        OutError = TEXT("secure token generation failed");
        return false;
    }
    const FString TemporaryPath = FString::Printf(TEXT("%s.tmp-%s"), *TokenPath, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (!FFileHelper::SaveStringToFile(Generated + LINE_TERMINATOR, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = TEXT("could not persist bridge token");
        return false;
    }
    if (!Files.Move(*TokenPath, *TemporaryPath, false, true, false, true))
    {
        Files.Delete(*TemporaryPath, false, true, true);
        OutError = TEXT("could not atomically install bridge token");
        return false;
    }
    if (!UnrealMCP::Compatibility::SecureTokenFile(TokenPath))
    {
        Files.Delete(*TokenPath, false, true, true);
        OutError = TEXT("could not restrict bridge token permissions");
        return false;
    }
    FString Verified;
    if (!FFileHelper::LoadFileToString(Verified, *TokenPath))
    {
        Files.Delete(*TokenPath, false, true, true);
        OutError = TEXT("could not re-read bridge token");
        return false;
    }
    Verified.TrimStartAndEndInline();
    if (Verified != Generated || !IsValidToken(Verified))
    {
        Files.Delete(*TokenPath, false, true, true);
        OutError = TEXT("bridge token verification failed");
        return false;
    }
    OutToken = MoveTemp(Verified);
    return true;
}

bool FUnrealMCPTokenStore::IsValidToken(const FString& Token)
{
    if (Token.Len() != TokenLength)
    {
        return false;
    }
    for (const TCHAR Character : Token)
    {
        if (!((Character >= TEXT('0') && Character <= TEXT('9')) || (Character >= TEXT('a') && Character <= TEXT('f'))))
        {
            return false;
        }
    }
    return true;
}

FString FUnrealMCPTokenStore::GenerateToken()
{
    // Each platform implements FGuid creation with operating-system randomness. Two
    // independent GUIDs provide a 256-bit bearer token without another dependency.
    return FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower()
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
}
