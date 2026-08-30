#Requires -Version 5.1
<#
.SYNOPSIS
    Creates a local self-signed code-signing certificate and signs the
    development build outputs, for use on your OWN test/dev machine only.
.DESCRIPTION
    Not valid for distribution. See README.md section 5 for why: a
    self-signed certificate shows "Unknown Publisher" and triggers
    SmartScreen warnings on any machine other than one where you've
    manually imported this exact certificate into the Trusted Root store.
    For production distribution you need an OV/EV Authenticode certificate
    from a public CA.
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$certSubject = "CN=PDF Printer Dev Signing (NOT FOR DISTRIBUTION)"
$existing = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $certSubject }

if (-not $existing) {
    Write-Host "Creating self-signed dev certificate..." -ForegroundColor Cyan
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $certSubject `
        -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature `
        -FriendlyName "PDF Printer Dev Signing"
}
else {
    $cert = $existing[0]
}

Write-Host "Importing certificate into local Trusted Root (dev machine only)..." -ForegroundColor Cyan
$rootStore = Get-Item Cert:\LocalMachine\Root
$rootStore.Open("ReadWrite")
$rootStore.Add($cert)
$rootStore.Close()

$targets = @(
    "$Root\Driver\PortMonitor\x64\$Configuration\pdfpm.dll",
    "$Root\Installer\bin\$Configuration\PDF Printer Setup.exe"
)

foreach ($target in $targets) {
    if (Test-Path $target) {
        Write-Host "Signing $target" -ForegroundColor Cyan
        Set-AuthenticodeSignature -FilePath $target -Certificate $cert -TimestampServer "http://timestamp.digicert.com" | Out-Null
    }
    else {
        Write-Host "Skipping (not built yet): $target" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Dev signing complete. THIS IS NOT VALID FOR PUBLIC DISTRIBUTION." -ForegroundColor Red
Write-Host "See README.md section 5 for production signing requirements." -ForegroundColor Red
