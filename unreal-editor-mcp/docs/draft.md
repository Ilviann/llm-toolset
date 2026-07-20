# Unreal Engine 5 MCP Plugin Requirements

## Product Goal and Delivery Priorities

The primary workflow is implementing game logic in Unreal Engine Blueprints. The first usable release must support both creating new Actor-derived Blueprint assets from a caller-selected valid base class and safely modifying the code and configuration of existing Actor Blueprints.

Delivery order:

1. Actor-derived Blueprints, including their component hierarchy, component defaults, variables, and graph logic.
2. Gameplay-framework Blueprints, including GameMode, GameState, and GameInstance families.
3. Other specialized Blueprint families after the first two groups are usable.

Initial graph editing must use small, typed, atomic actions. Each action must validate its target and arguments, integrate with Unreal's editor transaction/undo model where supported, and report bounded compile or validation diagnostics. A later workflow should compose these primitives into one transactional replacement of a bounded logic unit, such as one event-handler implementation, function, macro, or selected graph region, without rewriting unrelated Blueprint content.

macOS is the first native validation platform because it is currently available. Windows support is mandatory: platform-specific paths, editor discovery, plugin loading, and process behavior must be isolated so they can be tested and validated on Windows. Linux should remain portable in accordance with the repository-wide platform policy, although native Linux validation is not an initial requirement.

## Architecture Overview
The MCP (Model Context Protocol) plugin for Unreal Engine 5 will consist of two primary components:

### 1. MCP Server
- **Language:** Python3
- **Transport:** STDIO
- **Role:** Serves as the primary interface for LLM agent applications. It handles the communication protocol, request parsing, and tool orchestration.

### 2. Unreal Editor Bridge
- **Language:** C++
- **Framework:** Unreal Engine 5 Framework
- **Role:** An Unreal Editor plugin that acts as the bridge between the MCP Server and the Unreal Editor.
- **Communication:** Exposes a localhost HTTP endpoint bound specifically to `127.0.0.1` for the MCP Server to send commands and receive data.
- **Security Model:** Every bridge request must authenticate with a high-entropy per-project token shared only with the configured MCP Server. Token loading and creation must fail closed: if the token cannot be read, generated, persisted, or re-read, the bridge must not accept commands or advertise itself as ready. Localhost binding remains mandatory but is not treated as authentication because unrelated local processes could otherwise control the open project.
- **Functionality:** Directly interacts with the Unreal Editor API to read and modify project data (assets, blueprints, scene hierarchies, etc.).

## Tool Definitions

### 1. Unreal Editor Application State
*Note: This tool set has **low priority** and must be implemented after all other tool sets.*

These tools allow the LLM to manage the lifecycle and status of the Unreal Editor instance.

- **Check Editor Status:** Verify if Unreal Editor is running, fully loaded, and ready to accept commands.
- **Launch Editor:** Start the Unreal Editor. The path to the editor executable must be provided via an environment variable.
- **Request Restart:** Trigger a request to restart the Unreal Editor (e.g., required after C++ source code changes or hot-reloads).
- **Shutdown Editor:** Gracefully shut down the Unreal Editor instance. (Required prerequisite for C++ build tools).

### 2. C++ Project Management
*Note: This tool set has **low priority** and must be implemented after all other tool sets.*

These tools are intended for use when the Unreal Editor is **not** running. They interact with the Unreal Build Tool (UBT) and other build systems.

- **Regenerate Project Files:** Call the platform-specific tool (e.g., `.sln` for Visual Studio on Windows, `.xcodeproj` for Xcode on MacOS) to regenerate project files. Requires an environment variable for the tool's path and the `.uproject` file path.
- **Rebuild Editor Libraries:** Call the specific tool provided by Unreal Engine to rebuild the editor's own libraries. Requires an environment variable for the tool's path.

*Note: Direct modification of C++ files is handled by an external "File Access" MCP server and is outside the scope of this project.*

### 3. Actor Blueprint Management
These tools allow the LLM to create, read, and modify Blueprint assets and their underlying logic.

- **Create Actor Blueprint:** Create a new Blueprint asset from a caller-selected valid Actor-derived base class. Reject missing, incompatible, abstract, deprecated, or otherwise unsupported parent classes with a stable error and no partial asset.
- **Inspect Blueprint:** Read bounded identity, parent class, compile state, component hierarchy, variables, functions, events, macros, graphs, nodes, pins, and connections from a targeted Blueprint asset. Large graphs and member lists must support filters and pagination rather than unbounded dumps.
- **Component Management:** Add, remove, rename, reparent, and configure Actor Blueprint components through atomic editor actions. Support component defaults required to construct the Actor before graph logic is authored.
- **Variable Management:** Create, modify, and delete member and local variables and manage relevant settings such as type, default value, replication, visibility, and instance editability.
- **Atomic Graph Editing:** Create, update, and remove supported graph nodes; set pin defaults; connect and disconnect compatible pins; and create functions, macros, custom events, and bounded node groups. Every mutation must validate the Blueprint and graph identity, node and pin types, limits, and stale preconditions before it is committed.
- **Compile and Save:** Explicitly compile and save a targeted Blueprint, return bounded compiler diagnostics, and allow the result to be read back for verification. Failed validation must not dirty the asset; compile failure must be reported distinctly from transport or save failure.
- **Structured Representation:** Export a compact structured representation for targeted inspection and planning. It is not initially an unrestricted whole-Blueprint rewrite format.
- **Bounded Logic-Block Replacement (Later):** Replace one event handler, function, macro, or selected graph region as a single prevalidated transaction while preserving unrelated graphs, variables, metadata, and layout. The bridge should automatically lay out nodes created or changed by this operation.

The first end-to-end acceptance workflow is: create or open an Actor Blueprint, inspect it, add and configure components, add variables and event-graph logic through atomic actions, compile it, save it, and read it back to verify the result.
