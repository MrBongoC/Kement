# Kement follow-up: signature-precise overload selection + a process-spawn sink.
# Runs a target that writes a file and spawns a process, first in monitor mode
# (both sinks should fire, incl. the WriteAllText overload M4 missed), then in
# block mode denying the spawn (Process.Start is short-circuited).
$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent
$core   = Join-Path $root "core\build\Release\Kement.Core.dll"
$probe  = Join-Path $root "probes\bin\Kement.Probes.dll"
$prj    = Join-Path $root "samples\process-target\ProcessTarget.csproj"
$dir    = Join-Path $root "samples\process-target\bin"
$target = Join-Path $dir "ProcessTarget.dll"

Write-Host "=== building process target ==="
& dotnet build $prj -c Release -o $dir | Out-Null
Copy-Item $probe -Destination $dir -Force

function RunMode([string]$mode, [string]$deny, [string]$probeLog) {
    if (Test-Path $probeLog) { Remove-Item $probeLog -Force }
    $env:CORECLR_ENABLE_PROFILING = "1"
    $env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
    $env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
    $env:KEMENT_LOG               = Join-Path $env:TEMP "kement-proc-core.log"
    $env:KEMENT_TRIPWIRE          = "1"
    $env:KEMENT_PROBE_LOG         = $probeLog
    $env:KEMENT_MODE              = $mode
    $env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
    if ($deny) { $env:KEMENT_DENY = $deny } else { Remove-Item Env:KEMENT_DENY -ErrorAction SilentlyContinue }
    foreach ($v in @("KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_ALLOW")) {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
    & dotnet $target
}

$monLog = Join-Path $env:TEMP "kement-proc-monitor.log"
$blkLog = Join-Path $env:TEMP "kement-proc-block.log"

Write-Host "`n=== MONITOR mode ==="
$mon = RunMode "monitor" "" $monLog
$mon | ForEach-Object { Write-Host $_ }
Write-Host "--- probe log ---"; if (Test-Path $monLog) { Get-Content $monLog }

Write-Host "`n=== BLOCK mode (deny 'hostname') ==="
$blk = RunMode "block" "hostname" $blkLog
$blk | ForEach-Object { Write-Host $_ }
Write-Host "--- probe log ---"; if (Test-Path $blkLog) { Get-Content $blkLog }

Write-Host "`n=== verification ==="
$monHits = @(); if (Test-Path $monLog) { $monHits = Get-Content $monLog }
$monWrite = ($monHits | Where-Object { $_ -match "file.write" }).Count -gt 0
$monProc  = ($monHits | Where-Object { $_ -match "process.start" }).Count -gt 0
$blkHits = @(); if (Test-Path $blkLog) { $blkHits = Get-Content $blkLog }
$blkProc  = ($blkHits | Where-Object { $_ -match "BLOCK\s+process.start" }).Count -gt 0
$spawnBlocked = ($blk | Where-Object { $_ -match "spawn blocked" }).Count -gt 0
Write-Host "  monitor: file.write fired=$monWrite, process.start fired=$monProc"
Write-Host "  block:   process.start BLOCK=$blkProc, target saw spawn blocked=$spawnBlocked"
if ($monWrite -and $monProc -and $blkProc -and $spawnBlocked) {
    Write-Host "RESULT: PASS (overloads fixed; process-spawn sink monitored and blocked)"; exit 0
} else {
    Write-Host "RESULT: FAIL"; exit 2
}
