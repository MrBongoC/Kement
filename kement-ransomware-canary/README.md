# kement-ransomware-canary

Alert when destructive file ops spike, the file-churn signature of ransomware. The
Core hooks `File.WriteAllText` / `File.Delete` to a counting probe.

Run: `run.ps1` (a demo churns 60 write+delete pairs fast; the canary trips). Hooks:
`hooks.txt` (`canary`). Rate via `KEMENT_CANARY_RATE` (default 10/s). Needs the
probe via `DOTNET_STARTUP_HOOKS`.
