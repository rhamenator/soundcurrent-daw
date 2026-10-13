$ErrorActionPreference = 'Stop'
$collector = Join-Path $PWD '.github/scripts/retain-owner-failure-projects.ps1'
$prefix = Join-Path ([IO.Path]::GetTempPath()) ('sc-manual-control-Ελληνικά-' + [guid]::NewGuid())
$fixtureBuild = Join-Path ([IO.Path]::GetTempPath()) ('sc-retention-test-' + [guid]::NewGuid())
$nativePrefix = Join-Path ([IO.Path]::GetTempPath()) ('sc-pw-owner-é-/' + [guid]::NewGuid())
$controllerPrefix = Join-Path ([IO.Path]::GetTempPath()) ('sc-native-owner-é-日本-' + [guid]::NewGuid())
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
  Remove-Item -LiteralPath (Join-Path $fixtureBuild 'manual-owner-failure-projects') -Recurse
  Remove-Item -LiteralPath (Join-Path $fixtureBuild 'native-owner-failure-evidence/pw-owner-unit-failure.log')
  New-Item -ItemType Directory -Path (Join-Path $controllerPrefix 'media/empty') -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $fixtureBuild 'Testing/Temporary') -Force | Out-Null
  [IO.File]::WriteAllBytes((Join-Path $controllerPrefix 'media/take.wav'),[byte[]](0,255,17,0,64,9))
  [IO.File]::WriteAllText((Join-Path $controllerPrefix 'project.json'),'{"unicode":"Ελληνικά 日本語","phase":"after-owner-shutdown"}')
  $lastTest = Join-Path $fixtureBuild 'Testing/Temporary/LastTest.log'
  [IO.File]::WriteAllText($lastTest,"CTest output`nnative_control_wait_expired={}`npreserved_failure_project=$controllerPrefix`npreserved_failure_project=$controllerPrefix`n")
  & $collector -BuildDirectory $fixtureBuild
  $controllerSnapshot = Join-Path $fixtureBuild ('manual-owner-failure-projects/' + [IO.Path]::GetFileName($controllerPrefix))
  $controllerManifest = Get-Content -LiteralPath (Join-Path $controllerSnapshot 'retention.json') -Raw | ConvertFrom-Json
  if ($controllerManifest.originalFiles.Count -ne 2 -or $controllerManifest.totalBytes -ne ((Get-Item (Join-Path $controllerPrefix 'project.json')).Length + 6)) { throw 'CTest controller snapshot extent wrong' }
  foreach ($file in $controllerManifest.originalFiles) {
    if ((Get-FileHash -LiteralPath (Join-Path $controllerPrefix $file.path)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $controllerSnapshot $file.path)).Hash) { throw 'CTest controller snapshot bytes changed' }
  }
  Remove-Item -LiteralPath (Join-Path $fixtureBuild 'manual-owner-failure-projects') -Recurse
  $foreignController = Join-Path $fixtureBuild ('sc-native-owner-é-日本-' + [guid]::NewGuid())
  [IO.File]::WriteAllText($lastTest,"preserved_failure_project=$foreignController`n")
  $refused = $false; try { & $collector -BuildDirectory $fixtureBuild } catch { $refused = $true }
  if (-not $refused) { throw 'Controller marker outside temp root accepted' }
  $three = @(1..3 | ForEach-Object { 'preserved_failure_project=' + (Join-Path ([IO.Path]::GetTempPath()) ('sc-native-owner-é-日本-' + [guid]::NewGuid())) })
  [IO.File]::WriteAllText($lastTest,($three -join "`n"))
  $countRefused = $false; try { & $collector -BuildDirectory $fixtureBuild } catch { $countRefused = $_.ToString().Contains('Too many owner failure projects') }
  if (-not $countRefused) { throw 'Unbounded native controller markers accepted' }
  $aliasDistinct = $false
  if ($IsWindows) {
    if (-not ('SoundCurrentRetentionAliasTest' -as [type])) {
      Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;
public static class SoundCurrentRetentionAliasTest {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern uint GetShortPathNameW(string path, StringBuilder result, uint capacity);
}
'@
    }
    $parent = [IO.Path]::GetDirectoryName($controllerPrefix)
    $short = [Text.StringBuilder]::new(32768)
    $count = [SoundCurrentRetentionAliasTest]::GetShortPathNameW($parent,$short,32768)
    if (-not $count -or $count -ge 32768) { throw 'Cannot inspect Windows temp-parent alias' }
    $aliasDistinct = -not [StringComparer]::OrdinalIgnoreCase.Equals($parent,$short.ToString())
    $alias = Join-Path $short.ToString() ([IO.Path]::GetFileName($controllerPrefix))
    [IO.File]::WriteAllText($lastTest,"preserved_failure_project=$alias`n")
    & $collector -BuildDirectory $fixtureBuild
    if ((Get-FileHash -LiteralPath (Join-Path $controllerPrefix 'media/take.wav')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $controllerSnapshot 'media/take.wav')).Hash) { throw 'Windows temp alias changed retained bytes' }
  }
  Write-Output ("Controller CTest retention checks passed: exact bytes, duplicate marker, parent/count refusal. Windows={0}; aliasDistinct={1}" -f $IsWindows,$aliasDistinct)
  Write-Output ("Retention helper checks passed: exact Unicode/audio bytes, manifest, foreign-path refusal. Windows={0}; reparseFixture={1}" -f $IsWindows,$linked)
} finally {
  Remove-Item -LiteralPath $prefix -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $fixtureBuild -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $nativePrefix -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $controllerPrefix -Recurse -Force -ErrorAction SilentlyContinue
}
