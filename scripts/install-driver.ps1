#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Candidates = @(
    (Join-Path $RepoRoot "aegisDisplayCore.exe"),
    (Join-Path $RepoRoot "dist\aegisDisplayCore.exe"),
    (Join-Path $RepoRoot "host\core\build\aegisDisplayCore.exe")
)

$CoreExe = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $CoreExe) {
    throw "aegisDisplayCore.exe not found. Run scripts\build-all.ps1 first or run from the extracted portable release folder."
}

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)

if (-not $isAdmin) {
    $scriptArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"")
    if ($Uninstall) { $scriptArgs += "-Uninstall" }
    Write-Host "Requesting Administrator elevation (UAC)..." -ForegroundColor Yellow
    $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $scriptArgs -Verb RunAs -Wait -PassThru
    exit $proc.ExitCode
}

$FwRules = @(
    @{ Name = "aegisDisplay Discovery (UDP 28450)";       Protocol = "UDP"; Port = 28450 },
    @{ Name = "aegisDisplay Control & Video (TCP 28451)"; Protocol = "TCP"; Port = 28451 },
    @{ Name = "aegisDisplay Low-Latency Video (UDP 28452)"; Protocol = "UDP"; Port = 28452 }
)

if ($Uninstall) {
    foreach ($rule in $FwRules) {
        Get-NetFirewallRule -DisplayName $rule.Name -ErrorAction SilentlyContinue | Remove-NetFirewallRule -ErrorAction SilentlyContinue
    }
    & $CoreExe "--uninstall-driver"
    exit $LASTEXITCODE
}

& $CoreExe "--install-driver"
$driverExit = $LASTEXITCODE
if ($driverExit -ne 0) {
    exit $driverExit
}

foreach ($rule in $FwRules) {
    $existing = Get-NetFirewallRule -DisplayName $rule.Name -ErrorAction SilentlyContinue
    if (-not $existing) {
        New-NetFirewallRule `
            -DisplayName $rule.Name `
            -Direction Inbound `
            -Action Allow `
            -Protocol $rule.Protocol `
            -LocalPort $rule.Port `
            -Profile Any `
            -Program $CoreExe | Out-Null
    }
}

Write-Host "[OK] SudoVDA driver and Windows Firewall rules (28450/UDP, 28451/TCP, 28452/UDP) configured." -ForegroundColor Green
exit 0
