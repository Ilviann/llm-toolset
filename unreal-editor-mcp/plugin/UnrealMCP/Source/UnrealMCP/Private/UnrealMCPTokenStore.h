#pragma once

#include "CoreMinimal.h"

class FUnrealMCPTokenStore
{
public:
    static bool LoadOrCreate(const FString& StateDirectory, FString& OutToken, FString& OutError);
    static bool IsValidToken(const FString& Token);

private:
    static FString GenerateToken();
};
