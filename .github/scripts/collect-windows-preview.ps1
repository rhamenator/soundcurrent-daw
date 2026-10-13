# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$ExpectedSourceCommit)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = (Get-Location).Path
$build = Join-Path $root 'build-desktop-native'
$sdk = Join-Path $root 'native-ui-dependencies/qt'
$out = Join-Path $root '.cache/windows-preview-export'
if (Test-Path -LiteralPath $out) { throw 'Owned collection directory already exists' }
New-Item -ItemType Directory -Path $out | Out-Null
try {
    $stage = Join-Path $out 'deploy'
    $verify = Join-Path $out 'verification'
    $sources = Join-Path $out 'dependency-source'
    foreach ($p in @($stage,$verify,$sources)) { New-Item -ItemType Directory -Path $p | Out-Null }
    foreach ($name in @('soundcurrent-daw.exe','sc-import-inspect-worker.exe',
        'sc-approved-wave-probe.exe','sc-media-import-worker.exe','sc-stretch-render-worker.exe')) {
        Copy-Item -LiteralPath (Join-Path $build $name) -Destination $stage
    }
    Copy-Item -LiteralPath (Join-Path $root 'native-dependencies/sndfile-build/sndfile.dll') -Destination $stage
    $env:PATH = (Join-Path $sdk 'bin') + ';' + $env:PATH
    & (Join-Path $sdk 'bin/windeployqt.exe') --release --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types generic,networkinformation,tls,sqldrivers,printsupport --dir $stage (Join-Path $stage 'soundcurrent-daw.exe') *>&1 | Tee-Object -FilePath (Join-Path $out 'windeployqt.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native deployment failed' }
    foreach ($name in @('sc-stretch-artifact-tests.exe','sc-protected-warp-probe.exe','sc-native-panel-tests.exe')) {
        Copy-Item -LiteralPath (Join-Path $build $name) -Destination $verify
    }
    Copy-Item -LiteralPath (Join-Path $sdk 'bin/Qt6Test.dll') -Destination $verify
    New-Item -ItemType Directory -Path (Join-Path $verify 'platforms') | Out-Null
    Copy-Item -LiteralPath (Join-Path $sdk 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $verify 'platforms')
    Copy-Item -LiteralPath (Join-Path $sdk 'sbom/qtbase-6.12.0.spdx.json') -Destination $out
    Copy-Item -LiteralPath (Join-Path $build 'CMakeCache.txt') -Destination $out
    Copy-Item -LiteralPath (Join-Path $build 'Testing/Temporary/LastTest.log') -Destination $out
    Copy-Item -LiteralPath (Join-Path $root 'native-ui-dependencies/verified-runtime.json') -Destination $out
    $compilerLine = @(Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:(FILEPATH|STRING)=' })
    if ($compilerLine.Count -ne 1) { throw 'Ambiguous compiler identity' }
    $compiler = $compilerLine[0].Split('=',2)[1]
    $tool = Get-Item -LiteralPath $compiler
    [ordered]@{path=$tool.FullName;bytes=$tool.Length;sha256=(Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant();fileVersion=$tool.VersionInfo.FileVersion;productVersion=$tool.VersionInfo.ProductVersion;previousProductRuntime='14.44.35211.0';runtimeCompatibilityQualified=$false} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out 'compiler.json')
    $dumpbin = Join-Path $tool.Directory.FullName 'dumpbin.exe'
    foreach ($exe in Get-ChildItem -LiteralPath $stage -Filter '*.exe') {
        & $dumpbin /dependents $exe.FullName *>&1 | Set-Content -Encoding utf8 (Join-Path $out ($exe.Name + '.imports.txt'))
        if ($LASTEXITCODE -ne 0) { throw 'Native import audit failed' }
    }
    $pin = Get-Content -LiteralPath 'research/windows-desktop-test-dependencies.json' -Raw | ConvertFrom-Json
    if (-not $pin.sourceUrl.StartsWith('https://download.qt.io/official_releases/')) { throw 'Unreviewed Qt source location' }
    $qtSource = Join-Path $sources 'qtbase-everywhere-src-6.12.0.tar.xz'
    Invoke-WebRequest -Uri $pin.sourceUrl -OutFile $qtSource -TimeoutSec 120
    if ((Get-FileHash -LiteralPath $qtSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sourceSha256) { throw 'Qt source differs' }
    Copy-Item -LiteralPath (Join-Path $root 'native-dependencies/libsndfile-1.2.2.tar.gz') -Destination $sources
    & python tools/windows_preview_bundle.py --build $build --stage $stage --output $out --inputs (Join-Path $root '.cache/windows-preview-build-inputs.json') --expected-source $ExpectedSourceCommit
    if ($LASTEXITCODE -ne 0) { throw 'Native input collection failed' }
    [ordered]@{exitCode=0;installerPrepared=$false;nativeAudioActivated=$false} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out 'terminal.json')
} catch {
    [ordered]@{exitCode=1;error=$_.Exception.Message;installerPrepared=$false;nativeAudioActivated=$false} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $out 'terminal.json')
    throw
}
