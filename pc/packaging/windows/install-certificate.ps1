# install-certificate.ps1 - trust the BricoHams code-signing certificate on
# this Windows user account, so the installer and RT950Toolkit.exe show
# "BricoHams" as verified publisher. Run from the folder that holds
# BricoHams-CodeSigning.cer:
#   powershell -ExecutionPolicy Bypass -File install-certificate.ps1
# SHA-1 thumbprint: 5F32E97D9BF2A4F80642F52CDEAFE494457B78D4
$cer = Join-Path $PSScriptRoot "BricoHams-CodeSigning.cer"
$c = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 $cer
if ($c.Thumbprint -ne "5F32E97D9BF2A4F80642F52CDEAFE494457B78D4") { throw "Unexpected certificate: $($c.Thumbprint)" }
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
Write-Host "BricoHams certificate installed (Trusted Publishers and Root of the current user)."
