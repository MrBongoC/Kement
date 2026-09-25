# Kement follow-up: TRIPWIRE ON LIVE ATTACH. Start an unmonitored process doing
# file ops in a loop, attach the Core with --tripwire, and confirm sink hits show
# up in the target's stdout (logged via CoreLib's Internal.Console, no probe
# assembly needed). Stops only the PID it started.
$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent
$core   = Join-Path $root "core\build\Release\Kement.Core.dll"
$cli    = Join-Path $root "controller\bin\kement.dll"
$prj    = Join-Path $root "samples\fileloop-target\FileLoopTarget.csproj"
$dir    = Join-Path $root "samples\fileloop-target\bin"
$target = Join-Path $dir "FileLoopTarget.dll"
$outFile= Join-Path $env:TEMP "kement-attach-tw-target.out"
$logFile= Join-Path $env:TEMP "kement-attach-tw-core.log"

Write-Host "=== building fileloop target + controller ==="
& dotnet build $prj -c Release -o $dir | Out-Null
& dotnet build (Join-Path $root "controller\Kement.Cli.csproj") -c Release -o (Join-Path $root "controller\bin") | Out-Null
foreach ($f in @($outFile,$logFile)) { if (Test-Path $f) { Remove-Item $f -Force } }

foreach ($v in @("CORECLR_ENABLE_PROFILING","CORECLR_PROFILER","CORECLR_PROFILER_PATH",
                 "KEMENT_LOG","KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","DOTNET_STARTUP_HOOKS")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== starting target unmonitored (loop) ==="
$proc = Start-Process -FilePath "dotnet" -ArgumentList @($target) -PassThru `
                      -RedirectStandardOutput $outFile -NoNewWindow
$pidToAttach = $proc.Id
Write-Host "    target pid = $pidToAttach"

try {
    Start-Sleep -Seconds 3
    Write-Host "`n=== attaching Core with --tripwire ==="
    & dotnet $cli attach --pid $pidToAttach --core $core --tripwire --log $logFile
    if ($LASTEXITCODE -ne 0) { throw "attach failed (exit $LASTEXITCODE)" }
    Write-Host "`n=== waiting for instrumented sink hits ==="
    Start-Sleep -Seconds 6
}
finally {
    if (-not $proc.HasExited) {
        Write-Host "`n=== stopping target pid $pidToAttach ==="
        Stop-Process -Id $pidToAttach -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "`n=== Core log ==="
if (Test-Path $logFile) { Get-Content $logFile | Where-Object { $_ -match "attach|Tripwire|instrumented" } | Select-Object -First 20 }

Write-Host "`n=== target stdout (should include injected [kement] lines) ==="
$out = @(); if (Test-Path $outFile) { $out = Get-Content $outFile }
$out | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }

Write-Host "`n=== verification ==="
# Only the injected instrumentation writes "[kement-sink] ..."; Core KLOG uses "[kement] ".
$hits = $out | Where-Object { $_ -match "^\[kement-sink\]" }
$distinct = $hits | ForEach-Object { if ($_ -match "\[kement-sink\] (\S+)") { $matches[1] } } | Sort-Object -Unique
Write-Host "  injected sink-hit lines: $($hits.Count); sinks: $($distinct -join ', ')"
if ($hits.Count -ge 2) {
    Write-Host "RESULT: PASS (tripwire instrumented a running process on attach)"; exit 0
} else {
    Write-Host "RESULT: FAIL (no injected sink hits in target stdout)"; exit 2
}
