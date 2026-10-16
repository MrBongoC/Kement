# Kement tool: crack a RUNNING app live. Start a locked vault service unmonitored,
# attach mid-run with a hook that flips its lock check, watch it breach without a
# restart. Stops only the PID it started.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$cli   = Join-Path $here "..\profiler\controller\bin\kement.dll"
$prj   = Join-Path $here "VaultServer\VaultServer.csproj"
$dir   = Join-Path $here "VaultServer\bin"
$app   = Join-Path $dir "VaultServer.dll"
$out   = Join-Path $env:TEMP "kement-vault.out"

Write-Host "=== building VaultServer + controller ==="
& dotnet build $prj -c Release -o $dir | Out-Null
& dotnet build (Join-Path $here "..\profiler\controller\Kement.Cli.csproj") -c Release -o (Join-Path $here "..\profiler\controller\bin") | Out-Null
if (Test-Path $out) { Remove-Item $out -Force }
foreach ($v in @("CORECLR_ENABLE_PROFILING","CORECLR_PROFILER","CORECLR_PROFILER_PATH","KEMENT_HOOKS","KEMENT_LOG","KEMENT_TRIPWIRE")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== starting locked vault (unmonitored) ==="
$proc = Start-Process -FilePath "dotnet" -ArgumentList @($app) -PassThru -RedirectStandardOutput $out -NoNewWindow
try {
    Start-Sleep -Seconds 4
    Write-Host "=== attaching Kement with the crack hook ==="
    & dotnet $cli attach --pid $proc.Id --core $core --hooks (Join-Path $here "hooks.txt") --log (Join-Path $env:TEMP "kement-vault-core.log")
    if ($LASTEXITCODE -ne 0) { throw "attach failed ($LASTEXITCODE)" }
    Start-Sleep -Seconds 6
}
finally {
    if (-not $proc.HasExited) { Write-Host "=== stopping pid $($proc.Id) ==="; Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
}

$lines = @(); if (Test-Path $out) { $lines = Get-Content $out }
Write-Host "`n=== vault output (before / after attach) ==="
$lines | Where-Object { $_ -match "tick" } | Select-Object -First 3 | ForEach-Object { Write-Host "  BEFORE: $_" }
$lines | Where-Object { $_ -match "tick" } | Select-Object -Last 3  | ForEach-Object { Write-Host "  AFTER : $_" }

Write-Host "`n=== verification ==="
$locked = ($lines | Where-Object { $_ -match "= LOCKED" }).Count
$breached = ($lines | Where-Object { $_ -match "UNLOCKED" }).Count
Write-Host "  LOCKED ticks: $locked ; UNLOCKED ticks: $breached"
if ($locked -ge 1 -and $breached -ge 1) { Write-Host "RESULT: PASS (cracked a running app live via attach)"; exit 0 }
else { Write-Host "RESULT: FAIL"; exit 2 }
