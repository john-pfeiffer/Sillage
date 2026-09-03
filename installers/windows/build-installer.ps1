# Builds the Windows installer for Sillage with Inno Setup.
#
#   installers/windows/build-installer.ps1 -Version 1.0.0 -BuildDir build\Sillage_artefacts\Release [-OutDir dist]
#
# Uses the Inno Setup compiler on PATH, or its default install location.
param(
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [string] $BuildDir,
    [string] $OutDir = "dist"
)

$ErrorActionPreference = "Stop"

$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if ($iscc) {
    $isccPath = $iscc.Source
} else {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $isccPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $isccPath) {
        throw "Inno Setup 6 (ISCC.exe) not found; install it or add it to PATH."
    }
}

$buildDirFull = (Resolve-Path $BuildDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outDirFull = (Resolve-Path $OutDir).Path
$script = Join-Path $PSScriptRoot "Sillage.iss"

& $isccPath "/DAppVersion=$Version" "/DBuildDir=$buildDirFull" "/DOutputDir=$outDirFull" $script
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

Write-Host "built $outDirFull\Sillage-$Version-windows.exe"
