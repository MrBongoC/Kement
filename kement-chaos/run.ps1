# Kement tool: fault injection. Force a healthy method to fail, to test resilience.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "ChaosDemo\ChaosDemo.csproj"
$dir   = Join-Path $here "ChaosDemo\bin"
$app   = Join-Path $dir "ChaosDemo.dll"

Write-Host "=== building ChaosDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-chaos-core.log"
$env:KEMENT_PROBE_LOG         = Join-Path $env:TEMP "kement-chaos.log"
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_MODE","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (FetchData should fail every call) ==="
$out = & dotnet $app
$out | ForEach-Object { Write-Host $_ }

Write-Host "`n=== verification ==="
$summary = $out | Where-Object { $_ -match "summary:" }
$failed = $out | Where-Object { $_ -match "FAILED" }
Write-Host "  $summary"
if ($failed.Count -ge 1 -and ($summary -match "ok=0")) { Write-Host "RESULT: PASS (failure injected into a healthy method)"; exit 0 }
else { Write-Host "RESULT: FAIL"; exit 2 }
