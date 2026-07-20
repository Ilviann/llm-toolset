# Native wire contracts

The HTTP request has exactly two fields: `command` is `capabilities` or `editor_state`, and `arguments` is an empty object. Authentication is one `Authorization: Bearer <64 lowercase hex>` header. Requests with duplicate/missing authorization, an unknown command, fields, or arguments are rejected before editor access.

A success is `{ok:true,result:<object>}`. A failure is `{ok:false,error:{code,message,details,retryable}}`. Native errors use the same stable codes accepted by Python and never contain exceptions, addresses, tokens, absolute project paths, or unbounded logs.

`capabilities` is authoritative for `bridge_version`, `unreal_version`, `platform`, `mode`, `bridge_ready`, exact `commands`, optional `features`, effective `limits`, and listener properties. `editor_state` contains project hash/name, readiness, ready/shutdown state, play/simulate/save/GC flags, queued count, and one concise operation state.
