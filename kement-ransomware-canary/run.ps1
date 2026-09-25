# Kement tool: ransomware canary. Alert when destructive file ops spike.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "CanaryDemo\CanaryDemo.csproj"
$dir   = Join-Path $here "CanaryDemo\bin"
$app   = Join-Path $dir "CanaryDemo.dll"
$plog  = Join-Path $env:TEMP "kement-canary.log"

Write-Host "=== building CanaryDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null
if (Test-Path $plog) { Remove-Item $plog -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-canary-core.log"
$env:KEMENT_PROBE_LOG         = $plog
$env:KEMENT_CANARY_RATE       = "10"
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_MODE","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (fast file churn should trip the canary) ==="
& dotnet $app | ForEach-Object { Write-Host $_ }

Write-Host "`n=== canary log ==="
if (Test-Path $plog) { Get-Content $plog | Select-Object -First 5 }

Write-Host "`n=== verification ==="
$alert = 0; if (Test-Path $plog) { $alert = (Get-Content $plog | Where-Object { $_ -match "CANARY" }).Count }
Write-Host "  canary alerts: $alert"
if ($alert -ge 1) { Write-Host "RESULT: PASS (rate spike detected)"; exit 0 } else { Write-Host "RESULT: FAIL"; exit 2 }
