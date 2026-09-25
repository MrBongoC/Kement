# Kement tool: remote kill switch. Flip a feature flag off on a running process.
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$core = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$prj  = Join-Path $here "KillDemo\KillDemo.csproj"
$dir  = Join-Path $here "KillDemo\bin"
$app  = Join-Path $dir "KillDemo.dll"

Write-Host "=== building KillDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-killswitch-core.log"
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","DOTNET_STARTUP_HOOKS")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (feature should read OFF) ==="
$out = & dotnet $app
$out | ForEach-Object { Write-Host $_ }

Write-Host "`n=== verification ==="
$off = ($out | Where-Object { $_ -match "OFF \(killed\)" }).Count
$on  = ($out | Where-Object { $_ -match "= ON" }).Count
Write-Host "  ON lines: $on ; OFF lines: $off"
if ($off -ge 1 -and $on -eq 0) { Write-Host "RESULT: PASS (feature killed live)"; exit 0 } else { Write-Host "RESULT: FAIL"; exit 2 }
