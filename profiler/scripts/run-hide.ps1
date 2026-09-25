# Kement M2 (anti-analysis): show the CORECLR_* env-var scrub. Runs the same
# target twice; with KEMENT_HIDE=1 the target can no longer see the profiler vars.
$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent
$core   = Join-Path $root "core\build\Release\Kement.Core.dll"
$prj    = Join-Path $root "samples\hide-check\HideCheck.csproj"
$dir    = Join-Path $root "samples\hide-check\bin"
$target = Join-Path $dir "HideCheck.dll"

Write-Host "=== building hide-check ==="
& dotnet build $prj -c Release -o $dir | Out-Null

function Run([string]$hide) {
    $env:CORECLR_ENABLE_PROFILING = "1"
    $env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
    $env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
    $env:KEMENT_LOG               = Join-Path $env:TEMP "kement-hide.log"
    if ($hide -eq "1") { $env:KEMENT_HIDE = "1" } else { Remove-Item Env:KEMENT_HIDE -ErrorAction SilentlyContinue }
    foreach ($v in @("KEMENT_DUMP_DIR","KEMENT_TRIPWIRE","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD")) {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
    & dotnet $target
}

Write-Host "`n=== run 1: KEMENT_HIDE off (target should SEE the profiler) ==="
$off = Run "0"; $off | ForEach-Object { Write-Host $_ }

Write-Host "`n=== run 2: KEMENT_HIDE on (target should NOT see the profiler) ==="
$on = Run "1"; $on | ForEach-Object { Write-Host $_ }

Write-Host "`n=== verification ==="
$offSees = ($off | Where-Object { $_ -match "CORECLR_PROFILER\s+= \{A1B2" }).Count -gt 0
$onHidden = ($on | Where-Object { $_ -match "CORECLR_PROFILER\s+= <empty>" }).Count -gt 0
Write-Host "  off: target saw profiler = $offSees"
Write-Host "  on : profiler hidden     = $onHidden"
if ($offSees -and $onHidden) { Write-Host "RESULT: PASS (env scrub hides the profiler when armed)"; exit 0 }
else { Write-Host "RESULT: FAIL"; exit 2 }
