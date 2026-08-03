#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UnrealMCPCompanionApi.h"

class UNREALMCP_API IUnrealMCPModule : public IModuleInterface
{
public:
    static IUnrealMCPModule& Get()
    {
        return FModuleManager::GetModuleChecked<IUnrealMCPModule>(TEXT("UnrealMCP"));
    }

    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded(TEXT("UnrealMCP"));
    }

    virtual int32 GetCompanionApiVersion() const = 0;
    virtual FUnrealMCPRegistrationResult RegisterCompanion(
        const FUnrealMCPCompanionRegistration& Registration,
        IModuleInterface& OwningModule) = 0;
    virtual void UnregisterCompanion(
        FUnrealMCPRegistrationHandle Handle,
        IModuleInterface& OwningModule) = 0;
};
