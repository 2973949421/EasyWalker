param([string]$SdRoot = 'D:\')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$package = Join-Path $repo 'test-data\local\p3-media\package'
$private = Join-Path $repo 'test-data\local\p3-media\renderfix-084'
$root = (Resolve-Path -LiteralPath $SdRoot).Path.TrimEnd('\') + '\'
if ($root -notmatch '^[A-Z]:\\$' -or $root -in @('B:\','C:\')) { throw 'Expected removable SD, not workspace/system drive' }
foreach ($required in @('firmware\ADV-Walkman-P3ABC-Gate.bin','Music\AveMujica','ADVWalkman\state','ADVWalkman\logs')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $required))) { throw "Absent/wrong SD: $required" }
}
$builds = Get-Content -LiteralPath (Join-Path $repo 'artifacts\p3-0.8.4-builds.json') -Raw | ConvertFrom-Json
if ($builds.Count -ne 3) { throw 'Expected Dev/P3ABC/P3A final builds' }
$gate = $builds | Where-Object environment -eq 'player-p3abc-gate'
if (-not $gate -or $gate.bytes -gt 0x140000 -or $gate.memory.media_plus_events_bytes -gt 49152) { throw 'Invalid build budget' }
$items = @([pscustomobject]@{relative='firmware\ADV-Walkman-P3ABC-Gate.bin';source=(Join-Path $repo 'artifacts\ADV-Walkman-P3ABC-Gate.bin');hash=$gate.sha256})
$fontReport = Get-Content -LiteralPath (Join-Path $private 'fonts.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($face in @('library-cjk-12','library-cjk-18','library-latin-14','library-latin-22')) {
    $font = $fontReport | Where-Object name -eq $face
    if (-not $font -or $font.missing.Count -ne 0) { throw "Incomplete font: $face" }
    foreach ($ext in @('vlw','idx','idx2')) {
        $rel = "ADVWalkman\fonts\$face.$ext"
        $items += [pscustomobject]@{relative=$rel;source=(Join-Path $package $rel);hash=$font.files.($ext.Insert(0,'.'))}
    }
}
if ($items.Count -ne 13) { throw 'Unexpected deployment scope' }
foreach ($item in $items) {
    $target = [IO.Path]::GetFullPath((Join-Path $root $item.relative))
    if (-not $target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Unexpected target: $target" }
    if ((Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash -ne $item.hash) { throw "Prepared file changed: $($item.relative)" }
}
# Only the exact replacements are backed up. No music/covers/cache cleanup.
$recovery = Join-Path $private ('sd-recovery-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$protected = @{}
foreach ($folder in @('ADVWalkman\state','ADVWalkman\logs')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse) {
        $protected[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$report = @()
foreach ($item in $items) {
    $target = Join-Path $root $item.relative
    $saved = Join-Path $recovery $item.relative
    New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $saved
    Copy-Item -LiteralPath $item.source -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $item.hash) { throw "Copy verification failed: $target" }
    $report += [pscustomobject]@{path=$item.relative;sha256=$item.hash;bytes=(Get-Item -LiteralPath $target).Length}
}
foreach ($path in $protected.Keys) {
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $protected[$path]) { throw "State/log unexpectedly changed: $path" }
}
$receipt = [pscustomobject]@{version='0.8.4-p3d.renderfix';sd=$root;files=$report;protected_files=$protected.Count;recovery=$recovery;completed=(Get-Date -Format o)}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $private 'sd-delivery.json') -Encoding UTF8
Write-Output "VERIFIED 13 exact replacements; $($protected.Count) state/log files unchanged; recovery=$recovery"
