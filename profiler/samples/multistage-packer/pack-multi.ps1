# Build the multi-stage toy: Payload (stage2) -> Loader (stage1) -> MultiPacker (stage0).
# Each stage embeds the AES-encrypted next stage and loads it from memory.
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$repo = Split-Path (Split-Path $root -Parent) -Parent

$key = [Convert]::FromBase64String("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=")
$iv  = [Convert]::FromBase64String("AAECAwQFBgcICQoLDA0ODw==")
function Encrypt([string]$inFile, [string]$outFile) {
    $plain = [System.IO.File]::ReadAllBytes($inFile)
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Key = $key; $aes.IV = $iv
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
    $enc = $aes.CreateEncryptor()
    [System.IO.File]::WriteAllBytes($outFile, $enc.TransformFinalBlock($plain, 0, $plain.Length))
    $sha = [System.Security.Cryptography.SHA256]::Create()
    (($sha.ComputeHash($plain) | ForEach-Object { $_.ToString("x2") }) -join "")
}

Write-Host "=== stage2: Payload.dll ==="
& dotnet build "$repo\samples\toy-packer\Payload\Payload.csproj" -c Release -o "$repo\samples\toy-packer\Payload\bin" | Out-Null
$payload = "$repo\samples\toy-packer\Payload\bin\Payload.dll"
$h2 = Encrypt $payload "$root\Loader\stage2.enc"
Write-Host "    Payload.dll sha256=$h2"

Write-Host "=== stage1: Loader.dll (embeds stage2) ==="
& dotnet build "$root\Loader\Loader.csproj" -c Release -o "$root\Loader\bin" | Out-Null
$loader = "$root\Loader\bin\Loader.dll"
$h1 = Encrypt $loader "$root\Packer\stage1.enc"
Write-Host "    Loader.dll sha256=$h1"

Write-Host "=== stage0: MultiPacker.exe (embeds stage1) ==="
& dotnet build "$root\Packer\MultiPacker.csproj" -c Release -o "$root\Packer\bin" | Out-Null
Write-Host "=== done. stub: $root\Packer\bin\MultiPacker.dll ==="
Write-Host "    expected dumps: Loader.dll ($h1), Payload.dll ($h2)"
