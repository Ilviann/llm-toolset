# Native wire contracts

The HTTP request has exactly two fields: `command` is `capabilities`, `editor_state`, or `blueprint_inspect`; `arguments` is empty for the first two and follows one exact inspector query shape for the third. Authentication is one `Authorization: Bearer <64 lowercase hex>` header. Requests with duplicate/missing authorization, an unknown command, fields, or invalid arguments are rejected before Blueprint access.

A success is `{ok:true,result:<object>}`. A failure is `{ok:false,error:{code,message,details,retryable}}`. Native errors use the same stable codes accepted by Python and never contain exceptions, addresses, tokens, absolute project paths, or unbounded logs.

`capabilities` is authoritative for `bridge_version`, `unreal_version`, `platform`, `mode`, `bridge_ready`, exact `commands`, optional `features`, effective `limits`, listener properties, and `asset_access`. `asset_access.read_scope` is `all_mounted_content`; `asset_access.mutation_scope` is `project_content_and_local_project_plugins`. `editor_state` contains project hash/name, readiness, ready/shutdown state, play/simulate/save/GC flags, queued count, and one concise operation state.
