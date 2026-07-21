#include "UnrealMCPCompatibility.h"

#include "Misc/EngineVersionComparison.h"

#if PLATFORM_UNIX || PLATFORM_MAC
#include <sys/stat.h>
#endif

#if UE_VERSION_OLDER_THAN(5, 8, 0)
#error Unreal Editor MCP requires Unreal Engine 5.8 or newer.
#endif

bool UnrealMCP::Compatibility::SupportsCurrentEngine()
{
    return true;
}

FString UnrealMCP::Compatibility::EngineApiLine()
{
#if UE_VERSION_NEWER_THAN(5, 8, 99)
    return TEXT("5.9+");
#else
    return TEXT("5.8");
#endif
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
