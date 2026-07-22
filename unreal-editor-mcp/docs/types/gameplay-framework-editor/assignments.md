# Gameplay-framework assignment contracts

`gameplay_framework_edit` accepts exactly `operation_id`, the active `project_hash`, `setting`, `class_path`, and `expected_class`. `setting` is `default_game_mode` or `default_game_instance`; `expected_class` may be empty only when the live setting is empty. The command participates in the normal operation ledger and lost-response reconciliation.

GameMode accepts exact `AGameModeBase` descendants. GameInstance accepts exact `UGameInstance` descendants. Native classes must be usable; Blueprint-generated classes must be compiled, saved, clean, package-backed, and neither skeleton nor reinstanced. Unsaved, missing, abstract, deprecated, editor-only, wrong-family, stale, or already-selected classes reject without persistence.

Only `GlobalDefaultGameMode` or `GameInstanceClass` in the project's `DefaultEngine.ini` GameMapsSettings section can change. The caller cannot supply a config path, section, key, world override, or arbitrary setting. A success reports project identity, setting, old/new classes, `verified`, `restart_required: false`, and `active_sessions_unaffected: true`. Failed replacement/read-back restores the previous on-disk content; read-only or source-controlled files return `write_conflict`.
