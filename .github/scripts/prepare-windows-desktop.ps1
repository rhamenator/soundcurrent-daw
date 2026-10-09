# SPDX-License-Identifier: GPL-3.0-only
# Hosted CI only. No local VM, installer, driver or audio endpoint changes.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$pin = Get-Content -Raw -Encoding utf8 research/windows-desktop-test-dependencies.json | ConvertFrom-Json
if ($pin.format -ne 'sc-windows-desktop-test-dependencies-v1' -or $pin.qtVersion -ne '6.12.0' -or
    $pin.module -ne 'QtBase' -or -not $pin.archive.url.StartsWith('https://download.qt.io/')) {
    throw 'Unsupported desktop test dependency record'
}
$root = Join-Path (Get-Location) 'native-ui-dependencies'
if (Test-Path -LiteralPath $root) { throw 'Owned native desktop dependency directory already exists' }
New-Item -ItemType Directory -Path $root | Out-Null
$archive = Join-Path $root 'qtbase.7z'
Invoke-WebRequest -Uri $pin.archive.url -OutFile $archive -TimeoutSec 120
if ((Get-Item -LiteralPath $archive).Length -ne $pin.archive.bytes -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant() -ne $pin.archive.sha256) {
    throw 'QtBase archive differs from the independently reviewed SDK'
}
$qt = Join-Path $root 'qt'
& 'C:\Program Files\7-Zip\7z.exe' x $archive ('-o' + $qt) -y
if ($LASTEXITCODE -ne 0) { throw 'Pinned QtBase extraction failed' }
foreach ($entry in $pin.files.psobject.Properties) {
    $path = Join-Path $qt $entry.Name
    if ((Get-Item -LiteralPath $path).Length -ne $entry.Value.bytes -or
        (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.Value.sha256) {
        throw ('Qt test runtime differs: ' + $entry.Name)
    }
}
$product = Get-Content -Raw -Encoding utf8 research/windows-preview-dependencies.json | ConvertFrom-Json
if ($pin.qtVersion -ne $product.qtVersion -or $pin.sourceSha256 -ne $product.qtSourceSha256) {
    throw 'Desktop test/product Qt source identity differs'
}
foreach ($name in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll')) {
    $test = $pin.files.psobject.Properties['bin/' + $name].Value
    $expected = $product.files.psobject.Properties[$name].Value
    if ($test.sha256 -ne $expected.sha256 -or $test.bytes -ne $expected.bytes) {
        throw ('Desktop test/product SDK identity differs: ' + $name)
    }
}
$pin | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 (Join-Path $root 'verified-runtime.json')
