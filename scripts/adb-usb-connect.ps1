#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Serial = "",
    [int]$ControlPort = 28451,
    [switch]$LaunchApp
)

$ErrorActionPreference = "Stop"

$adbExe = (Get-Command adb -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
if (-not $adbExe) {
    $adbCandidates = @(
        (Join-Path $env:ANDROID_HOME "platform-tools\adb.exe"),
        (Join-Path $env:ANDROID_SDK_ROOT "platform-tools\adb.exe"),
        (Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path $_) }
    $adbExe = $adbCandidates | Select-Object -First 1
}
if (-not $adbExe) {
    throw "adb.exe not found in PATH, ANDROID_HOME, or %LOCALAPPDATA%\Android\Sdk\platform-tools."
}

$argsPrefix = @()
if (-not [string]::IsNullOrWhiteSpace($Serial)) {
    $argsPrefix = @("-s", $Serial)
}

Write-Host "Checking connected ADB devices..." -ForegroundColor Cyan
& $adbExe @argsPrefix devices -l
if ($LASTEXITCODE -ne 0) { throw "adb devices failed." }

Write-Host "Configuring USB reverse tunnel tcp:$ControlPort -> tcp:$ControlPort..." -ForegroundColor Cyan
& $adbExe @argsPrefix reverse "tcp:$ControlPort" "tcp:$ControlPort"
if ($LASTEXITCODE -ne 0) { throw "adb reverse failed. Ensure a single device is connected or pass -Serial." }

if ($LaunchApp) {
    Write-Host "Launching com.aegissoft.aegisdisplay/.MainActivity..." -ForegroundColor Cyan
    & $adbExe @argsPrefix shell am start -n "com.aegissoft.aegisdisplay/.MainActivity"
}

Write-Host "USB ADB tunnel ready. In aegisDisplay Android app, tap 'USB / Localhost (127.0.0.1)' and Connect." -ForegroundColor Green
