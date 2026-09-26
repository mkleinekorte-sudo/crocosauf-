#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
# JDK 17, Android SDK platform 35 / build-tools 35.0.0 must be installed.
if ! command -v java >/dev/null; then echo 'Bitte JDK 17 installieren und JAVA_HOME/PATH setzen.'; exit 1; fi
if [[ -z "${ANDROID_HOME:-}" && -z "${ANDROID_SDK_ROOT:-}" && ! -f local.properties ]]; then
  echo 'ANDROID_HOME auf dein Android-SDK setzen oder den SDK-Pfad in Android Studio konfigurieren.'; exit 1
fi
croco_cache="${XDG_CACHE_HOME:-$HOME/.cache}/crocosauf"
mkdir -p "$croco_cache"
croco_gradle="$croco_cache/gradle-8.11.1"
if [[ ! -x "$croco_gradle/bin/gradle" ]]; then
  curl --fail --location https://services.gradle.org/distributions/gradle-8.11.1-bin.zip -o "$croco_cache/gradle.zip"
  curl --fail --location https://services.gradle.org/distributions/gradle-8.11.1-bin.zip.sha256 -o "$croco_cache/gradle.sha256"
  python3 - "$croco_cache/gradle.zip" "$croco_cache/gradle.sha256" <<'PY'
import hashlib,sys
from pathlib import Path
assert hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()==Path(sys.argv[2]).read_text().strip(), 'Gradle checksum mismatch'
PY
  unzip -q -o "$croco_cache/gradle.zip" -d "$croco_cache"
fi
"$croco_gradle/bin/gradle" wrapper --gradle-version 8.11.1
"$croco_gradle/bin/gradle" --no-daemon :app:assembleDebug :app:lintDebug
printf '\nAPK erstellt: %s/app/build/outputs/apk/debug/app-debug.apk\n' "$PWD"
