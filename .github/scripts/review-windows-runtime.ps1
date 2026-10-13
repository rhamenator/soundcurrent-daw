# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$CollectionDirectory)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$out = Join-Path $CollectionDirectory 'runtime-review'
if (Test-Path -LiteralPath $out) { throw 'Owned runtime review already exists' }
New-Item -ItemType Directory -Path $out | Out-Null
$exe = Join-Path $out 'vc_redist.x64.exe'
$url = 'https://aka.ms/vc14/vc_redist.x64.exe'
$record = [ordered]@{sourceUrl=$url;downloaded=$false;executed=$false;installed=$false;signatureQualified=$false;runtimeVersionQualified=$false}
try {
    Invoke-WebRequest -Uri $url -OutFile $exe -TimeoutSec 120
    $record.downloaded = $true
    $file = Get-Item -LiteralPath $exe
    if ($file.Length -gt 64MB -or $file.Length -lt 1MB) { throw 'Unexpected runtime package size' }
    $record.bytes = $file.Length
    $record.sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $record.fileVersion = $file.VersionInfo.FileVersion
    $record.productVersion = $file.VersionInfo.ProductVersion
    $record.originalFilename = $file.VersionInfo.OriginalFilename
    $record.productName = $file.VersionInfo.ProductName
    $signature = Get-AuthenticodeSignature -LiteralPath $exe
    $record.signatureStatus = [string]$signature.Status
    $record.signatureMessage = $signature.StatusMessage
    if ($signature.SignerCertificate) {
        $record.signerSubject = $signature.SignerCertificate.Subject
        $record.signerIssuer = $signature.SignerCertificate.Issuer
        $record.signerThumbprint = $signature.SignerCertificate.Thumbprint
    }
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(?:^|,\s*)O=Microsoft Corporation(?:,|$)') {
        throw 'Native Windows trust did not validate Microsoft runtime signer'
    }
    $record.signatureQualified = $true
    if ($record.originalFilename -ine 'VC_redist.x64.exe' -or
        $record.productVersion -notmatch '^14\.[0-9]+\.[0-9]+\.[0-9]+$') {
        throw 'Unexpected Microsoft x64 runtime version identity'
    }
    $compiler = Get-Content -LiteralPath (Join-Path $CollectionDirectory 'compiler.json') -Raw | ConvertFrom-Json
    if ($compiler.crtVersion) {
        $record.crtVersion = $compiler.crtVersion
        $record.runtimeVersionQualified = ([Version]$record.productVersion -ge [Version]$compiler.crtVersion)
    }
    if (-not $record.runtimeVersionQualified) { $record.pending='Native CRT version absent or newer than runtime; do not change product pin' }
} catch {
    $record.error = $_.Exception.Message
    throw
} finally {
    $record | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $out 'native-review.json')
}
