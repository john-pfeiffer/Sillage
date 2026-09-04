# Builds the Windows installer for Sillage with Inno Setup, signing it when a
# certificate is present.
#
#   installers/windows/build-installer.ps1 -Version 1.0.0 -BuildDir build\Sillage_artefacts\Release [-OutDir dist]
#
# Uses the Inno Setup compiler on PATH, or its default install location.
#
# Optional Authenticode signing, driven by the environment:
#   SILLAGE_WIN_CERT_PFX       base64 of the code-signing certificate (.pfx)
#   SILLAGE_WIN_CERT_PASSWORD  its password
# When set, the VST3 DLL and the standalone exe are signed before packaging
# and the installer itself after, all SHA-256 with an RFC 3161 timestamp.
# Without it the result is unsigned and Windows SmartScreen shows its
# "More info > Run anyway" prompt on first launch.
param(
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [string] $BuildDir,
    [string] $OutDir = "dist"
)

$ErrorActionPreference = "Stop"

# ---- Inno Setup ---------------------------------------------------------------
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
$installer = Join-Path $outDirFull "Sillage-$Version-windows.exe"

# ---- Signing ------------------------------------------------------------------
$signtool = $null
$pfxPath = $null

if ($env:SILLAGE_WIN_CERT_PFX -and $env:SILLAGE_WIN_CERT_PASSWORD) {
    $kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $signtool = Get-ChildItem -Path $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $signtool) {
        throw "signtool.exe not found in the Windows 10 SDK; cannot sign."
    }

    $pfxPath = Join-Path ([System.IO.Path]::GetTempPath()) "sillage-signing-$([System.Guid]::NewGuid()).pfx"
    [System.IO.File]::WriteAllBytes($pfxPath, [System.Convert]::FromBase64String($env:SILLAGE_WIN_CERT_PFX))
    Write-Host "signing with $signtool"
} else {
    Write-Host "SILLAGE_WIN_CERT_PFX not set: installer will be unsigned"
}

function Invoke-Sign([string] $path) {
    if (-not $signtool) { return }
    if (-not (Test-Path $path)) { throw "cannot sign missing file: $path" }
    & $signtool sign /f $pfxPath /p $env:SILLAGE_WIN_CERT_PASSWORD /fd SHA256 `
        /tr http://timestamp.digicert.com /td SHA256 $path
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $path (exit $LASTEXITCODE)" }
    & $signtool verify /pa /v $path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "signature verification failed on $path" }
    Write-Host "signed $path"
}

try {
    Invoke-Sign (Join-Path $buildDirFull "VST3\Sillage.vst3\Contents\x86_64-win\Sillage.vst3")
    Invoke-Sign (Join-Path $buildDirFull "Standalone\Sillage.exe")

    & $isccPath "/DAppVersion=$Version" "/DBuildDir=$buildDirFull" "/DOutputDir=$outDirFull" $script
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

    Invoke-Sign $installer
}
finally {
    if ($pfxPath -and (Test-Path $pfxPath)) {
        Remove-Item -Force $pfxPath -ErrorAction SilentlyContinue
    }
}

Write-Host "built $installer"
