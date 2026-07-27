# Editor lifecycle contracts

`editor_lifecycle` accepts exactly `operation_id` (32 lowercase hexadecimal characters) and `operation` (`launch`, `shutdown`, `restart`, or `cancel`). `cancel` addresses the active or retained record with the same ID; it does not identify a process.

Launch uses the startup-configured executable and resolved project descriptor as a fixed two-element argument array with `shell` disabled. A terminal result reports the operation/project/version, state, bounded timestamps, process ID, and old/new bridge-instance IDs. States include `accepted`, `starting`, `shutdown_preflight`, `shutting_down`, `launching`, `ready`, `already_running`, `stopped`, `already_stopped`, `cancelled`, `timed_out`, `rejected`, `failed`, and `outcome_unknown`.

The durable record is `{version:1,records:[...]}` at `Saved/UnrealMCP/lifecycle.json`. It is at most 32 KiB, contains at most 16 exact records, retains them for 24 hours, rejects symbolic-link targets, and uses same-directory atomic replacement. It is separate from the bridge process's mutation ledger.
