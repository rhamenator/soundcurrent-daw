$ErrorActionPreference = 'Stop'
$collector = Join-Path $PWD '.github/scripts/retain-owner-failure-projects.ps1'
$prefix = Join-Path ([IO.Path]::GetTempPath()) ('sc-manual-control-Ελληνικά-' + [guid]::NewGuid())
$fixtureBuild = Join-Path ([IO.Path]::GetTempPath()) ('sc-retention-test-' + [guid]::NewGuid())
$nativePrefix = Join-Path ([IO.Path]::GetTempPath()) ('sc-pw-owner-é-/' + [guid]::NewGuid())
try {
  New-Item -ItemType Directory -Path (Join-Path $prefix 'media/empty') -Force | Out-Null
  New-Item -ItemType Directory -Path $fixtureBuild | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $prefix 'media/audio.partial.rf64'),[byte[]](0,1,255,0,44))
  [IO.File]::WriteAllText((Join-Path $prefix 'project.json'),'{"unicode":"Ελληνικά 日本語"}')
  [IO.File]::WriteAllText((Join-Path $fixtureBuild 'manual-owner-3-stderr.log'),"preserved_failure_project=$prefix`n")
  & $collector -BuildDirectory $fixtureBuild
  $snapshot = Join-Path $fixtureBuild ('manual-owner-failure-projects/' + [IO.Path]::GetFileName($prefix))
  $manifest = Get-Content -LiteralPath (Join-Path $snapshot 'retention.json') -Raw | ConvertFrom-Json
  if ($manifest.originalFiles.Count -ne 2 -or $manifest.atomicProjectSnapshot -ne $false) { throw 'Snapshot manifest wrong' }
  foreach ($file in $manifest.originalFiles) {
    if ((Get-FileHash -LiteralPath (Join-Path $prefix $file.path)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $snapshot $file.path)).Hash) { throw 'Snapshot bytes changed' }
  }
  Remove-Item -LiteralPath (Join-Path $fixtureBuild 'manual-owner-failure-projects') -Recurse
  [IO.File]::WriteAllText((Join-Path $fixtureBuild 'manual-owner-3-stderr.log'),"preserved_failure_project=$fixtureBuild`n")
  $refused = $false; try { & $collector -BuildDirectory $fixtureBuild } catch { $refused = $true }
  if (-not $refused) { throw 'Foreign project path accepted' }
  [IO.File]::WriteAllText((Join-Path $fixtureBuild 'manual-owner-3-stderr.log'),"preserved_failure_project=$prefix`n")
  $linked = $false
  try { New-Item -ItemType SymbolicLink -Path (Join-Path $prefix 'escape') -Target $fixtureBuild | Out-Null; $linked = $true } catch { if (-not $IsWindows) { throw }; Write-Output 'Windows reparse fixture unavailable; no Windows reparse qualification claimed.' }
  if ($linked) {
    $refused = $false; try { & $collector -BuildDirectory $fixtureBuild } catch { $refused = $true }
    if (-not $refused) { throw 'Reparse project entry accepted' }
  }
  Remove-Item -LiteralPath (Join-Path $fixtureBuild 'manual-owner-3-stderr.log')
  New-Item -ItemType Directory -Path $nativePrefix -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $fixtureBuild 'native-owner-failure-evidence') | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $nativePrefix 'project.json'),[byte[]](17,0,255))
  [IO.File]::WriteAllText((Join-Path $fixtureBuild 'native-owner-failure-evidence/pw-owner-unit-failure.log'),"preserved_failure_project=$nativePrefix`n")
  & $collector -BuildDirectory $fixtureBuild
  $nativeSnapshot = Join-Path $fixtureBuild ('manual-owner-failure-projects/' + [IO.Path]::GetFileName($nativePrefix))
  if ((Get-FileHash -LiteralPath (Join-Path $nativePrefix 'project.json')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $nativeSnapshot 'project.json')).Hash) { throw 'Native owner marker snapshot changed bytes' }
  Write-Output ("Retention helper checks passed: exact Unicode/audio bytes, manifest, foreign-path refusal. Windows={0}; reparseFixture={1}" -f $IsWindows,$linked)
} finally {
  Remove-Item -LiteralPath $prefix -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $fixtureBuild -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $nativePrefix -Recurse -Force -ErrorAction SilentlyContinue
}
