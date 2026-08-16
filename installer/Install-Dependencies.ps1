#Requires -RunAsAdministrator
# Downloads and installs the extra bits Predator Utility needs:
#   - PawnIO kernel driver (https://github.com/namazso/PawnIO.Setup) for CPU watts
#   - VC++ 2015-2022 x64 runtime if it's missing
#
# This script is open source and is also run by the Inno Setup installer.

$ErrorActionPreference = "Stop"
$tmp = Join-Path $env:TEMP "PredatorUtility-deps"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

function Download-File([string]$Url, [string]$OutFile) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
}

function Have-PawnIO {
    return (Test-Path "C:\Program Files\PawnIO\PawnIOLib.dll") -or
           (Test-Path "C:\Program Files\PawnIO\PawnIO.sys")
}

if (-not (Have-PawnIO)) {
    $pawn = Join-Path $tmp "PawnIO_setup.exe"
    Download-File "https://github.com/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe" $pawn
    Write-Host "Installing PawnIO (needs admin, may ask for a reboot later)"
    $ok = $false
    foreach ($args in @(
            @("-install", "-silent"),
            @("/VERYSILENT", "/NORESTART"),
            @("-install")
        )) {
        $p = Start-Process -FilePath $pawn -ArgumentList $args -Wait -PassThru
        if ($p.ExitCode -eq 0 -or $p.ExitCode -eq 3010) {
            $ok = $true
            break
        }
    }
    if (-not $ok -and -not (Have-PawnIO)) {
        Write-Host "PawnIO silent install didn't take. Running the official installer so you can click through."
        Start-Process -FilePath $pawn -Wait
    }
    if (Have-PawnIO) {
        Write-Host "PawnIO is installed."
    } else {
        Write-Host "PawnIO didn't install. CPU watts won't work until you install it from https://pawnio.eu/"
    }
} else {
    Write-Host "PawnIO already present."
}

$sys32 = Join-Path $env:SystemRoot "System32\vcruntime140.dll"
if (-not (Test-Path $sys32)) {
    $vc = Join-Path $tmp "vc_redist.x64.exe"
    Download-File "https://aka.ms/vs/17/release/vc_redist.x64.exe" $vc
    Write-Host "Installing Visual C++ runtime"
    $p = Start-Process -FilePath $vc -ArgumentList "/install", "/quiet", "/norestart" -Wait -PassThru
    Write-Host ("VC++ exit " + $p.ExitCode)
} else {
    Write-Host "VC++ runtime already present."
}

Write-Host "Dependencies done."
exit 0
