# Kement M0 harness: launch the test target under the Core via startup attach.
# Proves attach + JIT events + ReJIT rewrite end to end.
param(
    [string]$Clsid   = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}",
    [string]$Core    = "$PSScriptRoot\..\core\build\Release\Kement.Core.dll",
    [string]$Target  = "$PSScriptRoot\..\test-target\bin\TestTarget.dll",
    [string]$LogFile = "$env:TEMP\kement-m0.log",
    [int]$RetVal     = 222
)

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = $Clsid
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $Core).Path
$env:KEMENT_LOG               = $LogFile
$env:KEMENT_TARGET_ASSEMBLY   = "TestTarget"
$env:KEMENT_REJIT_TYPE        = "TestTarget.Program"
$env:KEMENT_REJIT_METHOD      = "MagicNumber"
$env:KEMENT_REJIT_RETVAL      = "$RetVal"

Write-Host "=== launching target under Kement Core ==="
& dotnet (Resolve-Path $Target).Path
Write-Host "=== target exited with code $LASTEXITCODE ==="
