# Prüfbericht - Crocosauf 40 LEDs, v6.6 / App 1.1

Datum: 04.10.2026. Geprüfter Code-Commit: `02ebb36cd9241491f43f098a27486c7c0e6d17bf`.

[GitHub Actions Build 15](https://github.com/mkleinekorte-sudo/crocosauf-/actions/runs/37221458573)

- ESP32-Job 111492482490: erfolgreich. Arduino-Core 3.3.7, ESP32 Dev Module, Huge APP. Flash: 1.493.287 / 3.145.728 Byte; globale Variablen: 48.356 / 327.680 Byte.
- Android-Job 111492482593: erfolgreich. APK-Build und Lint abgeschlossen; APK Signature Scheme v2 erfolgreich verifiziert. Paket de.mkrativ.crocosauf, versionCode 2, Version 1.1, Android 8+ (minSdk 26), targetSdk 35.
- Android-Oberflächen-Job 111492669650: erfolgreich. Android-35-Emulator, Pixel-7-Profil. Resultat: CROCOSAUF_UI_PASS. Konfigurationsdaten werden für die UI-Prüfung injiziert; eine echte Bluetooth-Verbindung mit Hardware wird dabei nicht behauptet.
- 35 C++-Szenarien und 267 Java-Protokollprüfungen erfolgreich, darunter zwei Stunden simulierte Laufzeit mit Remindern und anschließendem Spielstart.
- Original-Upload unverändert gesichert, zehn originale 40-LED-Paare geprüft.

## Ausgelieferte Dateien

Sketch SHA256:
`4b205039f6f76696d890881a8224d6fa8baa8e72b49e098f6d719f96b1283a4d`

APK SHA256:
`ff58fe72770c4664b93cd1a98ed6373cfca2cccba3808d32785bbfaeefccb8a5`

APK-Artefakt-ID: 11309828556. Downloadgröße der APK: 44912 Byte.
Die APK ist eine signierte Debug-/Testversion. Ein späterer Build kann einen anderen Debug-Schlüssel verwenden und dann die vorherige Deinstallation verlangen.

## Grenzen

Keine Hörprüfung, keine reale BLE-Funkstrecke und kein Dauertest an Marcels Gerät. Die Fehler im Schlaf-/Aufweckpfad sind korrigiert und abgesichert; die konkrete Ursache des gemeldeten Stillstands nach 30 Minuten ist nicht am Gerät nachgewiesen. Der 45-Minuten-Hardwaretest steht deshalb noch aus.
