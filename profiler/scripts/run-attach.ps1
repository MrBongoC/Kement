# Kement M3: attach the Core to an ALREADY-RUNNING .NET process.
# Starts the toy unpacker with no profiler env, attaches mid-flight via the
# diagnostics IPC, and verifies the Core catches an in-memory module loaded
# AFTER attach. Stops only the exact PID it started.
$ErrorActionPreference = "Stop"
$root    = Split-Path $PSScriptRoot -Parent
$core    = Join-Path $root "core\build\Release\Kement.Core.dll"
$cli     = Join-Path $root "controller\bin\kement.dll"
$stub    = Join-Path $root "samples\toy-packer\Packer\bin\Packer.dll"
$payload = Join-Path $root "samples\toy-packer\Payload\bin\Payload.dll"
$dumpDir = Join-Path $env:TEMP "kement-attach-dump"
$logFile = Join-Path $env:TEMP "kement-attach.log"
$outFile = Join-Path $env:TEMP "kement-attach-target.out"

if (Test-Path $dumpDir) { Remove-Item $dumpDir -Recurse -Force }
foreach ($f in @($logFile, $outFile)) { if (Test-Path $f) { Remove-Item $f -Force } }

# Ensure the target starts with NO profiler env (a genuinely unmonitored process).
foreach ($v in @("CORECLR_ENABLE_PROFILING","CORECLR_PROFILER","CORECLR_PROFILER_PATH",
                 "KEMENT_LOG","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== starting target unmonitored (loop mode) ==="
$proc = Start-Process -FilePath "dotnet" -ArgumentList @($stub, "loop") -PassThru `
                      -RedirectStandardOutput $outFile -NoNewWindow
$pidToAttach = $proc.Id
Write-Host "    target pid = $pidToAttach"

try {
    Start-Sleep -Seconds 3   # let it get past startup and into the loop
    Write-Host "`n=== attaching Kement Core to the running process ==="
    & dotnet $cli attach --pid $pidToAttach --core $core --dump $dumpDir --log $logFile --target Payload
    if ($LASTEXITCODE -ne 0) { throw "attach failed (exit $LASTEXITCODE)" }

    Write-Host "`n=== waiting for post-attach in-memory module loads ==="
    Start-Sleep -Seconds 6
}
finally {
    if (-not $proc.HasExited) {
        Write-Host "`n=== stopping target pid $pidToAttach ==="
        Stop-Process -Id $pidToAttach -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "`n=== Core log (post-attach) ==="
if (Test-Path $logFile) { Get-Content $logFile } else { Write-Host "(no core log written)" }

Write-Host "`n=== dumped files ==="
if (Test-Path $dumpDir) {
    Get-ChildItem $dumpDir | ForEach-Object { Write-Host ("  {0}  {1} bytes" -f $_.Name, $_.Length) }
} else { Write-Host "(no dumps)" }

Write-Host "`n=== verification vs original Payload.dll ==="
$pass = $false
if ((Test-Path $payload) -and (Test-Path $dumpDir)) {
    $want = (Get-FileHash -Algorithm SHA256 $payload).Hash.ToLower()
    Write-Host "  Payload.dll sha256 = $want"
    Get-ChildItem $dumpDir -Filter *.dll | ForEach-Object {
        $got = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower()
        $match = if ($got -eq $want) { $script:pass = $true; "MATCH" } else { "differ" }
        Write-Host ("  {0} sha256 = {1}  [{2}]" -f $_.Name, $got, $match)
    }
}
Write-Host ""
if ($pass) { Write-Host "RESULT: PASS (attached to a running process and dumped its in-memory payload)"; exit 0 }
else       { Write-Host "RESULT: FAIL (no post-attach dump matched the payload)"; exit 2 }
