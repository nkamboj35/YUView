# build_installer.ps1
# Builds the YUView-ML Windows installer using Inno Setup.
#
# Prerequisites:
#   1. Build YUView-ML first (run test_windows_build.ps1 from Developer PowerShell)
#   2. Install Inno Setup 6 from https://jrsoftware.org/isinfo.php
#
# Usage:
#   .\installer\build_installer.ps1 [-ReleaseDir <path>]
#
# If -ReleaseDir is not specified, defaults to build_test\YUViewSimpleRelease
# relative to the repository root.

param(
    [string]$ReleaseDir
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot  = Split-Path -Parent $scriptDir

Write-Host "=== YUView-ML Installer Builder ===" -ForegroundColor Cyan

# --- Locate the release folder ---
if (-not $ReleaseDir) {
    $ReleaseDir = Join-Path $repoRoot "build_test\YUViewSimpleRelease"
}
if (-not (Test-Path $ReleaseDir)) {
    Write-Error "Release folder not found: $ReleaseDir`nBuild YUView-ML first using test_windows_build.ps1"
    exit 1
}
$ReleaseDir = (Resolve-Path $ReleaseDir).Path
Write-Host "Release folder: $ReleaseDir" -ForegroundColor Gray

# --- Verify YUView-ML.exe exists ---
$exePath = Join-Path $ReleaseDir "YUView-ML.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "YUView-ML.exe not found in release folder: $ReleaseDir"
    exit 1
}

# --- Extract version ---
$appVersion = "0.0.0"
try {
    $gitDesc = git -C $repoRoot describe --tags --always 2>$null
    if ($LASTEXITCODE -eq 0 -and $gitDesc) {
        $appVersion = $gitDesc -replace '^v', ''
    }
} catch {}
Write-Host "App version: $appVersion" -ForegroundColor Gray

# --- Locate Inno Setup compiler ---
$isccPaths = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)

$isccExe = $null
$isccFromPath = Get-Command "ISCC" -ErrorAction SilentlyContinue
if ($isccFromPath) {
    $isccExe = $isccFromPath.Source
} else {
    foreach ($p in $isccPaths) {
        if (Test-Path $p) {
            $isccExe = $p
            break
        }
    }
}

if (-not $isccExe) {
    Write-Error @"
Inno Setup 6 not found. Please install it from:
  https://jrsoftware.org/isdl.php
Then either add it to PATH or install to the default location.
"@
    exit 1
}
Write-Host "Inno Setup compiler: $isccExe" -ForegroundColor Gray

# --- Verify required assets ---
$issFile = Join-Path $scriptDir "YUView-ML.iss"
if (-not (Test-Path $issFile)) {
    Write-Error "Inno Setup script not found: $issFile"
    exit 1
}

$licenseFile = Join-Path $repoRoot "LICENSE.GPL3"
if (-not (Test-Path $licenseFile)) {
    Write-Error "License file not found: $licenseFile"
    exit 1
}

$iconFile = Join-Path $repoRoot "YUViewApp\images\YUView.ico"
if (-not (Test-Path $iconFile)) {
    Write-Error "Icon file not found: $iconFile"
    exit 1
}

# --- Build the installer ---
Write-Host "`nBuilding installer..." -ForegroundColor Cyan

$outputDir = Join-Path $scriptDir "Output"
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

& $isccExe /Qp `
    /DSourceDir="$ReleaseDir" `
    /DAppVersion="$appVersion" `
    "$issFile"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Inno Setup compilation failed (exit code $LASTEXITCODE)."
    exit 1
}

$installerPath = Join-Path $outputDir "YUView-ML-Setup.exe"
if (Test-Path $installerPath) {
    $size = [math]::Round((Get-Item $installerPath).Length / 1MB, 1)
    Write-Host "`nInstaller created successfully!" -ForegroundColor Green
    Write-Host "  Path: $installerPath" -ForegroundColor Green
    Write-Host "  Size: ${size} MB" -ForegroundColor Green
} else {
    Write-Error "Installer output not found at expected path: $installerPath"
    exit 1
}
