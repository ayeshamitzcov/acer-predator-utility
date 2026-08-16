# Build Release binaries and compile the Inno Setup installer.
# Needs: Visual Studio + CMake, and Inno Setup 6 (https://jrsoftware.org/isinfo.php)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$gen = "Visual Studio 17 2022"
if (Test-Path $vswhere) {
    $ver = & $vswhere -latest -property catalog_productLineVersion
    if ($ver -eq "18") { $gen = "Visual Studio 18 2026" }
}

if (-not (Test-Path "$root\build\CMakeCache.txt")) {
    cmake -S $root -B "$root\build" -G $gen -A x64
}
cmake --build "$root\build" --config Release --target PredatorUtility
cmake --build "$root\build" --config Release --target predator-probe

$exe = "$root\build\Release\PredatorUtility.exe"
if (-not (Test-Path $exe)) {
    throw "Release build missing: $exe"
}

$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $iscc) {
    Write-Host "Inno Setup 6 not found. Installing via winget..."
    winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements
    $iscc = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $iscc) {
    throw "ISCC.exe still missing. Install Inno Setup 6 and re-run."
}

New-Item -ItemType Directory -Force -Path "$root\dist" | Out-Null
& $iscc "$root\installer\PredatorUtility.iss"
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed"
}
Get-ChildItem "$root\dist\PredatorUtility-Setup-*.exe" | ForEach-Object { Write-Host "Built $($_.FullName)" }
