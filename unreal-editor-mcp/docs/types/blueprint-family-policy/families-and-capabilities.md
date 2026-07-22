# Families and capabilities

`capabilities.blueprint_families` contains the six ordered family records. Each adds `multiplayer` with exact Actor/component/variable replication Booleans and supported RPC modes. Parent change is always false; project assignment is true only for the two GameMode families and GameInstance. GameInstance alone reports components false.

Classification uses the live native or generated class. Descendants of `AGameMode` remain `game_mode`; other descendants of `AGameModeBase` are `game_mode_base`. The equivalent nearest-family rule applies to `AGameState` and `AGameStateBase`. Other `AActor` descendants are `actor`. `UGameInstance` descendants are `game_instance`; unrelated UObject classes remain unsupported.

Discovery asset records add `blueprint_family` and `native_family_class`. Exact inspection pages, mutation results, action catalogs, and graph-edit results add `blueprint_family`. Exact inspection and mutation results also add `family_capabilities`, with live Booleans for `class_defaults`, `components`, `event_graphs`, `local_variables`, and `overrides`, plus `graph_types.event`, `graph_types.function`, and `graph_types.macro`.

All four GameMode/GameState families reuse the Actor-derived contracts. Common GameMode defaults include `GameStateClass`, `PlayerControllerClass`, `DefaultPawnClass`, and `bUseSeamlessTravel`; `AGameMode` additionally exposes defaults such as `bDelayedStart` and `MinRespawnDelay`. GameState families expose safe editable Actor defaults and `ServerWorldTimeSecondsUpdateFrequency`. Exact property availability and codec support remain live-reflection decisions.

GameInstance reuses class-default, member, callable, action, graph, compile, save, diagnostics, ledger, and security contracts without an SCS path. User variables such as session preferences become reflected generated-class defaults after compile and can then be targeted through `blueprint_default_edit`. Representative Blueprint events are `ReceiveInit`, `ReceiveShutdown`, `HandleNetworkError`, and `HandleTravelError`; the action catalog remains authoritative for exact callback availability and uniqueness.

The live action catalog remains authoritative for callbacks and inherited functions. Representative GameMode events include `K2_PostLogin`, `HandleStartingNewPlayer`, and, for `AGameMode`, `K2_OnSetMatchState`/match-state overrides. GameState action coverage includes inherited Actor events and callable state/time functions such as `GetServerWorldTimeSeconds`, `HasBegunPlay`, `HasMatchStarted`, and `HasMatchEnded`. Native non-Blueprint callbacks remain visible only through their exposed actions or inherited behavior; the bridge does not synthesize override graphs.

Actor-family component ownership is unchanged. The bridge can assign only the active project's default GameMode or GameInstance through the separate exact settings command; it does not reparent Blueprints or change world overrides.
