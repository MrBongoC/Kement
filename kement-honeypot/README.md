# kement-honeypot

Point the tripwire (monitor mode) at an app and get the full attack trail: every
sensitive operation tagged with the calling method and line.

Run: `run.ps1` (a simulated exploited handler drops a webshell, reads it, deletes
it; every step is attributed to `RequestHandler.Handle`). Uses `KEMENT_TRIPWIRE=1`
monitor mode and the probe via `DOTNET_STARTUP_HOOKS`.
