# Crocosauf Bluetooth - Android-App 1.0-beta

Für Marcels Crocosauf Deluxe mit ESP32 DevKit V1 / WROOM, DFPlayer, 20 LEDs und HT16K33-Display.
Stand: 04.10.2026. Aktuelle Firmware: **v6.5-BLE**; die vorhandene App **1.0-beta** bleibt kompatibel.

**Aktueller vollständiger Arduino-Sketch:** [Crocosauf_Deluxe_v6_5_BLE.ino](firmware/Crocosauf_Deluxe_v6_5_BLE/Crocosauf_Deluxe_v6_5_BLE.ino). Es ist eine Einzeldatei mit enthaltener Bluetooth-Steuerung.

Änderungen und gezielter Gerätetest: [Firmware v6.5](firmware/UPDATE_v6_5.md). Die frühere v6.4 bleibt als Sicherung im Repository.

**Die installierbare Android-Testversion ist fertig gebaut.** Android-Build, Android Lint, APK-Signaturprüfung und der echte ESP32-Build waren erfolgreich. Die APK wurde im Chat als `Crocosauf_1.0-beta.apk` bereitgestellt. Zusätzlich liegt sie als ZIP-Artefakt im [erfolgreichen GitHub-Build](https://github.com/mkleinekorte-sudo/crocosauf-/actions/runs/36250755658). Ein Versuch auf dem Handy und am echten Crocosauf steht noch aus.

**Voraussetzung für Bluetooth:** Den neuen Sketch `firmware/Crocosauf_Deluxe_v6_5_BLE` einmal per USB auf den ESP32 laden. Mit dem alten v6.2-Sketch kann sich die App nicht verbinden.

## Was du damit bedienen kannst

- Direkt aus einer nativen Android-App mit Crocosauf verbinden, ohne Browser oder WLAN-Wechsel.
- Spielmusik, GameOver, Welcome, Reminder und Neue-Runde-Sounds auswählen.
- Mehrere Tracks aktivieren und je Gruppe Zufall oder Reihenfolge einstellen.
- Sounds bei geschlossenem Maul testen und stoppen.
- Alle 20 LED-Effekte und 20 Displayanimationen auswählen und testen.
- Lautstärke per Schieberegler, Mute per Taste.
- Laufschrift sowie Reminder-Intervall, Dauer und Sound einstellen.
- Gerätezustand, Maulstellung und Pinchenrunde sehen.
- Werkseinstellungen nach Bestätigung laden.

Die App ist eine **Bluetooth-Fernbedienung für die SD-Dateien im DFPlayer**. Sie streamt weder Spotify noch Handyton, lädt keine MP3-Dateien hoch und liest keine Musiktitel aus der SD-Karte. Angezeigt werden die Tracknummern 001 usw. Für einen Bluetooth-Lautsprecherbetrieb wäre eine andere Audio-Lösung notwendig.

Die Spielmechanik, Maulerkennung und zehn Pinchenrunden arbeiten eigenständig weiter. Den Spielmodus wählst du weiterhin am mechanischen Normal/Pinchen-Schalter. Die bisherige Weboberfläche bleibt als zusätzliche Bedienmöglichkeit erhalten.

## 1. ESP32 vorbereiten

1. Deine bisherige funktionierende Firmware und die SD-Karte sichern.
2. Den gesamten Ordner `firmware/Crocosauf_Deluxe_v6_5_BLE` an einen geeigneten Platz kopieren.
3. Darin `Crocosauf_Deluxe_v6_5_BLE.ino` mit Arduino IDE öffnen. **v6.5 enthält alles in dieser einen Datei; keine zusätzlichen .h-Dateien nötig.**
4. ESP32-Boardpaket von Espressif installieren. Das aktuelle Buildziel verwendet **3.3.7**, wie Marcels Arduino-Installation.
5. Board **ESP32 Dev Module**, klassischer WROOM. Bei einem 4-MB-Modul unter Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)** auswählen. Diese Version nutzt kein OTA.
6. Bibliotheken installieren: FastLED, DFPlayerMini_Fast, FireTimer, Adafruit GFX Library, Adafruit LED Backpack Library, Adafruit BusIO. BLE ist Bestandteil des ESP32-Boardpakets; keine alte zusätzliche ESP32-BLE-Bibliothek installieren.
7. Überprüfen/kompilieren, anschließend per USB hochladen und neu einschalten.

Die vorhandene Pinbelegung bleibt erhalten: LEDs GPIO18, Maul GPIO27, Modus GPIO25, Lautstärke GPIO26, DFPlayer RX/TX an GPIO16/17, BUSY GPIO33, Display SDA21/SCL22. Die SD-Ordner bleiben Root sowie /02, /03, /04 und /05. Gespeicherte Spieleinstellungen werden weiterverwendet.

## 2. Android-App installieren

Ziel: Android 8 oder neuer, insbesondere dein Pixel 7. Das Paket verwendet native Android-Ansichten und die Android-Bluetooth-Schnittstelle, keinen WebView und keinen Webserver für die App-Verbindung.

### Fertige APK – kein Android Studio nötig

1. `Crocosauf_1.0-beta.apk` aus dem Chat auf dein Handy herunterladen. Alternativ im oben verlinkten Build das Artefakt **Crocosauf-Android-APK** herunterladen und die ZIP entpacken; darin liegt dieselbe Datei als `app-debug.apk`.
2. APK in der Datei-App öffnen. Falls Android nachfragt, der verwendeten Datei-App das Installieren aus dieser Quelle erlauben.
3. **Installieren** wählen, anschließend **Crocosauf** öffnen.
4. Beim Verbinden die Bluetooth-Berechtigung **Geräte in der Nähe** erlauben. Weiter mit Abschnitt 3.

Die folgenden Schritte sind nur nötig, wenn du die App selbst neu bauen möchtest.

### Auf einem Windows-PC

1. Android Studio mit Android SDK installieren.
2. Im SDK Manager **Android SDK Platform 35** und **Android SDK Build-Tools 35.0.0** installieren. SDK-Lizenzen bestätigen. Für das Projekt JDK 17 verwenden.
3. ZIP entpacken. Im Unterordner `android` eine PowerShell öffnen.
4. `powershell -ExecutionPolicy Bypass -File .\build-app.ps1` ausführen. Dies gilt für diesen Prozess. Das Skript lädt Gradle 8.11.1 von Gradle herunter, prüft dessen SHA-256-Prüfsumme und baut die App. Internetzugang wird nur zum Bauen benötigt.
5. Bei Erfolg liegt die APK unter `android/app/build/outputs/apk/debug/app-debug.apk`.
6. Diese APK auf dein Handy kopieren und dort öffnen. Android fragt gegebenenfalls, ob deine Datei-App aus dieser Quelle Apps installieren darf. Die Berechtigung kannst du nach der Installation wieder ausschalten.

Das ist zunächst eine Debug-/Test-APK. Sie ist nicht im Play Store veröffentlicht. Für wiederholte Updates denselben Signaturschlüssel behalten; bei Builds auf unterschiedlichen PCs kann eine Deinstallation nötig sein. Dabei geht die in der App gemerkte Geräteadresse verloren, die Crocosauf-Einstellungen bleiben auf dem ESP32.

Nach dem ersten erfolgreichen Skriptlauf ist auch der Gradle-Wrapper erzeugt. Danach lässt sich der Ordner `android` normal in Android Studio öffnen und bauen.

### Linux/macOS

Mit installiertem JDK 17, Android SDK 35 und gesetztem `ANDROID_HOME`: im Ordner `android` das Skript `./build-app.sh` starten. Dafür werden curl, unzip und Python 3 benötigt. Die APK liegt am gleichen relativen Ort.

### Alternativ über GitHub Actions

Der Workflow `.github/workflows/build.yml` startet beim Hochladen nach `main` automatisch und lässt sich unter Actions **Crocosauf Build** erneut starten. Er baut die Android-APK und den ESP32-Sketch in getrennten Jobs. Die Dateien erscheinen bei Erfolg als Build-Artefakte. Der Android-Job prüft außerdem die APK-Signatur, Paketinformationen und Android Lint. Den aktuellen Buildstatus zeigt die Actions-Seite dieses Repositorys.

## 3. Erstmals verbinden

1. Crocosauf einschalten. Ist es im Schlafmodus, Maul öffnen, danach wieder schließen.
2. App öffnen, **Crocosauf verbinden** antippen.
3. Bluetooth einschalten und die angefragte Berechtigung erlauben. Ab Android 12 heißt sie „Geräte in der Nähe“. Unter Android 8-11 braucht die Suche zusätzlich die Standortberechtigung und meist einen eingeschalteten Standortdienst; die App bestimmt oder speichert keinen Standort.
4. **Crocosauf Deluxe** in der Liste auswählen.
5. Welcome/Spielphase abwarten, Maul geschlossen lassen und den seitlichen Lautstärketaster **3 Sekunden halten**.
6. Auf dem Display erscheint **APP OK**. Die App lädt die Einstellungen; die Bedienelemente werden aktiv.

Für jede neue Verbindung ist diese kurze Freigabe nötig. Ohne Freigabe werden alle Steuerbefehle und das Auslesen der Einstellungen abgewiesen. Nach 90 Sekunden ohne Freigabe wird die Verbindung getrennt. Eine bereits gedrückte Taste zählt nach einer Neuverbindung erst nach Loslassen und erneutem Drücken.

Es gibt keine Kopplung über die Bluetooth-Systemliste und keinen PIN. Die Freigabe ist eine lokale Bedienfreigabe für die aktuelle Verbindung; diese erste Version richtet kein verschlüsseltes Bluetooth-Bonding ein. Es werden nur Spielbefehle und Geräteeinstellungen übertragen.

Nach einer erfolgreichen Verbindung merkt sich die App die Geräteadresse. Beim nächsten Mal kannst du **Letztes Crocosauf verbinden** verwenden.

## 4. Musik auswählen

1. Tab **Musik** öffnen und die Soundgruppe auswählen.
2. Die gewünschten Tracknummern anhaken. Mindestens einen Eintrag aktiv lassen.
3. Schalter „Zufall statt Reihenfolge“ nach Wunsch setzen.
4. **Auswahl speichern** drücken und die Bestätigung abwarten.

| Gruppe | Dateien |
|---|---|
| Spielmusik | /001.mp3 bis /012.mp3, optional /01 wie im bisherigen Sketch |
| GameOver | /02/001.mp3 bis /02/012.mp3 |
| Welcome | /03/001.mp3 bis /03/006.mp3 |
| Reminder | /04/001.mp3 bis /04/006.mp3 |
| Neue Runde | /05/001.mp3 bis /05/005.mp3 |

Die Auswahl gilt für kommende Auswahlen des Spiels. Eine bereits laufende Runde behält ihren Musiktrack. Mit **Test** wird ein Sound einmal vorgespielt; derselbe Testknopf stoppt ihn wieder. Tests sind nur bei geschlossenem Maul im Bereitschaftszustand möglich. Mute muss für Soundtests aus sein. Testende spätestens nach 60 Sekunden. Öffnen oder Moduswechsel beendet den Test.

Die Root-Reihenfolge hängt weiterhin vom DFPlayer-Dateiindex/Kopierablauf ab. Bluetooth ändert diese Eigenschaft nicht.

## 5. Effekte und System

Unter **Effekte** gibt es drei getrennte Gruppen: LED-Effekte beim Spielen, LED-Effekte beim Reminder und Displayanimationen im Normalmodus. Auswählen, testen und speichern funktionieren wie bei Musik.

Unter **System**:

- Lautstärke 5-30 per Schieberegler, Übertragung beim Loslassen. Der Regler hebt Mute nicht automatisch auf.
- **Stummschalten / Ton einschalten** ändert Mute.
- Reminder 1-60 Minuten, Dauer 1500-9000 ms; Erinnerung und Sound separat schaltbar.
- Laufschrift bis 40 darstellbare Zeichen; Umlaute werden ausgeschrieben, ungeeignete Zeichen entfernt.
- Werkseinstellungen erst nach Rückfrage und nur bei geschlossenem Maul in Bereitschaft.
- **Einstellungen neu laden** verwirft ungespeicherte App-Änderungen und liest den aktuellen Stand erneut. Das ist auch nach Änderungen über die zusätzliche Weboberfläche sinnvoll.

## 6. Verbindung, Schlaf und Strom

Die App braucht im Betrieb weder Internet noch einen WLAN-Wechsel. Sie fordert keine Internetberechtigung an und enthält keine Werbung, Analyse- oder Cloud-Dienste.

Beim Verlassen der App wird die Bluetooth-Verbindung beendet. Das Spiel läuft auf dem ESP32 weiter. Beim Zurückkehren erneut verbinden und freigeben. Es gibt keine versteckte Hintergrundverbindung.

Solange ein Bluetooth-Gerät verbunden ist, schläft Crocosauf nicht ein. Nach dem Trennen greift wieder die vorhandene Ruhelogik. Im Deep Sleep ist Bluetooth nicht erreichbar; Maul öffnen weckt den ESP32. Ein reines Timer-Aufwachen für einen Reminder startet Bluetooth nicht unnötig.

WLAN und Bluetooth sind voneinander unabhängig. Das automatische WLAN-Aus beendet keine laufenden Bluetooth-Tests. Ein vollständiges Ausschalten der Powerbank trennt natürlich beide Verbindungen.

Die öffentliche Firmware enthält kein festes WLAN-Passwort. Beim ersten WLAN-Start erzeugt der ESP32 ein individuelles Passwort mit 16 Zeichen und speichert es auf dem Gerät. Falls du die zusätzliche Weboberfläche verwenden möchtest: USB anschließen, Seriellmonitor auf **115200 Baud** stellen und den ESP32 neu starten; dort erscheint „Crocosauf WLAN-Passwort“. SSID ist `crocosauf`, Adresse `http://192.168.4.1`. Das WLAN-Passwort bleibt auch beim Zurücksetzen der Spieleinstellungen erhalten. Für die Bluetooth-App brauchst du dieses Passwort nicht.

## 7. Prüfung vor dem ersten Spielabend

- Passende Firmware v6.5-BLE über USB auf den ESP32 laden; beim Selbstbau die unten geprüften Bibliotheksversionen verwenden.
- Gerät in der App finden, Freigabe ausführen, alle Einstellungen korrekt laden.
- Vor der Freigabe müssen Steuerbefehle gesperrt sein.
- Track testen; Maul öffnen: Test endet, Spiel startet.
- Welcome während Verbindung/Bedienung vollständig abwarten lassen.
- Musikgruppen, Effektauswahl, Lautstärke und Reminder ändern, danach Gerät neu starten und erneut auslesen.
- App schließen und neu verbinden: erneute Freigabe erforderlich.
- Während Bluetooth verbunden bleibt, WLAN nach fünf Minuten ohne WLAN-Client ausschalten lassen.
- Danach geschlossen und ohne App/WLAN-Client schlafen lassen und durch Öffnen wieder wecken.

## 8. Prüfung der früheren v6.4 und der App

Die folgenden Buildwerte gehören zu v6.4 bzw. der unveränderten App. Die v6.5-Prüfung ist in `firmware/UPDATE_v6_5.md` dokumentiert.

- Echter Android-Build mit SDK 35, Build-Tools 35.0.0, Gradle 8.11.1, AGP 8.9.2 und JDK 17: erfolgreich.
- Android Lint: keine Fehler; drei Hinweise zu Ziel-API, älteren Android-Versionen und Backup-Konfiguration. Kein Laufzeittest auf einem Handy oder Emulator.
- APK-Signatur (Schema v2) mit `apksigner verify` erfolgreich geprüft. Nach Download SHA-256 mit dem Buildprotokoll verglichen.
- APK: Paket `de.mkrativ.crocosauf`, Version `1.0-beta`, 39.432 Bytes, Android ab API 26.
- APK SHA-256: `2035eae5c2dd5acec8863851b0f8bdbeeb339562e174d14e5c5bbdb511524aaf`.
- Echter ESP32-Build mit Boardpaket 3.3.0 und Huge-APP-Partition: erfolgreich. Flash 2.097.159 / 3.145.728 Bytes; globale Variablen 65.504 / 327.680 Bytes. Dynamischer Speicherverbrauch im Betrieb ist darin nicht vollständig enthalten.
- Dabei verwendet: FastLED 3.10.5, DFPlayerMini_Fast 1.2.4, FireTimer 1.0.5, Adafruit GFX Library 1.12.6, Adafruit LED Backpack Library 1.5.1 und Adafruit BusIO 1.17.4.
- Gesamter v6.4-Sketch inklusive Bluetooth-Adapter mit nachgebildeten Arduino-/ESP32-/BLE-Schnittstellen als C++17 mit `-Wall -Wextra -Werror` übersetzt.
- 17 vorhandene Spieltests bestanden.
- Vier zusätzliche BLE-Szenarien bestanden: Freigabe, Einstellungsrouten, zerstückelte/überlange Befehle und Verbindungslebenszyklus.
- Tatsächliche Java-Protokollklasse kompiliert und mit 267 Prüfungen getestet, einschließlich UTF-8 über 64 verschiedene Paketgrößen.
- Alle drei Java-Quelldateien durch den Java-Parser auf Syntax geprüft.

**Noch nicht durchgeführt:** grafischer Test auf Android oder Emulator sowie Funk-/Hardwaretest. Die erfolgreichen Builds und Simulationen ersetzen keine Prüfung von Verbindung, Ton, Heap-Belegung, UART-/Funk-Timings und Verdrahtung am echten Gerät.

Die Testskripte und Ergebnisse liegen in `tests`. `tests/run-tests.sh` wiederholt die lokalen Prüfungen. Nur die echte Firmware aus `firmware` auf den ESP32 laden; die nachgebildeten Schnittstellen unter `tests` sind keine Gerätebibliotheken.

## Technische Referenzen

- Android Bluetooth-Berechtigungen: https://developer.android.com/develop/connectivity/bluetooth/bt-permissions
- Android GATT-Callbacks: https://developer.android.com/reference/android/bluetooth/BluetoothGattCallback
- AGP 8.9 / Gradle 8.11.1 / JDK 17: https://developer.android.com/build/releases/agp-8-9-0-release-notes
- ESP32 Arduino BLE UART-Beispiel: https://github.com/espressif/arduino-esp32/tree/3.3.0/libraries/BLE/examples/UART
