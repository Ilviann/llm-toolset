# Platform adapter

`PlatformAdapter` owns the only Python platform branches. Windows path identities use slash normalization and case folding; macOS and Linux preserve case. Process liveness uses `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` on Windows and signal zero on Unix. Tests inject the process probe and exercise all three system names without depending on host process state.
