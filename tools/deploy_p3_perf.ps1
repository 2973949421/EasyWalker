[CmdletBinding()]
param([string]$SdRoot='D:\', [switch]$Apply)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$packageRoot=Join-Path $projectRoot 'test-data\local\p3-media\package'
$resolvedSd=(Resolve-Path -LiteralPath $SdRoot).Path.TrimEnd('\')+'\'
if($resolvedSd -ne 'D:\'){throw 'This reviewed delivery is explicitly scoped to D:\ only.'}
if(!(Test-Path -LiteralPath (Join-Path $resolvedSd 'ADVWalkman\state'))){throw 'Expected Walkman SD not mounted.'}
$manifest=Get-Content -LiteralPath (Join-Path $projectRoot 'artifacts\p3-0.8.2-builds.json') -Raw | ConvertFrom-Json
if(@($manifest).Count -ne 6){throw 'Six build records required.'}
$deliver=@{}
$deliver['firmware\ADV-Walkman-P3ABC-Gate.bin']=Join-Path $projectRoot 'artifacts\ADV-Walkman-P3ABC-Gate.bin'
foreach($name in @('cjk-12','cjk-14','cjk-16','cjk-18','latin-10','latin-12','latin-14')){
    $relative="ADVWalkman\fonts\$name.idx2"; $deliver[$relative]=Join-Path $packageRoot $relative
}
foreach($name in @('blackbirthday','choirschoir','ether','masuerade','octagramdance','sophie','symbol1','symbol3','twomoons','crucifix-x')){
    $relative="Lyrics\AveMujica\$name.zh-Hans.lrc"; $deliver[$relative]=Join-Path $packageRoot $relative
}
foreach($relative in @('Music\AveMujica\crucifix-x.mp3','Lyrics\AveMujica\crucifix-x.lrc','ADVWalkman\covers\AveMujica\crucifix-x.cover.adv','CoverSource\AveMujica\crucifix-x.jpg','ADVWalkman\library-covers\folders\AveMujica\cover.adv')){
    $deliver[$relative]=Join-Path $packageRoot $relative
}
$retire=@{
    'Music\ADVWalkmanBenchmark'=@('benchmark.mp3')
    'Lyrics\ADVWalkmanBenchmark'=@('benchmark.lrc','benchmark.zh-Hans.lrc')
    'ADVWalkman\covers\ADVWalkmanBenchmark'=@('benchmark.cover.adv')
    'CoverSource\ADVWalkmanBenchmark'=@('benchmark.jpg')
}
# Resolve and inspect all destructive targets BEFORE any copy/removal. Never
# use recursive removal, shell expansion, or an unvalidated target root.
$removeFiles=@();$removeDirs=@()
foreach($relative in $retire.Keys){
    $directory=Join-Path $resolvedSd $relative
    if(!(Test-Path -LiteralPath $directory)){continue}
    $absolute=(Resolve-Path -LiteralPath $directory).Path
    if(!$absolute.StartsWith($resolvedSd,[StringComparison]::OrdinalIgnoreCase)){throw 'Out-of-scope removal target.'}
    $children=@(Get-ChildItem -LiteralPath $absolute -Force)
    foreach($child in $children){
        if($child.PSIsContainer -or $child.Name -notin $retire[$relative]){throw "Unknown benchmark item; stopped: $($child.FullName)"}
        $backup=if($child.Name -eq 'benchmark.mp3'){Join-Path $projectRoot 'test-data\local\p3-media\perf-baseline\benchmark.mp3'}else{Join-Path $packageRoot "$relative\$($child.Name)"}
        if(!(Test-Path -LiteralPath $backup) -or (Get-FileHash -LiteralPath $backup).Hash -ne (Get-FileHash -LiteralPath $child.FullName).Hash){throw "PC recovery copy differs: $($child.FullName)"}
        $removeFiles+=$child.FullName
    }
    $removeDirs+=$absolute
}
$protected=@{}
foreach($directory in @('ADVWalkman\state','ADVWalkman\logs')){
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $resolvedSd $directory) -File -Force){
        $protected[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName).Hash
        if($directory -eq 'ADVWalkman\state' -and $file.Name -match '^(queue|session)-'){
            if([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName)).Contains('ADVWalkmanBenchmark')){throw 'Saved state still refers to benchmark.'}
        }
    }
}
foreach($relative in $deliver.Keys){if(!(Test-Path -LiteralPath $deliver[$relative])){throw "Missing delivery source: $relative"}}
$firmware=$manifest|Where-Object environment -eq 'player-p3abc-gate'
if((Get-FileHash -LiteralPath $deliver['firmware\ADV-Walkman-P3ABC-Gate.bin']).Hash.ToLowerInvariant() -ne $firmware.sha256){throw 'Firmware manifest mismatch.'}
Write-Output "DELIVERY_FILES=$($deliver.Count) RETIRE_FILES=$($removeFiles.Count)"
if(!$Apply){Write-Output 'READ_ONLY_PREFLIGHT_OK';return}
$copied=@()
foreach($relative in ($deliver.Keys|Sort-Object)){
    $destination=[IO.Path]::GetFullPath((Join-Path $resolvedSd $relative))
    if(!$destination.StartsWith($resolvedSd,[StringComparison]::OrdinalIgnoreCase)){throw 'Out-of-scope copy target.'}
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $deliver[$relative] -Destination $destination -Force
    $hash=(Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant()
    if($hash -ne (Get-FileHash -LiteralPath $deliver[$relative]).Hash.ToLowerInvariant()){throw "Copy mismatch: $relative"}
    $copied+=@{path=$relative;sha256=$hash;bytes=(Get-Item -LiteralPath $destination).Length}
}
# New resources are fully verified now. Only the exact reviewed old files are retired.
foreach($file in $removeFiles){Remove-Item -LiteralPath $file}
foreach($directory in $removeDirs){if(@(Get-ChildItem -LiteralPath $directory -Force).Count -eq 0){Remove-Item -LiteralPath $directory}}
foreach($file in $protected.Keys){if((Get-FileHash -LiteralPath $file).Hash -ne $protected[$file]){throw "Protected state/log changed: $file"}}
$record=@{version='0.8.2-p3d.perf';copied=$copied;retired=$removeFiles;protected_files=$protected.Count;protected_unchanged=$true}
$record|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $projectRoot 'artifacts\p3-0.8.2-sd-delivery.json') -Encoding utf8
Write-Output 'SD_DELIVERY_VERIFIED: 11 AveMujica songs; original states/logs unchanged; PC benchmark recovery retained.'
