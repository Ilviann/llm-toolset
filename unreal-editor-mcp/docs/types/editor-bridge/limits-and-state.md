# Limits and generated state

`UnrealMCPVersion.h` is the executable native limit source: 64 KiB request, 256 KiB response, eight queued commands, JSON depth 16, string length 4096, five-second dispatch deadline, and two-second heartbeat interval. Inspection adds 100 records per page, 2,048 discovery candidates, 4,096 structural fingerprint records, 32 retained cursors, 16 changed defaults per component, and a 30-second cursor lifetime. Python independently bounds its side and validates the native values returned by `capabilities`.

The token file is durable secret state. The discovery file is replaceable non-secret state and contains project hash, process ID, port, exact bridge version, Unreal version, and UTC epoch milliseconds only. Both use temporary-file-plus-rename writes. A new startup deletes any stale discovery file before attempting the credential/listener gate.

Pending requests transition from accepted to queued, then execute only on the Game thread. Shutdown rejects new work and retained work returns `cancelled`. Released operations never create transactions, compile, save, or mutate objects; inspection additionally verifies that package dirty and Blueprint compile status are unchanged.
