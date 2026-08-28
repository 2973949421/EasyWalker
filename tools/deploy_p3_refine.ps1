param([string]$SdRoot = 'D:\')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$package = Join-Path $repo 'test-data\local\p3-media\package'
$private = Join-Path $repo 'test-data\local\p3-media\refine-083'
$recovery = Join-Path $private 'sd-recovery'
$root = (Resolve-Path -LiteralPath $SdRoot).Path.TrimEnd('\') + '\'
if ($root -notmatch '^[A-Z]:\\$' -or $root -in @('B:\','C:\')) { throw 'Expected the removable SD root, not a workspace/system drive' }
foreach ($required in @('firmware\ADV-Walkman-P3ABC-Gate.bin','Music\AveMujica','ADVWalkman\state','ADVWalkman\logs')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $required))) { throw "Wrong or absent SD: $required" }
}
$builds = Get-Content -LiteralPath (Join-Path $repo 'artifacts\p3-0.8.3-builds.json') -Raw | ConvertFrom-Json
if ($builds.Count -ne 4) { throw 'Expected four verified build environments' }
$gate = $builds | Where-Object environment -eq 'player-p3abc-gate'
if (-not $gate -or $gate.bytes -gt 0x140000 -or $gate.memory.media_plus_events_bytes -gt 49152) { throw 'Missing or over-budget build' }
$items = @([pscustomobject]@{relative='firmware\ADV-Walkman-P3ABC-Gate.bin';source=(Join-Path $repo 'artifacts\ADV-Walkman-P3ABC-Gate.bin')})
$titles = Get-Content -LiteralPath (Join-Path $private 'titles.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($titles.Count -ne 11 -or @($titles | Where-Object changed).Count -ne 4) { throw 'Unexpected title manifest' }
foreach ($name in @('ankokutengoku','blackbirthday','twomoons','octagramdance')) {
    $rel = "Music\AveMujica\$name.mp3"
    $source = Join-Path $package $rel
    $old = Join-Path (Join-Path $private 'recovery') $rel
    $target = Join-Path $root $rel
    $observed = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($observed -ne (Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash -and $observed -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
        throw "SD music changed since preparation: $rel"
    }
    $items += [pscustomobject]@{relative=$rel;source=$source}
}
foreach ($face in @('library-cjk-12','library-cjk-18','library-latin-14','library-latin-22')) {
    foreach ($ext in @('vlw','idx','idx2')) {
        $rel = "ADVWalkman\fonts\$face.$ext"
        $items += [pscustomobject]@{relative=$rel;source=(Join-Path $package $rel)}
    }
}
if ($items.Count -ne 17) { throw 'Unexpected deployment scope' }
foreach ($item in $items) {
    if (-not (Test-Path -LiteralPath $item.source -PathType Leaf)) { throw "Missing $($item.source)" }
    $target = [IO.Path]::GetFullPath((Join-Path $root $item.relative))
    if (-not $target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes SD' }
}
if ((Get-FileHash -LiteralPath $items[0].source -Algorithm SHA256).Hash -ne $gate.sha256) { throw 'BIN differs from final build report' }

# Preserve the reported failure journal and every exact replacement once.
# No directory deletion, renaming, broad synchronization, or state rewriting.
$protected = @{}
foreach ($folder in @('ADVWalkman\state','ADVWalkman\logs')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse) {
        $protected[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$journal = Join-Path $root 'ADVWalkman\logs\p3-free-last.txt'
$journalCopy = Join-Path $private 'pre-refine-p3-free-last.txt'
if ((Test-Path -LiteralPath $journal) -and -not (Test-Path -LiteralPath $journalCopy)) { Copy-Item -LiteralPath $journal -Destination $journalCopy }
$report = @()
foreach ($item in $items) {
    $target = Join-Path $root $item.relative
    if (Test-Path -LiteralPath $target) {
        $saved = Join-Path $recovery $item.relative
        if (-not (Test-Path -LiteralPath $saved)) {
            New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $saved
        }
    }
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $item.source -Destination $target -Force
    $expected = (Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) { throw "Copy verification failed: $target" }
    $report += [pscustomobject]@{path=$item.relative;sha256=$expected;bytes=(Get-Item -LiteralPath $target).Length}
}
foreach ($path in $protected.Keys) {
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $protected[$path]) { throw "Unexpected state/log change: $path" }
}
$receipt = [pscustomobject]@{version='0.8.3-p3d.refine';sd=$root;files=$report;protected_files=$protected.Count;recovery=$recovery;completed=(Get-Date -Format o)}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $private 'sd-delivery.json') -Encoding UTF8
Write-Output "VERIFIED 17 exact files; $($protected.Count) state/log files unchanged; recovery=$recovery"
