# Kement M1: run the toy self-unpacker under the Core with the unpacker armed,
# then verify each dumped assembly byte-for-byte against the known payload.
param(
    [string]$Clsid   = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}",
    [string]$Core    = "$PSScriptRoot\..\core\build\Release\Kement.Core.dll",
    [string]$Stub    = "$PSScriptRoot\..\samples\toy-packer\Packer\bin\Packer.dll",
    [string]$Payload = "$PSScriptRoot\..\samples\toy-packer\Payload\bin\Payload.dll",
    [string]$DumpDir = "$env:TEMP\kement-dump",
    [string]$LogFile = "$env:TEMP\kement-unpack.log"
)

if (Test-Path $DumpDir) { Remove-Item $DumpDir -Recurse -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = $Clsid
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $Core).Path
$env:KEMENT_LOG               = $LogFile
$env:KEMENT_DUMP_DIR          = $DumpDir
# ReJIT demo off for the unpacker run.
Remove-Item Env:KEMENT_REJIT_TYPE   -ErrorAction SilentlyContinue
Remove-Item Env:KEMENT_REJIT_METHOD -ErrorAction SilentlyContinue

Write-Host "=== running toy self-unpacker under Kement (unpacker armed) ==="
& dotnet (Resolve-Path $Stub).Path
Write-Host "=== stub exited with code $LASTEXITCODE ==="

Write-Host "`n=== dumped files ==="
if (-not (Test-Path $DumpDir)) { Write-Host "NO DUMP DIR PRODUCED"; exit 1 }
Get-ChildItem $DumpDir | ForEach-Object { Write-Host ("  {0}  {1} bytes" -f $_.Name, $_.Length) }

Write-Host "`n=== manifest ==="
if (Test-Path "$DumpDir\manifest.txt") { Get-Content "$DumpDir\manifest.txt" }

Write-Host "`n=== verification vs original Payload.dll ==="
$want = (Get-FileHash -Algorithm SHA256 (Resolve-Path $Payload).Path).Hash.ToLower()
Write-Host "  Payload.dll sha256 = $want"
$pass = $false
Get-ChildItem $DumpDir -Filter *.dll | ForEach-Object {
    $got = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
    $match = if ($got -eq $want) { "MATCH"; $script:pass = $true } else { "differ" }
    Write-Host ("  {0} sha256 = {1}  [{2}]" -f $_.Name, $got, $match)
}
Write-Host ""
if ($pass) { Write-Host "RESULT: PASS (recovered Payload.dll byte-for-byte)"; exit 0 }
else       { Write-Host "RESULT: FAIL (no dump matched the payload)"; exit 2 }
