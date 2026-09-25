# Kement M5: tripwire in BLOCK mode. A denied file operation is short-circuited
# (the probe throws before the real sink runs), so the target's delete fails and
# the file survives -- behavioral proof that block actually prevents the op.
$ErrorActionPreference = "Stop"
$root      = Split-Path $PSScriptRoot -Parent
$core      = Join-Path $root "core\build\Release\Kement.Core.dll"
$probe     = Join-Path $root "probes\bin\Kement.Probes.dll"
$targetPrj = Join-Path $root "samples\block-target\BlockTarget.csproj"
$targetDir = Join-Path $root "samples\block-target\bin"
$target    = Join-Path $targetDir "BlockTarget.dll"
$probeLog  = Join-Path $env:TEMP "kement-block-probe.log"
$coreLog   = Join-Path $env:TEMP "kement-block-core.log"

Write-Host "=== building block target ==="
& dotnet build $targetPrj -c Release -o $targetDir | Out-Null
Copy-Item $probe -Destination $targetDir -Force
foreach ($f in @($probeLog, $coreLog)) { if (Test-Path $f) { Remove-Item $f -Force } }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_LOG               = $coreLog
$env:KEMENT_TRIPWIRE          = "1"
$env:KEMENT_PROBE_LOG         = $probeLog
$env:KEMENT_MODE              = "block"
$env:KEMENT_DENY              = "kement_block"
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_ALLOW")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "`n=== running target under Kement (tripwire, BLOCK mode, deny='kement_block') ==="
$out = & dotnet $target
$out | ForEach-Object { Write-Host $_ }
Write-Host "=== target exited with code $LASTEXITCODE ==="

Write-Host "`n=== probe log ==="
if (Test-Path $probeLog) { Get-Content $probeLog } else { Write-Host "(no probe log)" }

Write-Host "`n=== verification ==="
$hits = @(); if (Test-Path $probeLog) { $hits = Get-Content $probeLog }
$blockCount = ($hits | Where-Object { $_ -match "BLOCK" }).Count
$readBlocked   = ($out | Where-Object { $_ -match "read blocked  = True" }).Count -gt 0
$deleteBlocked = ($out | Where-Object { $_ -match "delete blocked = True" }).Count -gt 0
$survived      = ($out | Where-Object { $_ -match "still exists after delete attempt = True" }).Count -gt 0
Write-Host "  BLOCK log lines: $blockCount; read blocked: $readBlocked; delete blocked: $deleteBlocked; file survived: $survived"
if ($blockCount -ge 2 -and $deleteBlocked -and $survived) {
    Write-Host "RESULT: PASS (block mode short-circuited the denied file ops)"; exit 0
} else {
    Write-Host "RESULT: FAIL"; exit 2
}
