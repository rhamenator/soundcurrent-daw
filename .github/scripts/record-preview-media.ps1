# SPDX-License-Identifier: GPL-3.0-only
$ErrorActionPreference = 'Stop'
$out = Join-Path (Get-Location) '.cache/windows-media-build-review'
if (Test-Path -LiteralPath $out) { throw 'Owned media build review exists' }
New-Item -ItemType Directory -Path $out | Out-Null
$record = [ordered]@{sourceVersion='1.2.2';sourceSha256='ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121';nativePlatform='win32';linkFlags='/Brepro /PDBALTPATH:sndfile.pdb';twoIndependentBuildDirectories=$true;byteIdentical=$false;productPinChanged=$false}
try {
    $source = Join-Path (Get-Location) 'native-dependencies/libsndfile-1.2.2.tar.gz'
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sourceSha256) { throw 'Media source differs' }
    $runs = @()
    foreach ($name in @('sndfile-build','sndfile-repeat')) {
        $root = Join-Path (Get-Location) ('native-dependencies/' + $name)
        $dll = Join-Path $root 'sndfile.dll'
        Copy-Item -LiteralPath $dll -Destination (Join-Path $out ($name + '-sndfile.dll'))
        $runs += [ordered]@{directory=$root;bytes=(Get-Item -LiteralPath $dll).Length;sha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()}
        Copy-Item -LiteralPath (Join-Path $root 'CMakeCache.txt') -Destination (Join-Path $out ($name + '-CMakeCache.txt'))
        Copy-Item -LiteralPath (Join-Path $root 'build.ninja') -Destination (Join-Path $out ($name + '-build.ninja'))
    }
    $record.builds = $runs
    $record.byteIdentical = ($runs[0].bytes -eq $runs[1].bytes -and $runs[0].sha256 -eq $runs[1].sha256)
    if (-not $record.byteIdentical) { throw 'Fresh native media DLL builds differ; preserve both facts, do not update product pin' }
} catch {
    $record.error = $_.Exception.Message
    throw
} finally {
    $record | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $out 'native-review.json')
}
