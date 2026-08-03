#include "UnrealMCPCompatibility.h"

#include "Misc/EngineVersionComparison.h"

#if PLATFORM_UNIX || PLATFORM_MAC
#include <sys/stat.h>
#endif

#if ENGINE_MAJOR_VERSION != 5 || ENGINE_MINOR_VERSION != 7
#error Unreal Editor MCP requires Unreal Engine 5.7.x.
#endif

bool UnrealMCP::Compatibility::SupportsCurrentEngine()
{
    return true;
}

FString UnrealMCP::Compatibility::EngineApiLine()
{
    return TEXT("5.7");
}

bool UnrealMCP::Compatibility::SecureTokenFile(const FString& Path)
{
#if PLATFORM_UNIX || PLATFORM_MAC
    const FTCHARToUTF8 NativePath(*Path);
    return chmod(NativePath.Get(), S_IRUSR | S_IWUSR) == 0;
#elif PLATFORM_WINDOWS
    // Windows project state inherits the owning user's ACL. Native Windows
    // validation verifies that no broader ACL is introduced.
    return true;
#else
    return false;
#endif
}
