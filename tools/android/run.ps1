# SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds the debug APK, installs it on a device over adb and starts it with the automation
# endpoint forwarded to this PC, so tools/drive.py works as on the desktop:
#   powershell -ExecutionPolicy Bypass -File tools\android\run.ps1 -Device <serial> -Port 5601
#   python tools/drive.py --port 5601 state
# -Stop only stops the app and removes the forward. -Apk installs that file instead of the
# one Gradle writes. Nothing else on the device is touched.
param(
    [string]$Device = "",
    [int]$Port = 5577,
    [switch]$NoBuild,
    [string]$Apk = "",
    [switch]$GrantStorage,
    [switch]$Stop
)
$ErrorActionPreference = "Stop"
$repo = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA "Android\Sdk" }
$adb = Join-Path $env:ANDROID_HOME "platform-tools\adb.exe"
$target = @()
if ($Device) { $target = @("-s", $Device) }
$package = "io.github.denarmo.emuden.next"

if ($Stop) {
    & $adb @target shell am force-stop $package
    & $adb @target forward --remove "tcp:$Port"
    exit 0
}
if (-not $NoBuild -and -not $Apk) {
    Push-Location (Join-Path $repo "shells\android")
    try { & .\gradlew.bat :app:assembleDebug --console=plain; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
    finally { Pop-Location }
}
if (-not $Apk) { $Apk = Join-Path $repo "build\android\gradle\app\outputs\apk\debug\app-debug.apk" }
& $adb @target install -r $Apk
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($GrantStorage) {
    # The same as "Allow access" in the dialog the app shows at its start, then the switch on
    # the system screen it opens.
    & $adb @target shell appops set $package MANAGE_EXTERNAL_STORAGE allow
}
& $adb @target shell input keyevent KEYCODE_WAKEUP
& $adb @target forward "tcp:$Port" "tcp:$Port"
& $adb @target shell am start -n "$package/io.github.denarmo.emuden.EmuDenActivity" --ei automation $Port
