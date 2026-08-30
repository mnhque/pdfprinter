#Requires -Version 5.1
<#
.SYNOPSIS
    Builds every PDF Printer component and produces "PDF Printer Setup.exe".
.DESCRIPTION
    Run this on a Windows machine with Visual Studio 2022 (C++ desktop +
    .NET desktop workloads), the Windows SDK, .NET 8 SDK, and WiX v4
    (`dotnet tool install --global wix`) already installed. See README.md
    section 3 for the full prerequisite list.
.PARAMETER Configuration
    Debug or Release. Defaults to Release.
.PARAMETER Platform
    Build platform for native projects. Only x64 is supported/tested.
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Step($name, [scriptblock]$action) {
    Write-Host ""
    Write-Host "==> $name" -ForegroundColor Cyan
    & $action
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) {
        throw "$name failed with exit code $LASTEXITCODE"
    }
}

Step "Locating msbuild via vswhere" {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install Visual Studio 2022." }
    $script:msbuildPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $script:msbuildPath) { throw "MSBuild not found via vswhere." }
    Write-Host "Using $script:msbuildPath"
}

Step "Building port monitor (pdfpm.dll)" {
    & $script:msbuildPath "$root\Driver\PortMonitor\PortMonitor.vcxproj" `
        /p:Configuration=$Configuration /p:Platform=$Platform /m
}

Step "Building installer custom actions (PDFPrinter.CustomActions.dll)" {
    & $script:msbuildPath "$root\Installer\CustomActions\PDFPrinter.CustomActions.vcxproj" `
        /p:Configuration=$Configuration /p:Platform=$Platform /m
}

Step "Publishing Conversion Service (.NET 8)" {
    dotnet publish "$root\PDFEngine\ConversionService\PDFPrinter.ConversionService.csproj" `
        -c $Configuration -r win-x64 --self-contained false
}

Step "Publishing Config App (.NET 8 / WPF)" {
    dotnet publish "$root\Configuration\ConfigApp\PDFPrinter.ConfigApp.csproj" `
        -c $Configuration -r win-x64 --self-contained false
}

Step "Staging Ghostscript redistributable" {
    & "$PSScriptRoot\package-ghostscript.ps1"
}

Step "Building MSI/EXE installer (WiX v4)" {
    Push-Location "$root\Installer"
    try {
        & $script:msbuildPath "PDFPrinter.Installer.wixproj" `
            /p:Configuration=$Configuration `
            /p:Platform=$Platform `
            /m
    }
    finally {
        Pop-Location
    }
}

Write-Host ""
Write-Host "Build complete. Output: Installer\bin\$Configuration\PDF Printer Setup.exe" -ForegroundColor Green
Write-Host "Remember to sign pdfpm.dll and the installer before distribution (see README.md section 5)." -ForegroundColor Yellow
