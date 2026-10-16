# kement-live-crack

Crack a **running** app live. A locked vault service runs unmonitored and prints
`LOCKED`; Kement attaches to the live process and flips `Vault.Unlock()` to return
true, and it starts printing `UNLOCKED` mid-run, no restart, no file patched.

Run: `run.ps1` (starts the vault, attaches after 4s, shows LOCKED before /
UNLOCKED after). Hook: `hooks.txt` (`returnbool`). No probe needed, so it works
over live attach (pure IL rewrite via ReJIT-on-attach).

This is the attach + hook combo: `kement attach --pid <id> --hooks <file>`.
Only crack software you own; the demo builds and cracks its own toy service.
