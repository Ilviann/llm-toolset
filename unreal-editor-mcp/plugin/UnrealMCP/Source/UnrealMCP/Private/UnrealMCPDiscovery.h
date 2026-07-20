#pragma once

#include "CoreMinimal.h"

class FUnrealMCPDiscovery
{
public:
    FUnrealMCPDiscovery(FString InStateDirectory, FString InProjectHash, uint32 InPort);
    bool Write(FString& OutError) const;
    void Remove() const;
    FString Path() const;

private:
    FString StateDirectory;
    FString ProjectHash;
    uint32 Port;
};
