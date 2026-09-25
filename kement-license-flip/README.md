# kement-license-flip

Flip a running app's license check to "premium" without patching any file. The Core
rewrites `License.IsPremium()` to return true via ReJIT.

Run: `run.ps1` (builds PremiumApp, runs it under the Core, verifies the tier flips
trial -> PREMIUM). Hook config: `hooks.txt`. No probe assembly needed.

Only flip software you own. The demo builds and flips its own toy app.
