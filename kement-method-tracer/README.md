# kement-method-tracer

Trace any method and its argument from outside a running app, with no code change.
The Core injects a call to the managed probe at each hooked method.

Run: `run.ps1` (traces `Service.Login` / `Service.Charge` and their args into a log).
Hooks: `hooks.txt` (`trace` action). Needs the probe via `DOTNET_STARTUP_HOOKS`.

Note: `trace` logs arg0, so it assumes a static method with a reference-type first
argument. Instance methods or later args use the `tracearg` action.
