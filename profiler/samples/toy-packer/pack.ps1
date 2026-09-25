# Build the toy self-unpacker:
#   1. build Payload.dll (the "hidden" assembly)
#   2. AES-encrypt it into Packer/payload.enc (same key/IV as Program.cs)
#   3. build the Packer stub with the ciphertext embedded
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

Write-Host "=== building Payload.dll ==="
& dotnet build "$root\Payload\Payload.csproj" -c Release -o "$root\Payload\bin" | Out-Null
$payloadDll = "$root\Payload\bin\Payload.dll"
if (-not (Test-Path $payloadDll)) { throw "Payload.dll not produced" }

Write-Host "=== encrypting payload -> payload.enc ==="
$key = [Convert]::FromBase64String("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=")
$iv  = [Convert]::FromBase64String("AAECAwQFBgcICQoLDA0ODw==")
$plain = [System.IO.File]::ReadAllBytes($payloadDll)

$aes = [System.Security.Cryptography.Aes]::Create()
$aes.Key = $key; $aes.IV = $iv
$aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
$aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
$enc = $aes.CreateEncryptor()
$cipher = $enc.TransformFinalBlock($plain, 0, $plain.Length)
[System.IO.File]::WriteAllBytes("$root\Packer\payload.enc", $cipher)

$sha = [System.Security.Cryptography.SHA256]::Create()
$hash = ($sha.ComputeHash($plain) | ForEach-Object { $_.ToString("x2") }) -join ""
Write-Host "    Payload.dll: $($plain.Length) bytes, sha256=$hash"
Write-Host "    payload.enc: $($cipher.Length) bytes"

Write-Host "=== building Packer stub ==="
& dotnet build "$root\Packer\Packer.csproj" -c Release -o "$root\Packer\bin" | Out-Null
Write-Host "=== done. stub: $root\Packer\bin\Packer.dll ==="
