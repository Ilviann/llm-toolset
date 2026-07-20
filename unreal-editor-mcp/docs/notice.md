# Technical Notes and Critiques

These notes are gathered during the requirements gathering phase for the Unreal Engine 5 MCP Plugin.

## Confirmed Product Scope
- **Primary Workflow:** Implement game logic in Blueprints rather than editing C++ source through this MCP server.
- **First Blueprint Family:** Create and modify Actor-derived Blueprints, including component trees, component defaults, variables, and graph logic.
- **Second Blueprint Family:** Add gameplay-framework support for GameMode, GameState, and GameInstance families.
- **Mutation Model:** Start with typed atomic actions. Later compose them into prevalidated replacement of one bounded event handler, function, macro, or graph region.
- **Validation Platforms:** Validate first on macOS. Windows support is mandatory and must not be deferred as an architectural concern.

## Architecture & Communication
- **Transport Strategy:** The MCP Server uses **STDIO** (standard for local LLM agents like Claude Desktop), while the Bridge uses **HTTP**. The MCP Server acts as a gateway, translating JSON-RPC calls into HTTP requests.
- **Concurrency:** Unreal Engine's editor operations are primarily on the main thread. The C++ Bridge must handle incoming HTTP requests asynchronously and safely dispatch commands to the Game/Editor thread.

## Security
- **Localhost Binding:** The bridge is bound strictly to `127.0.0.1`.
- **Per-Project Authentication:** The bridge and configured MCP Server share a high-entropy token scoped to one Unreal project. Every HTTP request must authenticate before it can be dispatched to the Editor thread.
- **Fail-Closed Startup:** If the token cannot be read, generated, durably persisted, or re-read, the bridge must not accept commands or report itself ready. Discovery and heartbeat data must never contain the token.
- **Local Process Threat:** Loopback binding limits remote exposure but does not authenticate software running on the same machine. The bridge therefore does not rely on user-managed OS isolation as its primary access control.

## Tool-Specific Notes
- **"Fully Loaded" Check:** Checking if a process exists is insufficient. It is recommended to implement a "Heartbeat" or a "Ready" flag in the C++ Bridge that only flips to `true` once the editor's lifecycle events have completed and the HTTP server is ready.
- **Launch Logic:** The path to the editor executable should be provided via an environment variable. The MCP server should handle missing variables gracefully with clear error messages for the LLM.
- **Restart Request:** Since a restart will likely break the connection, we need to clarify if the MCP server should simply signal the restart and exit, or if a "Restart Successful" confirmation is required.
- **Dependency Management (Build Tools):** There is a distinction between "Build" tasks and "Editor" tasks. 
    - **MCP Server** should likely handle external tools (Regenerate Project Files, Rebuild Libraries) using `subprocess`.
    - **C++ Bridge** should focus on tasks requiring direct access to the live Editor API.
- **Path Resolution:** A consistent naming convention for environment variables (e.g., `UE5_PATH`, `UE_BUILD_TOOL_PATH`) should be established for use by both Python and C++.
- **Shutdown Logic:** Shutdown should be handled by the Editor (e.g., `FEditorFileUtils::Exit()`) via the bridge, rather than the OS killing the process, to prevent data corruption.
- **Blueprint Transactions:** Graph and component mutations should participate in Unreal's transaction/undo system where the public editor API supports it. Validation must occur before committing a mutation so a rejected request does not leave a partially edited Blueprint.
- **Blueprint Identity and Staleness:** Mutation requests should carry stable asset and graph targets plus revision or structural preconditions so a request based on an older inspection cannot silently modify replacement nodes or pins.
- **Compile Verification:** Compilation, saving, and read-back verification are separate observable steps. Blueprint compiler diagnostics must be returned in bounded structured results rather than inferred from a save succeeding.
- **Future Block Replacement:** Whole-block operations should preserve content outside the selected event handler, function, macro, or graph region and should use the same atomic primitives rather than introduce an unrestricted text-to-Blueprint evaluator.
