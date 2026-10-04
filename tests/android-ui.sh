#!/usr/bin/env bash
set -euo pipefail
mkdir -p ui-results
adb install -r apk/app-debug.apk
adb install -r test-apk/app-debug-androidTest.apk
adb shell am instrument -w de.mkrativ.crocosauf.test/de.mkrativ.crocosauf.SettingsInstrumentation | tee ui-results/instrumentation.txt
adb pull /sdcard/Android/data/de.mkrativ.crocosauf/files/screenshots ui-results/ || true
grep -q '^CROCOSAUF_UI_PASS:' ui-results/instrumentation.txt
