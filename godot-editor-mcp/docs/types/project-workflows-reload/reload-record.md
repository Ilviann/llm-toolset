# Types: reload record and status

**Sources:** `reload_commands.gd`, `state_payloads.py`, `waiting.py`

Before restart, the plugin atomically persists a bounded pending record containing record version, normalized project identity, exact bridge version, operation ID, process/time freshness data, and pending state. It never persists credentials.

Startup accepts only a fresh matching record, restores completion into the new process's operation registry, and exposes a status carrying the same project/version/operation identities. Python reconnect succeeds only when all identities match the accepted request. Malformed, stale, cross-project, cross-version, or inconsistent states have distinct errors and cannot become ambiguous success.
