# Kement M4: run a target under the Core with the tripwire armed (monitor mode).
# The Core instruments System.IO.File sinks via ReJIT; each hit calls the managed
# probe, which logs the sink, its path argument, and the managed caller.
$ErrorActionPreference = "Stop"
$root     = Split-Path $PSScriptRoot -Parent
$core     = Join-Path $root "core\build\Release\Kement.Core.dll"
$probe    = Join-Path $root "probes\bin\Kement.Probes.dll"
$targetPrj= Join-Path $root "samples\tripwire-target\TripwireTarget.csproj"
$targetDir= Join-Path $root "samples\tripwire-target\bin"
$target   = Join-Path $targetDir "TripwireTarget.dll"
$probeLog = Join-Path $env:TEMP "kement-probe.log"
$coreLog  = Join-Path $env:TEMP "kement-tripwire-core.log"

Write-Host "=== building tripwire target ==="
& dotnet build $targetPrj -c Release -o $targetDir | Out-Null
# The injected call is into CoreLib; the probe assembly must resolve from the
# app base, so drop it next to the target.
Copy-Item $probe -Destination $targetDir -Force

foreach ($f in @($probeLog, $coreLog)) { if (Test-Path $f) { Remove-Item $f -Force } }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_LOG               = $coreLog
$env:KEMENT_TRIPWIRE          = "1"
$env:KEMENT_PROBE_LOG         = $probeLog
# Make Kement.Probes resolvable from framework code via a startup hook.
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "`n=== running target under Kement (tripwire, monitor mode) ==="
& dotnet $target
Write-Host "=== target exited with code $LASTEXITCODE ==="

Write-Host "`n=== Core tripwire log ==="
if (Test-Path $coreLog) { Get-Content $coreLog | Where-Object { $_ -match "Tripwire|instrumented|armed" } }

Write-Host "`n=== probe log (sink hits) ==="
if (Test-Path $probeLog) { Get-Content $probeLog } else { Write-Host "(no probe log)" }

Write-Host "`n=== verification ==="
$hits = @(); if (Test-Path $probeLog) { $hits = Get-Content $probeLog }
$haveDelete = ($hits | Where-Object { $_ -match "file.delete" }).Count -gt 0
$distinct = ($hits | ForEach-Object { ($_ -split '\s+')[1] } | Sort-Object -Unique)
Write-Host "  sink hits: $($hits.Count), distinct sinks: $($distinct -join ', ')"
if ($haveDelete -and $hits.Count -ge 2) {
    Write-Host "RESULT: PASS (tripwire caught file sinks with args + caller)"; exit 0
} else {
    Write-Host "RESULT: FAIL (expected file.delete plus other sink hits)"; exit 2
}
