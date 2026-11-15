#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Serial = "",
    [int]$ControlPort = 28451,
    [switch]$LaunchApp
)

$ErrorActionPreference = "Stop"

$adbCmd = Get-Command adb -ErrorAction SilentlyContinue
if (-not $adbCmd) {
    throw "adb executable not found in PATH."
}

$argsPrefix = @()
if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $argsPrefix = @("-s", $Serial)
}

Write-Host "Checking connected ADB devices..." -ForegroundColor Cyan
& adb @argsPrefix devices -l
if ($LASTEXITCODE -ne 0) { throw "adb devices failed." }

Write-Host "Configuring USB reverse tunnel tcp:$ControlPort -> tcp:$ControlPort..." -ForegroundColor Cyan
& adb @argsPrefix reverse "tcp:$ControlPort" "tcp:$ControlPort"
if ($LASTEXITCODE -ne 0) { throw "adb reverse failed. Ensure a single device is connected or pass -Serial." }

if ($LaunchApp) {
    Write-Host "Launching com.aegissoft.aegisdisplay/.MainActivity..." -ForegroundColor Cyan
    & adb @argsPrefix shell am start -n "com.aegissoft.aegisdisplay/.MainActivity"
}

Write-Host "USB ADB tunnel ready. In aegisDisplay Android app, tap 'USB ADB Tunnel' or connect to 127.0.0.1." -ForegroundColor Green
