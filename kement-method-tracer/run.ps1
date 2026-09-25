# Kement tool: trace any method + its argument from outside, with no code change.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "TraceDemo\TraceDemo.csproj"
$dir   = Join-Path $here "TraceDemo\bin"
$app   = Join-Path $dir "TraceDemo.dll"
$plog  = Join-Path $env:TEMP "kement-tracer.log"

Write-Host "=== building TraceDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null
if (Test-Path $plog) { Remove-Item $plog -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-tracer-core.log"
$env:KEMENT_PROBE_LOG         = $plog
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_MODE","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (app has no logging; trace comes from Kement) ==="
& dotnet $app | ForEach-Object { Write-Host $_ }

Write-Host "`n=== trace log ==="
if (Test-Path $plog) { Get-Content $plog } else { Write-Host "(no trace log)" }

Write-Host "`n=== verification ==="
$hits = @(); if (Test-Path $plog) { $hits = Get-Content $plog }
$login  = ($hits | Where-Object { $_ -match "Service.Login\s+alice" }).Count -gt 0
$charge = ($hits | Where-Object { $_ -match "Service.Charge\s+4111" }).Count -gt 0
Write-Host "  traced Login(alice)=$login ; traced Charge(4111...)=$charge"
if ($login -and $charge) { Write-Host "RESULT: PASS (methods + args traced live)"; exit 0 }
else { Write-Host "RESULT: FAIL"; exit 2 }
