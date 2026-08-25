[CmdletBinding()]
param(
    [string]$SdRoot,
    [ValidateSet(1, 2)]
    [int]$Gate = 1
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$pio = 'B:\PlatformIO\penv\Scripts\pio.exe'
$artifacts = Join-Path $projectRoot 'artifacts'
$environment = if ($Gate -eq 2) { 'player-dev-gate-b' } else { 'player-dev' }
$source = Join-Path $projectRoot ".pio\build\$environment\firmware.bin"
$artifact = Join-Path $artifacts 'ADV-Walkman-Dev.bin'

if (-not (Test-Path -LiteralPath $pio)) {
    throw "PlatformIO CLI not found at $pio"
}

$previousCoreDir = $env:PLATFORMIO_CORE_DIR
try {
    $env:PLATFORMIO_CORE_DIR = 'B:\PlatformIO'
    & $pio run --project-dir $projectRoot -e $environment
    if ($LASTEXITCODE -ne 0) {
        throw "$environment build failed."
    }

    New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $artifact -Force
    $sourceInfo = Get-Item -LiteralPath $source
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
    $artifactHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
    if ($sourceInfo.Length -ne (Get-Item -LiteralPath $artifact).Length -or
        $sourceHash -ne $artifactHash) {
        throw 'Artifact copy verification failed.'
    }

    Write-Output "PLAYER_BIN=$artifact"
    Write-Output "PLAYER_GATE=$Gate"
    Write-Output "PLAYER_SIZE=$($sourceInfo.Length)"
    Write-Output "PLAYER_SHA256=$sourceHash"

    if ($SdRoot) {
        $resolvedSdRoot = (Resolve-Path -LiteralPath $SdRoot).Path
        $firmwareDir = Join-Path $resolvedSdRoot 'firmware'
        New-Item -ItemType Directory -Path $firmwareDir -Force | Out-Null
        $sdBinary = Join-Path $firmwareDir 'ADV-Walkman-Dev.bin'
        Copy-Item -LiteralPath $artifact -Destination $sdBinary -Force
        $sdInfo = Get-Item -LiteralPath $sdBinary
        $sdHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sdBinary).Hash.ToLowerInvariant()
        if ($sdInfo.Length -ne $sourceInfo.Length -or $sdHash -ne $sourceHash) {
            throw 'SD firmware copy verification failed.'
        }
        Write-Output "SD_BIN=$sdBinary"
        Write-Output "SD_SIZE=$($sdInfo.Length)"
        Write-Output "SD_SHA256=$sdHash"
    }
}
finally {
    if ($null -eq $previousCoreDir) {
        Remove-Item Env:PLATFORMIO_CORE_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:PLATFORMIO_CORE_DIR = $previousCoreDir
    }
}
