#include "Modules/ModuleManager.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPBridge.h"
#include "UnrealMCPCompatibility.h"
#include "UnrealMCPTokenStore.h"
#include "UnrealMCPVersion.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCP, Log, All);

namespace
{
FString ProjectHash()
{
    FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FPaths::NormalizeFilename(ProjectPath);
#if PLATFORM_WINDOWS
    ProjectPath.ToLowerInline();
#endif
    const FTCHARToUTF8 Utf8(*ProjectPath);
    uint8 Hash[FSHA1::DigestSize];
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
    return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
}

uint32 ConfiguredPort()
{
    int32 Value = static_cast<int32>(UnrealMCP::DefaultPort);
    GConfig->GetInt(TEXT("UnrealMCP"), TEXT("Port"), Value, GEditorPerProjectIni);
    return Value >= 1024 && Value <= 65535 ? static_cast<uint32>(Value) : 0U;
}

void ConfigureLoopbackListener(uint32 Port)
{
    TArray<FString> Overrides;
    GConfig->GetArray(TEXT("HTTPServer.Listeners"), TEXT("ListenerOverrides"), Overrides, GEngineIni);
    const FString PortNeedle = FString::Printf(TEXT("Port=%u"), Port);
    Overrides.RemoveAll([&PortNeedle](const FString& Value) { return Value.Contains(PortNeedle, ESearchCase::IgnoreCase); });
    Overrides.Add(FString::Printf(
        TEXT("(Port=%u,BindAddress=127.0.0.1,BufferSize=262144,ConnectionsBacklogSize=8,MaxConnectionsAcceptPerFrame=2,ReuseAddressAndPort=false)"),
        Port));
    GConfig->SetArray(TEXT("HTTPServer.Listeners"), TEXT("ListenerOverrides"), Overrides, GEngineIni);
}
}

class FUnrealMCPModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (!UnrealMCP::Compatibility::SupportsCurrentEngine())
        {
            UE_LOG(LogUnrealMCP, Error, TEXT("Unreal MCP does not support this engine API line"));
            return;
        }
        const uint32 Port = ConfiguredPort();
        if (Port == 0)
        {
            UE_LOG(LogUnrealMCP, Error, TEXT("Unreal MCP disabled: Port must be between 1024 and 65535"));
            return;
        }
        const FString StateDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"));
        // A previous editor may have been terminated by the OS without module
        // shutdown. Never advertise that stale process during a new startup.
        IFileManager::Get().Delete(*FPaths::Combine(StateDirectory, TEXT("discovery.json")), false, true, true);
        FString Token;
        FString Error;
        if (!FUnrealMCPTokenStore::LoadOrCreate(StateDirectory, Token, Error))
        {
            UE_LOG(LogUnrealMCP, Error, TEXT("Unreal MCP disabled: %s"), *Error);
            return;
        }
        ConfigureLoopbackListener(Port);
        Bridge = MakeShared<FUnrealMCPBridge>(MoveTemp(Token), StateDirectory, ProjectHash(), Port);
        if (!Bridge->Start(Error))
        {
            UE_LOG(LogUnrealMCP, Error, TEXT("Unreal MCP disabled: %s"), *Error);
            Bridge.Reset();
            return;
        }
        UE_LOG(LogUnrealMCP, Display, TEXT("Unreal MCP %s ready on 127.0.0.1:%u (%s APIs)"), UnrealMCP::Version, Port, *UnrealMCP::Compatibility::EngineApiLine());
    }

    virtual void ShutdownModule() override
    {
        if (Bridge)
        {
            Bridge->Stop();
            Bridge.Reset();
        }
    }

private:
    TSharedPtr<FUnrealMCPBridge> Bridge;
};

IMPLEMENT_MODULE(FUnrealMCPModule, UnrealMCP)
