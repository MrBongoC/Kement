# Kement tool: self-reporting honeypot. Point the tripwire (monitor mode) at an
# app and get the full attack trail: every sensitive op with the calling method.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "HoneypotDemo\HoneypotDemo.csproj"
$dir   = Join-Path $here "HoneypotDemo\bin"
$app   = Join-Path $dir "HoneypotDemo.dll"
$plog  = Join-Path $env:TEMP "kement-honeypot.log"

Write-Host "=== building HoneypotDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null
Copy-Item $probe -Destination $dir -Force
if (Test-Path $plog) { Remove-Item $plog -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-honeypot-core.log"
$env:KEMENT_TRIPWIRE          = "1"
$env:KEMENT_MODE              = "monitor"
$env:KEMENT_PROBE_LOG         = $plog
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_HOOKS","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (attack trail captured by the tripwire) ==="
& dotnet $app | ForEach-Object { Write-Host $_ }

Write-Host "`n=== attack trail ==="
if (Test-Path $plog) { Get-Content $plog }

Write-Host "`n=== verification ==="
$hits = @(); if (Test-Path $plog) { $hits = Get-Content $plog }
$trail = ($hits | Where-Object { $_ -match "RequestHandler.Handle" }).Count
Write-Host "  sink hits attributed to RequestHandler.Handle: $trail"
if ($trail -ge 2) { Write-Host "RESULT: PASS (attack trail captured with caller)"; exit 0 } else { Write-Host "RESULT: FAIL"; exit 2 }
