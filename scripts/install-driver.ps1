#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Candidates = @(
    (Join-Path $RepoRoot "dist\aegisDisplayCore.exe"),
    (Join-Path $RepoRoot "host\core\build\aegisDisplayCore.exe")
)

$CoreExe = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $CoreExe) {
    throw "aegisDisplayCore.exe not found. Run scripts\build-all.ps1 first."
}

$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator
)

$arg = if ($Uninstall) { "--uninstall-driver" } else { "--install-driver" }

if ($isAdmin) {
    & $CoreExe $arg
    exit $LASTEXITCODE
} else {
    Write-Host "Requesting Administrator elevation (UAC) for $arg..." -ForegroundColor Yellow
    $proc = Start-Process -FilePath $CoreExe -ArgumentList $arg -Verb RunAs -Wait -PassThru
    exit $proc.ExitCode
}
