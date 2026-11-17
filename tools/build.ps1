# Configures and builds from a clean shell.
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Preset win [-Target emuden] [-BuildDir build\mine] [-Test] [-Version 0.2.0]
# -Version makes it a release build (EMUDEN_VERSION), which offers updates; it stays in the cache.
param(
    [string]$Preset = 'win',
    [string]$Target = '',
    [string]$BuildDir = '',
    [switch]$Test,
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root

if ($Preset -eq 'win' -and -not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'vswhere not found: install the Visual Studio 2022 build tools' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'no Visual Studio installation with the C++ tools' }
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x64 >nul && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}

if (-not $BuildDir) { $BuildDir = "build/$Preset" }
# Always configured: it is quick, and it picks up new directories and globbed files.
$configureArgs = @('--preset', $Preset, '-B', $BuildDir)
if ($Version) { $configureArgs += "-DEMUDEN_VERSION=$Version" }
cmake @configureArgs | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
$buildArgs = @('--build', $BuildDir, '--parallel')
if ($Target) { $buildArgs += @('--target') + $Target.Split(',') }
cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
if ($Test) {
    ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
}
