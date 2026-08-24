[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$pio = 'B:\PlatformIO\penv\Scripts\pio.exe'
$candidateCPackages = 'B:\PlatformIO\isolated\adv-walkman-c\packages'
$artifacts = Join-Path $projectRoot 'artifacts'

if (-not (Test-Path -LiteralPath $pio)) {
    throw "PlatformIO CLI not found at $pio"
}

$previousCoreDir = $env:PLATFORMIO_CORE_DIR
$previousPackagesDir = $env:PLATFORMIO_PACKAGES_DIR

try {
    New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
    $env:PLATFORMIO_CORE_DIR = 'B:\PlatformIO'
    Remove-Item Env:PLATFORMIO_PACKAGES_DIR -ErrorAction SilentlyContinue

    & $pio run --project-dir $projectRoot -e bench-a -e bench-b
    if ($LASTEXITCODE -ne 0) {
        throw 'Candidate A/B build failed.'
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\bench-a\firmware.bin') `
        -Destination (Join-Path $artifacts 'ADV-Walkman-Bench-A.bin') -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\bench-b\firmware.bin') `
        -Destination (Join-Path $artifacts 'ADV-Walkman-Bench-B.bin') -Force

    $env:PLATFORMIO_PACKAGES_DIR = $candidateCPackages
    & $pio run --project-dir $projectRoot -e bench-c
    if ($LASTEXITCODE -ne 0) {
        throw 'Candidate C build failed.'
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot '.pio\build\bench-c\firmware.bin') `
        -Destination (Join-Path $artifacts 'ADV-Walkman-Bench-C.bin') -Force
}
finally {
    if ($null -eq $previousCoreDir) {
        Remove-Item Env:PLATFORMIO_CORE_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:PLATFORMIO_CORE_DIR = $previousCoreDir
    }

    if ($null -eq $previousPackagesDir) {
        Remove-Item Env:PLATFORMIO_PACKAGES_DIR -ErrorAction SilentlyContinue
    }
    else {
        $env:PLATFORMIO_PACKAGES_DIR = $previousPackagesDir
    }
}
