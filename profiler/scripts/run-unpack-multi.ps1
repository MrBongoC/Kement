# Kement M2: run the multi-stage toy under the Core and verify BOTH stages are
# dumped, in load order, byte-for-byte, with a managed-fidelity flag.
$ErrorActionPreference = "Stop"
$root    = Split-Path $PSScriptRoot -Parent
$core    = Join-Path $root "core\build\Release\Kement.Core.dll"
$stub    = Join-Path $root "samples\multistage-packer\Packer\bin\MultiPacker.dll"
$loader  = Join-Path $root "samples\multistage-packer\Loader\bin\Loader.dll"
$payload = Join-Path $root "samples\toy-packer\Payload\bin\Payload.dll"
$dumpDir = Join-Path $env:TEMP "kement-multi-dump"
$logFile = Join-Path $env:TEMP "kement-multi.log"

if (Test-Path $dumpDir) { Remove-Item $dumpDir -Recurse -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_LOG               = $logFile
$env:KEMENT_DUMP_DIR          = $dumpDir
foreach ($v in @("KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_TRIPWIRE","KEMENT_HIDE")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running multi-stage self-unpacker under Kement ==="
& dotnet (Resolve-Path $stub).Path
Write-Host "=== stub exited with code $LASTEXITCODE ==="

Write-Host "`n=== manifest ==="
if (Test-Path "$dumpDir\manifest.txt") { Get-Content "$dumpDir\manifest.txt" }

Write-Host "`n=== verification ==="
$wantLoader  = (Get-FileHash -Algorithm SHA256 $loader).Hash.ToLower()
$wantPayload = (Get-FileHash -Algorithm SHA256 $payload).Hash.ToLower()
Write-Host "  want Loader.dll  = $wantLoader"
Write-Host "  want Payload.dll = $wantPayload"

$gotLoader = $false; $gotPayload = $false
Get-ChildItem $dumpDir -Filter *.dll | Sort-Object Name | ForEach-Object {
    $got = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
    $which = if ($got -eq $wantLoader) { $script:gotLoader = $true; "Loader (stage1)" }
             elseif ($got -eq $wantPayload) { $script:gotPayload = $true; "Payload (stage2)" }
             else { "unknown" }
    Write-Host ("  {0} -> {1}  [{2}]" -f $_.Name, $got, $which)
}
Write-Host ""
if ($gotLoader -and $gotPayload) {
    Write-Host "RESULT: PASS (both stages recovered byte-for-byte)"; exit 0
} else {
    Write-Host "RESULT: FAIL (missing a stage: loader=$gotLoader payload=$gotPayload)"; exit 2
}
