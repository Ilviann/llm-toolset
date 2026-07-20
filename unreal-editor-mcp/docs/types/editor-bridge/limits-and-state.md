# Limits and generated state

`UnrealMCPVersion.h` is the executable native limit source: 64 KiB request, 256 KiB response, eight queued commands, JSON depth 16, string length 4096, five-second dispatch deadline, and two-second heartbeat interval. Python independently bounds its side and validates the native values returned by `capabilities`.

The token file is durable secret state. The discovery file is replaceable non-secret state and contains project hash, process ID, port, exact bridge version, Unreal version, and UTC epoch milliseconds only. Both use temporary-file-plus-rename writes. A new startup deletes any stale discovery file before attempting the credential/listener gate.

Pending requests transition from accepted to queued, then execute only on the Game thread. Shutdown rejects new work and retained work returns `cancelled`; Phase 1 operations never create transactions, dirty packages, compile, save, or mutate objects.
