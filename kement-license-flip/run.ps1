# Kement tool: live license flip. Runs a trial app, and the Core rewrites its
# IsPremium() check to return true while it runs. No probe assembly needed.
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$core = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$prj  = Join-Path $here "PremiumApp\PremiumApp.csproj"
$dir  = Join-Path $here "PremiumApp\bin"
$app  = Join-Path $dir "PremiumApp.dll"

Write-Host "=== building PremiumApp ==="
& dotnet build $prj -c Release -o $dir | Out-Null

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-license-flip.log"
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","DOTNET_STARTUP_HOOKS")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (watch tier flip trial -> PREMIUM) ==="
$out = & dotnet $app
$out | ForEach-Object { Write-Host $_ }

Write-Host "`n=== verification ==="
$trial = ($out | Where-Object { $_ -match "tier = trial" }).Count
$prem  = ($out | Where-Object { $_ -match "tier = PREMIUM" }).Count
Write-Host "  trial lines: $trial ; PREMIUM lines: $prem"
if ($prem -ge 1) { Write-Host "RESULT: PASS (license check flipped live)"; exit 0 }
else { Write-Host "RESULT: FAIL"; exit 2 }
