<#
  FitnessAI Android tracker - installer (Windows)

  Pulls the latest release straight from the GitHub repo, checks whether your
  installed version is up to date, and sideloads it to a USB-connected phone via
  adb. No app store. Re-run any time to update - it only installs when GitHub has
  a newer versionCode than what's on your device.

  Usage:   irm https://<your-site>/install.ps1 | iex
    or:    powershell -ExecutionPolicy Bypass -File install.ps1
#>
$ErrorActionPreference = 'Stop'
$Repo = 'PyMite6941/Fitness-AI-Agents'
$Raw  = "https://raw.githubusercontent.com/$Repo/main/mobile/version.json"

Write-Host 'FitnessAI tracker installer'
Write-Host 'Checking GitHub for the latest version...'

# --- read the version manifest from GitHub ---------------------------------
try { $meta = Invoke-RestMethod -Uri $Raw } catch { Write-Host 'Could not reach GitHub. Check your connection.'; exit 1 }
$latestVer  = $meta.androidApp.version
$latestCode = [int]$meta.androidApp.versionCode
$apkUrl     = $meta.androidApp.apkUrl
Write-Host "Latest published: v$latestVer (build $latestCode)"

# --- is adb available + a phone connected? ---------------------------------
$adb = (Get-Command adb -ErrorAction SilentlyContinue)
if (-not $adb) {
  Write-Host ''
  Write-Host 'adb (Android Platform Tools) is not installed.'
  Write-Host 'Install it (https://developer.android.com/tools/releases/platform-tools)'
  Write-Host 'or just download the APK on your phone directly and tap it to install:'
  Write-Host "   $apkUrl"
  exit 0
}

$devices = (& adb devices | Select-Object -Skip 1 | Where-Object { $_ -match "`tdevice$" })
if (-not $devices) {
  Write-Host 'No phone detected over USB. Enable USB debugging and reconnect, or'
  Write-Host "download the APK on the phone directly: $apkUrl"
  exit 0
}

# --- compare with the installed versionCode --------------------------------
$dump = (& adb shell dumpsys package studio.tin.fitnessai 2>$null) -join "`n"
$installedCode = $null
if ($dump -match 'versionCode=(\d+)') { $installedCode = [int]$Matches[1] }
if ($null -ne $installedCode) {
  Write-Host "Installed build: $installedCode"
  if ($installedCode -ge $latestCode) { Write-Host 'Already up to date. Nothing to do.'; exit 0 }
  Write-Host "Update available ($installedCode -> $latestCode)."
} else {
  Write-Host 'App not yet installed - doing a fresh install.'
}

# --- download + install -----------------------------------------------------
$apk = Join-Path $env:TEMP 'fitness-ai.apk'
Write-Host 'Downloading APK...'
Invoke-WebRequest -Uri $apkUrl -OutFile $apk
Write-Host 'Installing to device...'
& adb install -r $apk
Remove-Item $apk -ErrorAction SilentlyContinue
Write-Host "Done. Open 'FitnessAI' on your phone and paste your pairing code to link your account."
