# Crocosauf 40 LEDs – Installation und Änderungen

Stand 04.10.2026. Firmware v6.6-BLE-40LED-APPONLY, Android-App 1.1.

## Richtig installieren

1. Die komplette Datei `Crocosauf_Deluxe_v6_6_BLE_40LED_APPONLY.ino` öffnen. Arduino darf dafür einen gleichnamigen Ordner anlegen. Es ist ein vollständiger Sketch; nichts an den alten Sketch anhängen. Keine zusätzlichen Projekt-Header erforderlich.
2. Board **ESP32 Dev Module**, Espressif-Boardpaket **3.3.7**. Bei deinem WROOM mit 4 MB Flash **Huge APP (3MB No OTA/1MB SPIFFS)** wählen. GPIOs sind unverändert.
3. Bibliotheken: FastLED, DFPlayerMini_Fast, FireTimer, Adafruit GFX Library, Adafruit LED Backpack Library, Adafruit BusIO. Bluetooth kommt mit dem ESP32-Paket.
4. Hochladen, danach die ganze Versorgung einschließlich DFPlayer aus- und einschalten. Die Audioinitialisierung dauert ungefähr 6,5 Sekunden.
5. APK `Crocosauf_1.1.apk` auf Android öffnen und installieren. Falls Android ein Update wegen abweichender Signatur ablehnt: bisherige Crocosauf-App deinstallieren, neue APK installieren. Die Musik- und Geräteeinstellungen auf dem ESP32 bleiben erhalten.
6. App öffnen → **Crocosauf verbinden** → Bluetooth-Berechtigung erlauben → Gerät auswählen. Maul schließen, Welcome abwarten, Seitentaster drei Sekunden halten: **APP OK**. Danach lädt die App die gespeicherten Werte.

Keine App im Browser und keine WLAN-Verbindung nötig. Die APK ist eine signierte Debug-/Testversion, keine Play-Store-Veröffentlichung.

## Einstellungen

Unter **System** findest du Licht & Display, aktuelle Lautstärke/Mute, Audio beim Einschalten, Reminder, Laufschrift, automatischen Schlaf und Gerätestatus. Mit dem jeweiligen Speichern-Button werden Werte übernommen und im Crocosauf gespeichert.

- **LED-Helligkeit:** 0–100 %, 0 = aus. Gilt für alle Spiel-/Pinchen-/Reminderfarben.
- **Displayhelligkeit:** 0–100 %, wobei das HT16K33 in 16 Stufen arbeitet. 0 % ist die dunkelste Stufe, nicht Display aus.
- **Display drehen:** 0°, 90°, 180°, 270°. Erst speichern, dann bei geschlossenem Maul **F zur Ausrichtung anzeigen**. Erneuter Druck beendet den Test; Maul öffnen startet wieder das Spiel.
- **Aktuelle Lautstärke:** 5–30, Mute separat. Diese Bedienung verändert keinen Soundzähler.
- **Audio aktiv:** Gesamten Ton ein-/ausschalten. LEDs, Display und Spiel bleiben nutzbar.
- **Startlautstärke:** 5–30 oder **Spiel startet stumm**. Separat gespeichert; das Speichern übernimmt diesen Pegel auch sofort.
- **Welcome-Lautstärke:** 0–30, unabhängig vom Spiel-Mute. 0 schaltet Welcome stumm. „Audio aktiv“ aus unterdrückt auch Welcome.
- **Reminder:** an/aus, mit/ohne Sound, Intervall 1–60 Minuten, LEDs und Display 1500–30000 ms. Die Sounddatei kann vorher enden; beim Ende oder Abbruch des Reminders wird die Wiedergabe gestoppt.
- **Schlaf:** standardmäßig **Aus**. Optional nach 15/30/60 Minuten ohne Bedienung bei geschlossenem Maul. Solange die App verbunden ist, bleibt das Gerät wach. Zum Verbinden im Schlaf Maul öffnen; das Aufwachen benötigt die Startzeit der Firmware.

Bestehende NVS-Einstellungen aus dem 40-LED-Sketch werden übernommen: `lBr`, `dBr`, `dRot`, `aEn`, `wVol`, `remLed`, Soundmasken und weitere Werte. Kein Factory Reset erforderlich. Nur die neue Schlafeinstellung startet zunächst mit Aus. Die neue getrennte Startlautstärke wird beim ersten Laden aus dem bisher gespeicherten Pegel/Mute übernommen.

## Stillstand nach etwa 30 Minuten

Die Vorlage wechselte nach 30 Minuten in Deep Sleep. Direkt davor rief sie `BLEDevice::deinit(true)` auf. Außerdem wurde beim Schlafen der RTC-Pin deinitialisiert, statt ihn sauber einzurichten; beim Aufwachen fehlte die Rückgabe des Maulsensor-Pins an die normale GPIO-Verarbeitung. Der sichtbare Fehler wurde nicht an Marcels Gerät reproduziert, aber diese Wege passen zum Zeitpunkt und zur fehlenden Reaktion auf das Öffnen.

Die neue Version lässt Schlaf standardmäßig aus. Optionaler Schlaf richtet Sensor und Timer mit Fehlerprüfung ein, gibt den RTC-Pin beim Start wieder frei und benutzt keine vollständige BLE-Deinitialisierung. Eine zusätzliche Timerkontrolle erfolgt spätestens nach 60 Sekunden Schlaf; sie hält den verbleibenden Reminder-Zeitpunkt fest. Ein offenes Maul hält das Gerät beim Aufwachen aktiv.

Displayzugriffe haben 25 ms I2C-Timeout. Ein nicht erreichbares Display wird nicht fortlaufend beschrieben und alle fünf Sekunden erneut geprüft. Nach Wiederkehr wird es mit gespeicherter Ausrichtung/Helligkeit neu initialisiert. Die Hauptschleife wird von einem 12-Sekunden-Watchdog überwacht. Bei einem echten Stillstand kann dieser einen Neustart auslösen; dabei geht die laufende Runde verloren. Das ist eine Absicherung, kein Nachweis, dass die Ursache bereits am Gerät beseitigt ist.

## Sounds und Pinchen

- Welcome kommt ausschließlich aus `/03`, GameOver aus `/02`, Reminder `/04`, Neue Runde `/05`. Die Root-Spielmusik verwendet weiter den globalen DFPlayer-Dateiindex; der hängt auch von der SD-Kopierreihenfolge ab. Optional `GAME_MUSIC_FOLDER=1` verwenden und die Spielmusik nach `/01` kopieren.
- Befehle zum DFPlayer werden mit mindestens 200 ms Abstand gesendet. Die SD wird nach Modulreset explizit ausgewählt. Hardware-Loop-/Alle-Titel-Kommandos entfallen; Spielmusik startet nach Ende mit demselben Titel erneut, mit kurzer Pause.
- GameOver läuft in aufsteigender Reihenfolge durch die Auswahl oder zufällig ohne direkte Wiederholung, sofern mehr als ein Titel aktiv ist. Lautstärkeänderungen setzen die Folge nicht zurück. Das Speichern einer geänderten Auswahl setzt nur deren eigenen Zähler zurück.
- Welcome-Reihenfolge wird über Neustarts gespeichert. Fehlende Wiedergabe/BUSY gibt die Startsperre nach einem begrenzten Timeout frei, damit ohne SD/DFPlayer weitergespielt werden kann.
- Jeder Zehnerdurchgang enthält jedes Pinchen genau einmal. Danach wird neu gemischt; jede Position unterscheidet sich vom vorherigen Durchgang. Grünphase und Neue-Runde-Sound bleiben erhalten. Die LED-Paare stammen unverändert aus dem hochgeladenen 40-LED-Sketch.

| Pinchen | Physische LEDs, ab 1 gezählt | C++-Indizes |
|---|---|---|
| 1 | 3 + 4 | 2, 3 |
| 2 | 7 + 8 | 6, 7 |
| 3 | 12 + 13 | 11, 12 |
| 4 | 15 + 16 | 14, 15 |
| 5 | 18 + 19 | 17, 18 |
| 6 | 22 + 23 | 21, 22 |
| 7 | 25 + 26 | 24, 25 |
| 8 | 28 + 29 | 27, 28 |
| 9 | 32 + 33 | 31, 32 |
| 10 | 37 + 38 | 36, 37 |

## Test am echten Crocosauf

1. Unter System prüfen, dass **40 LEDs** angezeigt wird. Licht z.B. auf 30 %, Display auf 50 % setzen, Drehung mit F prüfen, aus-/einschalten und Speicherung kontrollieren.
2. Welcome nur einen eindeutig erkennbaren Titel aktivieren. Komplett neu einschalten. Anschließend drei GameOver-Titel in Reihenfolge auswählen und sechs Spiele machen; dazwischen die Lautstärke ändern. Danach Zufall prüfen.
3. Zwei vollständige Pinchen-Durchgänge spielen. Pro Runde muss genau das jeweilige LED-Paar blinken; im zweiten Durchgang andere Reihenfolge.
4. **Schlaf Aus** lassen, App trennen/auf dem Handy schließen, Maul schließen und mindestens 45 Minuten laufen lassen. Reminder müssen weiterkommen. Danach Maul öffnen und ein Spiel starten. Ein zusätzlicher Test mit verbunden gebliebener App erlaubt die Kontrolle der Laufzeit.
5. Erst danach optional Schlaf auf 30 Minuten setzen. Nach Eintritt in den Schlaf ist das Display leer. Öffnen muss wecken; wenn die externe Aufweckung ausbleibt, prüft die Timerkontrolle spätestens nach 60 Sekunden erneut. Smartphone-Verbindung im Schlaf ist nicht möglich.

Wenn ein Fehler bleibt: nach dem Neustart/der Wiederverbindung den Gerätestatus fotografieren. Für die genaue Ursache sind zusätzlich die seriellen Meldungen bei **115200 Baud** nützlich. `[AUDIO]` zeigt den gesendeten Ordner-/Titelauftrag, nicht die vom DFPlayer bestätigte Datei. `[BOOT]` meldet den Resetgrund, `[SLEEP]` den geplanten Schlaf.

## Prüfung und Grenzen

Automatisierte Tests decken Spielabläufe, BLE-Freigabe/Übertragung, Titelwahl, Original-LED-Paare, Speicherung/Migration, zwei Stunden simulierten Idle-Betrieb, Displayausfall/-wiederkehr, fehlerhafte Wake-Konfiguration, wiederholte Timer-Wakes und 30-Sekunden-Reminder ab. Native ESP32- und Android-Builds sowie der Android-Oberflächentest laufen im zugehörigen GitHub-Workflow. Das Buildprotokoll ist maßgeblich für deren Ergebnis. Ein realer Langzeittest am angeschlossenen Crocosauf und ein Hörtest sind hier nicht möglich.

Technische Grundlagen: [ESP32 Schlafmodi](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/sleep_modes.html), [Watchdog](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html), [I2C-Timeout](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html).
