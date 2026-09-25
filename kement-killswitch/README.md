# kement-killswitch

Turn a feature flag off on a running process, no config change, no redeploy. The
Core rewrites `Feature.IsEnabled()` to return false via ReJIT.

Run: `run.ps1` (verifies the feature reads OFF for the whole run). Hook: `hooks.txt`.
No probe assembly needed.
