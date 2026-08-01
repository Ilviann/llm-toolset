# Plugin lifecycle/bridge contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: bridge client record

**Source:** `plugin/addons/godot_mcp/bridge_server.gd`

Internal dictionary retained for each accepted TCP peer. It contains the peer, receive buffer, acceptance/deadline time, and state needed to parse exactly one authenticated newline-delimited request. A validated runtime request may additionally remain associated with a deferred request identity until the debugger gateway resolves it.

The collection is capped at 16. Incomplete/unauthenticated requests expire after two seconds, excess clients are rejected, buffers are bounded, and shutdown disconnects every record. Fields are private implementation data and must not cross the bridge.

## Library: command handler ownership map

**Source:** `plugin/addons/godot_mcp/command_router.gd`

Services publish `Dictionary` maps from stable command strings to direct `Callable` handlers. `register_handlers(owner, handlers)` validates the entire proposed map first and rejects invalid callables, duplicate names within the map, or names already owned by another service. Only then is registration committed. `dispatch` returns a stable unknown-command envelope when absent.

The composition root retains each service instance, registers its map explicitly, and compares the final sorted command set with Python expectations through capabilities/tests.

## Type: token result

**Sources:** `token_store.gd`, `authenticated_startup.gd`

Internal success/failure dictionary returned by credential loading/creation. Success contains a validated bounded token that was read from durable storage or generated, written, and flushed. Failure contains a stable bounded error envelope and no usable credential.

`authenticated_startup.gd` invokes service/listener/discovery composition only for success. Never allow an in-memory generated token to start the bridge after persistence failure: the Python process could not authenticate to it.

## Library: discovery heartbeat publisher

**Source:** `plugin/addons/godot_mcp/discovery_record.gd`

Publishes the mirrored `DiscoveryRecord` under `.godot` through the atomic JSON record library. `start` captures port/version/process/project identity, `poll` refreshes the heartbeat on its bounded cadence, and `stop` removes the file only when the current record still belongs to the same process.

It must start after authentication and listener ownership succeed. Never publish the token or absolute project path.
