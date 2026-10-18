param(
    [Parameter(Mandatory)][string]$VentoyDeploy,
    [Parameter(Mandatory)][string]$ProfileName,
    [Parameter(Mandatory)][string]$OutFile
)
Set-StrictMode -Version Latest
Import-Module (Join-Path $VentoyDeploy "manager\Resolve.psm1") -Force

$resolved = Resolve-Profile -ProfileName $ProfileName -RepoRoot $VentoyDeploy -HostTimezone "UTC" -HostLocale "en_US.UTF-8"
$reference = [ordered]@{
    profile       = $ProfileName
    packages      = @($resolved.Packages | Select-Object -Unique)
    aur           = @($resolved.Aur | Select-Object -Unique)
    flatpak       = @($resolved.Flatpak | Select-Object -Unique)
    repo_setup    = @($resolved.RepoSetup | Select-Object -Unique)
    services      = @($resolved.Services | Select-Object -Unique)
    user_services = @($resolved.UserServices | Select-Object -Unique)
    skipped       = @($resolved.Skipped | Select-Object -Unique)
}
$json = ([pscustomobject]$reference | ConvertTo-Json -Depth 5) -replace "`r`n", "`n"
# Windows PowerShell's Out-File and Set-Content write a BOM or the ANSI codepage.
[System.IO.File]::WriteAllText($OutFile, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
