# kement-chaos

Inject failures into a healthy method to test how an app copes, no code change. The
Core prepends a call to the probe's `Fail`, which throws before the real body runs.

Run: `run.ps1` (forces `Worker.FetchData()` to fail every call; app reports 0 ok,
10 failed). Hooks: `hooks.txt` (`throw`). Needs the probe via `DOTNET_STARTUP_HOOKS`.
