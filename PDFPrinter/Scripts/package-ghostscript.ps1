#Requires -Version 5.1
<#
.SYNOPSIS
    Stages the Ghostscript 64-bit redistributable into Scripts\redist\ghostscript
    so the WiX installer (Product.wxs GhostscriptComponents) can pick it up.
.DESCRIPTION
    This script does NOT download Ghostscript (this environment has no
    network access, and silently fetching a third-party AGPL binary during a
    build is exactly the kind of "hidden dependency" README.md section 3
    calls out to avoid). Instead:
      1. Download the official 64-bit Ghostscript AGPL release yourself from
         https://www.ghostscript.com/releases/gsdnld.html
      2. Extract gswin64c.exe and gsdll64.dll from the installer/zip
      3. Place them at Scripts\redist\ghostscript\gswin64c.exe and
         Scripts\redist\ghostscript\gsdll64.dll
      4. Re-run this script (or build-all.ps1, which calls it) to verify.
    Resolve the AGPL-vs-commercial-license question (README.md section 3)
    before shipping a build containing this redistributable publicly.
#>

$ErrorActionPreference = "Stop"
$redistDir = Join-Path $PSScriptRoot "redist\ghostscript"

New-Item -ItemType Directory -Force -Path $redistDir | Out-Null

$required = @("gswin64c.exe", "gsdll64.dll")
$missing = $required | Where-Object { -not (Test-Path (Join-Path $redistDir $_)) }

if ($missing.Count -gt 0) {
    Write-Host "Ghostscript redistributable files are missing:" -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    Write-Host ""
    Write-Host "Download the 64-bit Ghostscript release from:" -ForegroundColor Yellow
    Write-Host "  https://www.ghostscript.com/releases/gsdnld.html" -ForegroundColor Yellow
    Write-Host "and place the files above in:" -ForegroundColor Yellow
    Write-Host "  $redistDir" -ForegroundColor Yellow
    throw "Ghostscript redistributable not staged; see instructions above."
}

Write-Host "Ghostscript redistributable staged at $redistDir" -ForegroundColor Green
