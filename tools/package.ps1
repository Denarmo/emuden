# Assembles a runnable EmuDEN folder from a finished build.
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-BuildDir build\win] [-Out dist\windows] [-Build] [-Zip]
# -Build runs tools\build.ps1 for the app first; -Zip also writes <Out>.zip.
param(
    [string]$BuildDir = 'build\win',
    [string]$Out = 'dist\windows',
    [switch]$Build,
    [switch]$Zip
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root

if ($Build) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset win -BuildDir $BuildDir -Target den_app
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}
$bin = Join-Path $BuildDir 'bin'
$exe = Join-Path $bin 'emuden.exe'
if (-not (Test-Path $exe)) { throw "no ${exe}: build first (tools\build.ps1 -Preset win) or pass -Build" }

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item $exe $Out
Get-ChildItem $bin -Filter *.dll | Copy-Item -Destination $Out
# Assets come from the source tree, so a stale copy in the build folder never ships.
Copy-Item -Recurse (Join-Path $root 'assets') (Join-Path $Out 'assets')

$licences = Join-Path $Out 'licences'
New-Item -ItemType Directory -Force $licences | Out-Null
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $licences 'EmuDEN.txt')
$third = [ordered]@{
    'SDL3.txt'          = '.deps\sdl3-src\LICENSE.txt'
    'zlib.txt'          = '.deps\zlib-src\LICENSE'
    'FreeType.txt'      = '.deps\freetype-src\LICENSE.TXT'
    'FreeType-FTL.txt'  = '.deps\freetype-src\docs\FTL.TXT'
    'stb.txt'           = '.deps\stb-src\LICENSE'
    'Monocypher.md'     = 'plugins\src\third_party\monocypher\LICENCE.md'
    'WebView2.txt'      = '.deps\webview2-src\LICENSE.txt'
    'XZ-Embedded.txt'   = '.deps\xzembedded-src\COPYING'
    'LZMA-SDK.txt'      = '.deps\lzma-sdk-src\DOC\lzma-sdk.txt'
    'Google-Sans-OFL.txt' = 'assets\fonts\OFL.txt'
}
foreach ($name in $third.Keys) {
    $source = Join-Path $root $third[$name]
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $licences $name)
    } else {
        Write-Warning "licence not found: $($third[$name])"
    }
}

# Game data stays out of the package: EmuDEN uses the OS app-data folder unless a "user"
# folder is created beside the executable (portable mode).
$size = (Get-ChildItem -Recurse $Out | Measure-Object Length -Sum).Sum
Write-Host ("{0}: {1:N1} MB" -f (Resolve-Path $Out), ($size / 1MB))
if ($Zip) {
    $archive = "$Out.zip"
    if (Test-Path $archive) { Remove-Item $archive }
    Compress-Archive -Path (Join-Path $Out '*') -DestinationPath $archive
    Write-Host (Resolve-Path $archive)
}
