# Crocosauf – App 1.1 und Firmware v6.6 für 40 LEDs

Grundlage ist Marcels Upload `Crocosauf_Deluxe_v6_4_BLE(1).ino` vom 04.10.2026: **40 LEDs, zehn feste LED-Paare, ausschließlich Bluetooth, kein WLAN/WebUI**. Die frühere 20-LED-Firmware bleibt im Repository als separate ältere Fassung.

- [Vollständiger Arduino-Sketch](firmware/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY/Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY.ino)
- [Installation, Änderungen und Gerätetest](firmware/UPDATE_v6_6_40LED.md)
- [Unveränderte Sicherung des 40-LED-Uploads](firmware/original_40LED_v6_4.ino.txt)
- [Builds und APK-Artefakte](https://github.com/mkleinekorte-sudo/crocosauf-/actions)

## Bedienung

Android 8 oder neuer. Die installierbare APK wird im Chat bereitgestellt und liegt nach erfolgreichem Build auch im Artefakt **Crocosauf-Android-APK**. Sie steuert die vorhandenen SD-Tracks über Bluetooth LE; sie überträgt keine Musikdateien oder Handy-Audiosignale.

In der App **Crocosauf verbinden** wählen, Berechtigung „Geräte in der Nähe“ erlauben, Crocosauf auswählen. Maul schließen, Spiel/Welcome abwarten, den Seitentaster drei Sekunden halten, bis **APP OK** erscheint. Die Freigabe gilt je Verbindung.

Die App bietet Musikgruppen mit Auswahl, Reihenfolge/Zufall und Tests, 20 LED-Effekte, 20 Displayanimationen, Lautstärke/Mute, Laufschrift, Reminder sowie neu:

- LED- und Displayhelligkeit in Prozent; Drehung 0/90/180/270 Grad.
- Audio an/aus, Startlautstärke und separate Welcome-Lautstärke.
- Reminder-Licht-/Displaydauer bis 30 Sekunden.
- Automatischer Schlaf: Aus, 15, 30 oder 60 Minuten. **Standard Aus.**
- Gerätestatus mit Laufzeit, letztem Startgrund und Display-Erreichbarkeit.

Alte v6.4-40LED-App-only-Firmware wird für ihre vorhandenen Einstellungen erkannt. Die neuen Schlaf- und Diagnosefunktionen benötigen den neuen v6.6-Sketch. App 1.0 kann mit der neuen Firmware weiter die bisher vorhandenen Befehle bedienen.

## Bauen

ESP32 Dev Module / klassischer WROOM mit 4 MB Flash, Espressif-Core 3.3.7, Partition **Huge APP (3MB No OTA/1MB SPIFFS)**. Libraries sind im Workflow festgelegt: FastLED 3.10.5, DFPlayerMini_Fast 1.2.4, FireTimer 1.0.5, Adafruit GFX 1.12.6, LED Backpack 1.5.1, BusIO 1.17.4.

Android: JDK 17, Gradle 8.11.1, SDK 35. Im Ordner `android`: `gradle :app:assembleDebug :app:lintDebug`. Vorhandene `build-app.sh` / `build-app.ps1` helfen beim lokalen Build. Debug-APKs aus unterschiedlichen Builds können andere Signaturen haben; dann vor Installation die alte App deinstallieren. Die Geräteeinstellungen liegen auf dem ESP32.

`bash tests/run-tests.sh` führt C++-Ablauftests und Java-Protokolltests aus. GitHub Actions baut zusätzlich den tatsächlichen ESP32-Sketch, die APK und den Android-Oberflächentest. Simulation, Emulator und Kompilierung ersetzen keinen Dauertest an Marcels Hardware.
