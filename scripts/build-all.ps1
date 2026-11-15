#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CoreDir = Join-Path $RepoRoot "host\core"
$CoreBuildDir = Join-Path $CoreDir "build"
$ConsoleProj = Join-Path $RepoRoot "host\console\aegisDisplayConsole.csproj"
$ConsoleTestProj = Join-Path $RepoRoot "host\console.tests\aegisDisplayConsole.Tests.csproj"
$AndroidDir = Join-Path $RepoRoot "android"
$DistDir = Join-Path $RepoRoot "dist"

Write-Host "=== [1/4] Building aegisDisplayCore ($Configuration) ===" -ForegroundColor Cyan
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $cand = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $cand) { $vcvars = $cand }
    }
}
if (-not $vcvars) {
    throw "Visual Studio C++ x64 build tools (vcvars64.bat) not found."
}

$testPart = if (-not $SkipTests) { " && ctest --test-dir `"$CoreBuildDir`" --output-on-failure" } else { "" }
$coreCmd = "call `"$vcvars`" && cmake -S `"$CoreDir`" -B `"$CoreBuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration && cmake --build `"$CoreBuildDir`" --parallel$testPart"
cmd /c $coreCmd
if ($LASTEXITCODE -ne 0) { throw "CMake/Ninja/CTest failed for aegisDisplayCore." }

Write-Host "=== [2/4] Building aegisDisplayConsole (.NET 11 WPF) ===" -ForegroundColor Cyan
dotnet build $ConsoleProj -c $Configuration
if ($LASTEXITCODE -ne 0) { throw "dotnet build failed for aegisDisplayConsole." }

if (-not $SkipTests) {
    dotnet test $ConsoleTestProj -c $Configuration
    if ($LASTEXITCODE -ne 0) { throw "dotnet test failed for aegisDisplayConsole.Tests." }
}

Write-Host "=== [3/4] Building Android Client (com.aegissoft.aegisdisplay) ===" -ForegroundColor Cyan
Push-Location $AndroidDir
try {
    $gradlew = Join-Path $AndroidDir "gradlew.bat"
    if (-not $SkipTests) {
        & $gradlew testDebugUnitTest assembleDebug
    } else {
        & $gradlew assembleDebug
    }
    if ($LASTEXITCODE -ne 0) { throw "Gradle build failed for Android client." }
} finally {
    Pop-Location
}

Write-Host "=== [4/4] Staging Portable Artifacts -> $DistDir ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DistDir "drivers\sudovda") | Out-Null

Copy-Item (Join-Path $CoreBuildDir "aegisDisplayCore.exe") $DistDir -Force
Copy-Item (Join-Path $RepoRoot "host\drivers\sudovda\*") (Join-Path $DistDir "drivers\sudovda") -Force

$consoleOut = Join-Path $RepoRoot "host\console\bin\$Configuration\net11.0-windows\*"
Copy-Item $consoleOut $DistDir -Recurse -Force

$apkPath = Join-Path $AndroidDir "app\build\outputs\apk\debug\app-debug.apk"
if (Test-Path $apkPath) {
    Copy-Item $apkPath (Join-Path $DistDir "aegisDisplay-debug.apk") -Force
}

Write-Host "=== Build Complete: $DistDir ===" -ForegroundColor Green
