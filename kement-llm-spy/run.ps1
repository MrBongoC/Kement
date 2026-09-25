# Kement tool: spy on an AI agent. Log every prompt an app sends to an LLM.
$ErrorActionPreference = "Stop"
$here  = $PSScriptRoot
$core  = Join-Path $here "..\profiler\core\build\Release\Kement.Core.dll"
$probe = Join-Path $here "..\profiler\probes\bin\Kement.Probes.dll"
$prj   = Join-Path $here "LlmDemo\LlmDemo.csproj"
$dir   = Join-Path $here "LlmDemo\bin"
$app   = Join-Path $dir "LlmDemo.dll"
$plog  = Join-Path $env:TEMP "kement-llmspy.log"

Write-Host "=== building LlmDemo ==="
& dotnet build $prj -c Release -o $dir | Out-Null
if (Test-Path $plog) { Remove-Item $plog -Force }

$env:CORECLR_ENABLE_PROFILING = "1"
$env:CORECLR_PROFILER         = "{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}"
$env:CORECLR_PROFILER_PATH    = (Resolve-Path $core).Path
$env:KEMENT_HOOKS             = (Resolve-Path (Join-Path $here "hooks.txt")).Path
$env:KEMENT_LOG               = Join-Path $env:TEMP "kement-llmspy-core.log"
$env:KEMENT_PROBE_LOG         = $plog
$env:DOTNET_STARTUP_HOOKS     = (Resolve-Path $probe).Path
foreach ($v in @("KEMENT_TRIPWIRE","KEMENT_DUMP_DIR","KEMENT_REJIT_TYPE","KEMENT_REJIT_METHOD","KEMENT_MODE","KEMENT_DENY")) {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

Write-Host "=== running (app talks to 'the model'; prompts captured by Kement) ==="
& dotnet $app | ForEach-Object { Write-Host $_ }

Write-Host "`n=== captured prompts ==="
if (Test-Path $plog) { Get-Content $plog }

Write-Host "`n=== verification ==="
$n = 0; if (Test-Path $plog) { $n = (Get-Content $plog | Where-Object { $_ -match "LlmClient.Complete" }).Count }
Write-Host "  captured prompts: $n"
if ($n -ge 3) { Write-Host "RESULT: PASS (LLM prompts captured live)"; exit 0 } else { Write-Host "RESULT: FAIL"; exit 2 }
