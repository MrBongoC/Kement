# Kement tool: return capture. The app prints only a masked secret; Kement reads
# the actual value Reveal() returns.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "SecretApp\SecretApp.csproj"
$dir   = Join-Path $here "SecretApp\bin"
$app   = Join-Path $dir "SecretApp.dll"
$plog  = Join-Path $env:TEMP "kement-secret.log"

Write-Host "=== building SecretApp ==="
& dotnet build $prj -c Release -o $dir | Out-Null
if (Test-Path $plog) { Remove-Item $plog -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-secret-core.log"
$env:KEMENT_PROBE_LOG         = $plog
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_MODE","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (app prints masked; Kement captures the real return) ==="
& dotnet $app | ForEach-Object { Write-Host $_ }

Write-Host "`n=== captured return values ==="
if (Test-Path $plog) { Get-Content $plog | Select-Object -First 3 }

Write-Host "`n=== verification ==="
$revealed = 0; if (Test-Path $plog) { $revealed = (Get-Content $plog | Where-Object { $_ -match "=> S3cr3t-2026" }).Count }
Write-Host "  return values captured showing the real secret: $revealed"
if ($revealed -ge 1) { Write-Host "RESULT: PASS (return value captured; secret revealed)"; exit 0 } else { Write-Host "RESULT: FAIL"; exit 2 }
