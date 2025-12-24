# test_windows_build.ps1
# This script attempts to build YUView following the documented steps.
# Prerequisites: qmake and jom/nmake must be in PATH. Run in Developer PowerShell.

Write-Host "Starting YUView Build Test..." -ForegroundColor Cyan

# Add specific Qt path for this environment
$qtPath = "C:\work\misc\yuviewer\YUViewQt\Qt\bin"
if (Test-Path $qtPath) {
    $env:PATH = "$qtPath;$env:PATH"
    Write-Host "Added $qtPath to PATH" -ForegroundColor Gray
}

# Check for qmake
if (-not (Get-Command "qmake" -ErrorAction SilentlyContinue)) {
    Write-Error "qmake not found in PATH. Please add Qt bin directory to PATH."
    exit 1
}

# Check for jom or nmake
$makeCmd = "nmake"
$makeArgs = @()
if (Get-Command "jom" -ErrorAction SilentlyContinue) {
    $makeCmd = "jom"
    $makeArgs = @("-j8")
}
Write-Host "Using build tool: $makeCmd $makeArgs" -ForegroundColor Green

# Create build directory
if (-not (Test-Path "build_test")) {
    New-Item -ItemType Directory -Path "build_test" | Out-Null
}
Push-Location "build_test"

# Configure
Write-Host "Running qmake..." -ForegroundColor Cyan
qmake CONFIG+=UNITTESTS ..
if ($LASTEXITCODE -ne 0) {
    Write-Error "qmake failed."
    Pop-Location
    exit 1
}

# Build
Write-Host "Running build..." -ForegroundColor Cyan
& $makeCmd $makeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    Pop-Location
    exit 1
}
Pop-Location

# Run Tests
Write-Host "Running Unit Tests..." -ForegroundColor Cyan

# Add YUViewLib to PATH so the test executable can find the DLL
$libPath = "$PWD\build_test\YUViewLib"
if (Test-Path $libPath) {
    $env:PATH = "$libPath;$env:PATH"
}

$testExe = ".\build_test\YUViewUnitTest\YUViewUnitTest.exe"
if (-not (Test-Path $testExe)) {
    $testExe = ".\build_test\YUViewUnitTest\release\YUViewUnitTest.exe"
}
if (-not (Test-Path $testExe)) {
    $testExe = ".\build_test\YUViewUnitTest\debug\YUViewUnitTest.exe"
}

if (Test-Path $testExe) {
    & $testExe
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Unit tests failed."
        exit 1
    }
    Write-Host "Unit tests passed!" -ForegroundColor Green
} else {
    Write-Warning "Unit test executable not found. Build might have failed or output path differs."
}

Write-Host "Build and Test Complete." -ForegroundColor Green

# -----------------------------------------------------------------------------
# Package a simple release folder and accompanying zip archive for distribution
# -----------------------------------------------------------------------------

$releaseRoot = Join-Path $PWD "build_test"
$releaseFolderName = "YUViewSimpleRelease"
$releaseDir = Join-Path $releaseRoot $releaseFolderName
$zipPath = Join-Path $releaseRoot "$releaseFolderName.zip"

# Locate the freshly built executable
$exeCandidates = @(
    "build_test\\YUViewApp\\release\\YUView-ML.exe",
    "build_test\\YUViewApp\\YUView-ML.exe",
    "build_test\\YUViewApp\\debug\\YUView-ML.exe"
)
$exePath = $null
foreach ($candidate in $exeCandidates) {
    $fullCandidate = Join-Path $PWD $candidate
    if (Test-Path $fullCandidate) {
        $exePath = $fullCandidate
        break
    }
}

if (-not $exePath) {
    Write-Warning "YUView executable not found. Skipping release packaging."
    exit 0
}

# Clean previous release outputs
if (Test-Path $releaseDir) {
    Remove-Item -Path $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseDir | Out-Null
if (Test-Path $zipPath) {
    Remove-Item -Path $zipPath -Force
}

# Ensure windeployqt is available to gather Qt runtime dependencies
$windeploy = Get-Command "windeployqt" -ErrorAction SilentlyContinue
if (-not $windeploy) {
    Write-Warning "windeployqt not found in PATH. Cannot create standalone release."
    exit 0
}

Write-Host "Creating portable release folder using windeployqt..." -ForegroundColor Cyan
& $windeploy --dir $releaseDir $exePath
if ($LASTEXITCODE -ne 0) {
    Write-Warning "windeployqt failed. Release folder may be incomplete."
    exit 0
}

# Copy the executable itself into the release folder
$exeName = Split-Path $exePath -Leaf
Copy-Item -Path $exePath -Destination (Join-Path $releaseDir $exeName) -Force

# Copy optional auxiliary DLLs (e.g., libde265) if present beside the repo root
$auxLibs = @("libde265.dll")
foreach ($lib in $auxLibs) {
    $libPath = Join-Path $PWD $lib
    if (Test-Path $libPath) {
        Copy-Item -Path $libPath -Destination $releaseDir -Force
    }
}

Write-Host "Creating zip archive $zipPath..." -ForegroundColor Cyan
Compress-Archive -Path (Join-Path $releaseDir '*') -DestinationPath $zipPath -Force
Write-Host "Release folder: $releaseDir" -ForegroundColor Green
Write-Host "Release archive: $zipPath" -ForegroundColor Green
