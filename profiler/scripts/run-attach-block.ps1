# Kement follow-up: BLOCK on live attach. Start an unmonitored file-ops loop,
# attach with --tripwire --mode block --deny, and confirm the denied file ops are
# short-circuited on the running process (the target reports BLOCKED after attach
# but ran fine before). Stops only the PID it started.
$ErrorActionPreference = "Stop"
$root   = Split-Path $PSScriptRoot -Parent
$core   = Join-Path $root "core\build\Release\Kement.Core.dll"
$cli    = Join-Path $root "controller\bin\kement.dll"
$prj    = Join-Path $root "samples\fileloop-target\FileLoopTarget.csproj"
$dir    = Join-Path $root "samples\fileloop-target\bin"
$target = Join-Path $dir "FileLoopTarget.dll"
$outFile= Join-Path $env:TEMP "kement-attach-block.out"
$logFile= Join-Path $env:TEMP "kement-attach-block-core.log"

Write-Host "=== building target + controller ==="
& dotnet build $prj -c Release -o $dir | Out-Null
& dotnet build (Join-Path $root "controller\Kement.Cli.csproj") -c Release -o (Join-Path $root "controller\bin") | Out-Null
foreach ($f in @($outFile,$logFile)) { if (Test-Path $f) { Remove-Item $f -Force } }
foreach ($v in @("CORECLR_ENABLE_PROFILING","CORECLR_PROFILER","CORECLR_PROFILER_PATH",
                 "KEMENT_LOG","KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","DOTNET_STARTUP_HOOKS")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== starting target unmonitored ==="
$proc = Start-Process -FilePath "dotnet" -ArgumentList @($target) -PassThru -RedirectStandardOutput $outFile -NoNewWindow
$pidToAttach = $proc.Id
Write-Host "    target pid = $pidToAttach"

try {
    Start-Sleep -Seconds 3
    Write-Host "`n=== attaching with --tripwire --mode block --deny kement_fileloop ==="
    & dotnet $cli attach --pid $pidToAttach --core $core --tripwire --mode block --deny "kement_fileloop" --log $logFile
    if ($LASTEXITCODE -ne 0) { throw "attach failed ($LASTEXITCODE)" }
    Start-Sleep -Seconds 6
}
finally {
    if (-not $proc.HasExited) { Write-Host "`n=== stopping pid $pidToAttach ==="; Stop-Process -Id $pidToAttach -Force -ErrorAction SilentlyContinue }
}

$out = @(); if (Test-Path $outFile) { $out = Get-Content $outFile }
Write-Host "`n=== target stdout (iter lines) ==="
$iters = $out | Where-Object { $_ -match "^\[fileloop\] iter" }
$iters | Select-Object -First 3 | ForEach-Object { Write-Host "  BEFORE: $_" }
$iters | Select-Object -Last 3  | ForEach-Object { Write-Host "  AFTER : $_" }

Write-Host "`n=== verification ==="
$before = $iters | Where-Object { $_ -match "write=ok read=ok delete=ok" }
$blocked = $iters | Where-Object { $_ -match "BLOCKED" }
Write-Host "  clean-iters before attach: $($before.Count); blocked-iters after attach: $($blocked.Count)"
if ($before.Count -ge 1 -and $blocked.Count -ge 1) {
    Write-Host "RESULT: PASS (block took effect on a live-attached process)"; exit 0
} else {
    Write-Host "RESULT: FAIL"; exit 2
}
