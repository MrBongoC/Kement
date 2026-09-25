# Kement tool: live SQL tracer. See every query a running app runs, no code change.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "SqlDemo\SqlDemo.csproj"
$dir   = Join-Path $here "SqlDemo\bin"
$app   = Join-Path $dir "SqlDemo.dll"
$plog  = Join-Path $env:TEMP "kement-sql.log"

Write-Host "=== building SqlDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null
if (Test-Path $plog) { Remove-Item $plog -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-sql-core.log"
$env:KEMENT_PROBE_LOG         = $plog
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_MODE","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (app runs SQL; Kement logs every statement) ==="
& dotnet $app | ForEach-Object { Write-Host $_ }

Write-Host "`n=== SQL trace log ==="
if (Test-Path $plog) { Get-Content $plog }

Write-Host "`n=== verification ==="
$hits = @(); if (Test-Path $plog) { $hits = Get-Content $plog }
$create = ($hits | Where-Object { $_ -match "CREATE TABLE" }).Count -gt 0
$select = ($hits | Where-Object { $_ -match "SELECT name FROM users" }).Count -gt 0
Write-Host "  traced CREATE=$create ; traced SELECT=$select"
if ($create -and $select) { Write-Host "RESULT: PASS (SQL statements traced live)"; exit 0 } else { Write-Host "RESULT: FAIL"; exit 2 }
