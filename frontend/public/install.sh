#!/usr/bin/env bash
# FitnessAI Android tracker — installer (macOS / Linux)
#
# Pulls the latest release straight from the GitHub repo, checks whether your
# installed version is up to date, and sideloads it to a USB-connected phone via
# adb. No app store. Re-run any time to update — it only installs when GitHub has
# a newer versionCode than what's on your device.
#
# Usage:   curl -fsSL https://<your-site>/install.sh | bash
#   or:    bash install.sh
set -euo pipefail

REPO="PyMite6941/Fitness-AI-Agents"
RAW="https://raw.githubusercontent.com/${REPO}/main/mobile/version.json"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "FitnessAI tracker installer"
echo "Checking GitHub for the latest version…"

# --- read the version manifest from GitHub ---------------------------------
META="$(curl -fsSL "$RAW")" || { echo "Could not reach GitHub. Check your connection."; exit 1; }
json() { printf '%s' "$META" | sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\{0,1\}\([^\",}]*\)\"\{0,1\}.*/\1/p" | head -1; }

LATEST_VER="$(json version)"
LATEST_CODE="$(json versionCode)"
APK_URL="$(json apkUrl)"
echo "Latest published: v${LATEST_VER} (build ${LATEST_CODE})"

# --- is a phone connected over adb? ----------------------------------------
if ! command -v adb >/dev/null 2>&1; then
  echo
  echo "adb (Android Platform Tools) is not installed."
  echo "Either install it (https://developer.android.com/tools/releases/platform-tools)"
  echo "or just download the APK on your phone directly and tap it to install:"
  echo "   $APK_URL"
  exit 0
fi

DEVICES="$(adb devices | awk 'NR>1 && $2=="device"{print $1}')"
if [ -z "$DEVICES" ]; then
  echo "No phone detected over USB. Enable USB debugging and reconnect, or download"
  echo "the APK on the phone directly: $APK_URL"
  exit 0
fi

# --- compare with the installed versionCode --------------------------------
INSTALLED_CODE="$(adb shell dumpsys package studio.tin.fitnessai 2>/dev/null \
  | sed -n 's/.*versionCode=\([0-9]*\).*/\1/p' | head -1 || true)"
if [ -n "${INSTALLED_CODE:-}" ]; then
  echo "Installed build: ${INSTALLED_CODE}"
  if [ "${INSTALLED_CODE}" -ge "${LATEST_CODE}" ] 2>/dev/null; then
    echo "Already up to date. Nothing to do."
    exit 0
  fi
  echo "Update available (${INSTALLED_CODE} → ${LATEST_CODE})."
else
  echo "App not yet installed — doing a fresh install."
fi

# --- download + install -----------------------------------------------------
echo "Downloading APK…"
curl -fsSL "$APK_URL" -o "$TMP/fitness-ai.apk"
echo "Installing to device…"
adb install -r "$TMP/fitness-ai.apk"
echo "Done. Open 'FitnessAI' on your phone and paste your pairing code to link your account."
