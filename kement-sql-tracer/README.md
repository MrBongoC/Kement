# kement-sql-tracer

See every SQL statement a running app executes, with no query logging configured.
The Core hooks the command executor and logs `this.CommandText`.

Run: `run.ps1` (a SQLite app runs CREATE/INSERT/SELECT; every statement is logged).
Hooks: `hooks.txt` (`tracethis CommandText`). Needs the probe via `DOTNET_STARTUP_HOOKS`.

HTTP works the same way with `tracearg` on `HttpRequestMessage.RequestUri` (example
line in `hooks.txt`, not run in this demo since it needs a live HTTP call).
Duplicate lines are real: `ExecuteNonQuery` internally calls `ExecuteReader`.
