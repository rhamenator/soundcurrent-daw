param([Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference = "Stop"
$build = [IO.Path]::GetFullPath($BuildDirectory)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$destination = Join-Path $build "manual-owner-failure-projects"
if ((Test-Path -LiteralPath $destination) -and ([IO.File]::GetAttributes($destination) -band [IO.FileAttributes]::ReparsePoint)) {
    throw "Reparse owner retention destination refused"
}
$sources = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if (-not (Test-Path -LiteralPath $build)) { exit 0 }
$logs = @(Get-ChildItem -LiteralPath $build -Filter "manual-owner-*-stderr.log" -File)
$nativeEvidence = Join-Path $build "native-owner-failure-evidence"
if (Test-Path -LiteralPath $nativeEvidence) { $logs += @(Get-ChildItem -LiteralPath $nativeEvidence -Filter "pw-owner-*-failure.log" -File) }
foreach ($log in $logs) {
    if ($log.Length -gt 1048576) { throw "Owner diagnostic log exceeds retention bound" }
    # Eager bounded read closes the handle before any path-policy refusal.
    # A lazy IEnumerable can retain a Windows file handle when foreach throws.
    foreach ($line in [IO.File]::ReadAllLines($log.FullName)) {
        if ($line -notmatch '^preserved_failure_project=(.+)$') { continue }
        $source = [IO.Path]::GetFullPath($Matches[1])
        $parent = [IO.Path]::GetDirectoryName($source).TrimEnd([IO.Path]::DirectorySeparatorChar)
        $name = [IO.Path]::GetFileName($source)
        $manual = [StringComparer]::OrdinalIgnoreCase.Equals($parent,$tempRoot) -and $name -match '^sc-manual-control-.+[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
        $nativeParent = Join-Path $tempRoot 'sc-pw-owner-é-'
        $native = [StringComparer]::OrdinalIgnoreCase.Equals($parent,$nativeParent) -and $name -match '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
        if (-not ($manual -or $native)) {
            throw "Owner failure path is outside the generated temp-project policy"
        }
        if ([IO.File]::GetAttributes($parent) -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse owner project parent refused" }
        [void]$sources.Add($source)
        if ($sources.Count -gt 3) { throw "Too many owner failure projects" }
    }
}
foreach ($source in $sources) {
    if (-not ([IO.File]::GetAttributes($source) -band [IO.FileAttributes]::Directory)) { throw "Owner project is not a directory" }
    $queue = [Collections.Generic.Queue[string]]::new(); $queue.Enqueue($source)
    $files = [Collections.Generic.List[object]]::new(); $bytes = [long]0; $entries = 0
    while ($queue.Count) {
        $folder = $queue.Dequeue()
        if ([IO.File]::GetAttributes($folder) -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse owner project refused" }
        foreach ($item in Get-ChildItem -LiteralPath $folder -Force) {
            if (++$entries -gt 512) { throw "Owner failure project entry bound exceeded" }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse owner entry refused" }
            if ($item.PSIsContainer) { $queue.Enqueue($item.FullName); continue }
            if ($item.Length -gt 536870912 - $bytes) { throw "Owner failure project byte bound exceeded" }
            $bytes += $item.Length
            $files.Add($item)
        }
    }
    $target = Join-Path $destination ([IO.Path]::GetFileName($source))
    if (Test-Path -LiteralPath $target) { throw "Owner failure snapshot already exists" }
    $manifest = [Collections.Generic.List[object]]::new()
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($source,$file.FullName)
        if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) {
            throw "Owner retention relative path escaped its project"
        }
        $copy = Join-Path $target $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($copy)) -Force | Out-Null
        $before = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        [IO.File]::Copy($file.FullName,$copy,$false)
        $after = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $copied = (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash
        if ($before -ne $after -or $before -ne $copied -or (Get-Item -LiteralPath $copy).Length -ne $file.Length) {
            throw "Owner failure bytes changed during retention"
        }
        $manifest.Add(@{path=$relative; bytes=$file.Length; sha256=$copied.ToLowerInvariant()})
    }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    @{schema=1; source=$source; totalBytes=$bytes; originalFiles=$manifest.ToArray(); atomicProjectSnapshot=$false} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $target "retention.json") -Encoding utf8
    Write-Output "Retained owned diagnostic failure project: $target ($bytes bytes)"
}
