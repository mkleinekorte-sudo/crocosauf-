# Crocosauf Deluxe v6.5-BLE – Sound und Pinchen

Stand: 04.10.2026. Für den klassischen ESP32 DevKit V1 / WROOM mit 20 LEDs.

## Installation

Die vollständige Datei `Crocosauf_Deluxe_v6_5_BLE/Crocosauf_Deluxe_v6_5_BLE.ino` öffnen oder ihren gesamten Inhalt in einen leeren Arduino-Sketch kopieren. Keine Teile an den alten Sketch anhängen. Diese Fassung enthält die Bluetooth-Steuerung und benötigt keine separaten Projekt-Header.

Board: **ESP32 Dev Module**. Boardpaket: **Espressif ESP32 3.3.7**. Bei 4 MB Flash: **Huge APP (3MB No OTA/1MB SPIFFS)**. Bibliotheken bleiben FastLED, DFPlayerMini_Fast, FireTimer, Adafruit GFX Library, Adafruit LED Backpack Library und Adafruit BusIO.

Die bestehende App 1.0-beta bleibt kompatibel. Soundmasken, Zufall/Reihenfolge, Lautstärke und weitere Einstellungen werden übernommen. Kein Factory Reset erforderlich. Nach dem Upload einmal die gesamte Versorgung einschließlich DFPlayer aus- und einschalten. Die DFPlayer-Initialisierung dauert jetzt ungefähr 6,5 Sekunden, anschließend startet der Welcome-Sound. Maul und App werden danach wie bisher bedient.

## Was korrigiert wurde

- **Zwei LEDs pro Pinchen:** Die Ankerpositionen der im Chat geposteten v6.2 sind erhalten; jeweils die benachbarte LED ergänzt das Paar. In jener Vorlage war tatsächlich nur eine LED eingeschaltet. Die Hardwarezuordnung hier nimmt benachbarte Paare am 20-LED-Streifen an.
- **GameOver:** Nur ausgewählte Titel aus `/02`. Reihenfolge läuft aufsteigend durch die Auswahl und beginnt danach wieder vorne. Zufall verhindert bei mindestens zwei aktivierten Titeln eine direkte Wiederholung. Bei nur einem aktivierten Titel ist die Wiederholung korrekt.
- **Lautstärke:** Verändert ausschließlich den Pegel, ohne die Titelzähler zu verändern. Änderungen an einer Soundauswahl setzen nur deren eigenen Zähler zurück; das Speichern unveränderter Auswahl setzt ihn nicht zurück.
- **Welcome:** Nur ausgewählte Titel aus `/03`. Bei mehreren Titeln wird der Welcome-Zähler für Reihenfolge auch über Aus-/Einschalten gespeichert. Tests in der App verbrauchen den Zähler nicht.
- **Audio-Kommandos:** Initialisierung mit Reset und expliziter SD-Auswahl; danach serialisierte Befehle mit mindestens 200 ms Abstand. Keine Hardware-Loop-/Alle-Titel-Kommandos mehr. Jeder Start nennt den gewünschten Ordner/Titel. Die Spielmusik wird nach Titelende über BUSY mit demselben Titel erneut gestartet; dabei entsteht eine kurze Pause. Einzelwiederholung wird erst während aktiver Wiedergabe deaktiviert.
- **Zehnerdurchgänge:** Alle zehn Pinchen werden vor einem neuen Durchgang gemischt. Jedes kommt genau einmal vor. Im nächsten Durchgang steht jedes Pinchen an einer anderen Rundenposition; auch das zuletzt gezeigte Pinchen wird nicht direkt wieder als erstes gewählt. Nach Runde 10 bleiben grünes Licht, Neue-Runde-Sound aus `/05` und Neustart bei 1 erhalten. Deep Sleep unterbricht die aktuelle Reihenfolge nicht. Die zuletzt erzeugte Folge wird gespeichert, sodass auch ein Neustart eine andere Folge erhält.

## LED-Zuordnung

Gezählt ab **LED 1** am Datenanfang des Streifens; C++ verwendet intern 0–19.

| Pinchen | LEDs |
|---|---|
| 1 | 1 + 2 |
| 2 | 3 + 4 |
| 3 | 5 + 6 |
| 4 | 7 + 8 |
| 5 | 9 + 10 |
| 6 | 11 + 12 |
| 7 | 13 + 14 |
| 8 | 15 + 16 |
| 9 | 17 + 18 |
| 10 | 19 + 20 |

Falls deine mechanische Anordnung andere Paare benötigt, ausschließlich die Tabelle `pinnchenLEDs[10][2]` im Sketch entsprechend anpassen.

## Kurzer Test am Gerät

1. In der App unter Welcome nur einen gut erkennbaren Titel aktivieren, **Auswahl speichern**, komplett aus- und einschalten. Erwartet wird genau diese Datei aus `/03`.
2. GameOver 001, 002 und 003 aktivieren, Zufall ausschalten, speichern. Sechs Spiele durchführen: erwartet 001, 002, 003, 001, 002, 003. Zwischendurch Lautstärke ändern; die Folge muss weiterlaufen.
3. Zufall einschalten und speichern. Es dürfen nur aktivierte GameOver-Titel kommen, keiner zweimal unmittelbar hintereinander, sofern mindestens zwei aktiviert sind.
4. Pinchenmodus: zehn Spiele durchführen. In jeder Cyanphase müssen zwei zusammengehörige LEDs blinken. Kein Pinchen doppelt innerhalb des Durchgangs. Danach grünes Licht und Sound; im nächsten Durchgang andere Reihenfolge.

## Falls weiterhin ein falscher Sound zu hören ist

Seriellmonitor auf **115200 Baud** öffnen. Jede tatsächlich gesendete Titelauswahl wird beispielsweise als `[AUDIO] /02/003.mp3 | Lautstaerke 20` ausgegeben. Diese Zeile zeigt den gesendeten Auftrag, nicht eine vom Modul bestätigte Dateizuweisung. `audioFolder` und `audioTrack` stehen zusätzlich in `/status`.

Wenn Welcome falsch klingt, die zugehörige Datei direkt auf dem PC anhören: `/03/001.mp3` usw. Nummerierte Ordner heißen zweistellig `02`, `03`, `04`, `05`, die Titel dreistellig `001.mp3` usw.; keine doppelten Titelpräfixe innerhalb desselben Ordners. Die Root-Spielmusik verwendet weiterhin den globalen DFPlayer-Dateiindex. Wer stattdessen eine eindeutige Ordnerzuordnung für Spielmusik möchte, kann `GAME_MUSIC_FOLDER` auf `1` setzen und die Spielmusik nach `/01/001.mp3` usw. kopieren.

Die ursprüngliche Ursache der falschen Töne ist ohne UART-/SD-/Hörtest am echten Modul nicht abschließend bewiesen. Die neue Steuerung beseitigt die riskanten Mode-Kommandos und kurzen Startabstände; Dateiinhalte, Verdrahtung und das Verhalten des konkreten DFPlayers sind damit noch nicht physisch geprüft.

## Prüfung

Die lokalen C++-Ablauftests prüfen 17 bestehende Spielszenarien, vier BLE-Szenarien und sieben neue Sound-/Pinchen-Szenarien. Dazu gehören 18 GameOver-Auswahlen je Modus mit Lautstärkeänderungen, gespeicherter Welcome, 12 vollständige Zehnerdurchgänge, 1.000 weitere Mischungen, Deep-Sleep-Fortsetzung, genau zwei Cyan-LEDs und DFPlayer-Befehlsabstände. Die unveränderte Java-Protokollklasse besteht 267 Prüfungen.

Ein echter ESP32-Build wird über GitHub Actions für die vollständige v6.5-Einzeldatei ausgeführt. Das Buildprotokoll ist die maßgebliche Quelle für den Compilerstatus. Simulation und Kompilierung ersetzen keinen Hör- und Funktionstest am Gerät.

Technische Grundlagen: [DFPlayer-Befehle](https://powerbroker2.github.io/DFPlayerMini_Fast/html/class_d_f_player_mini___fast.html), [DFRobot-Protokoll und Startzeiten](https://image.dfrobot.com/image/data/DFR0299/DFPlayer%20Mini%20Manul.pdf), [ESP32-Zufallsquelle](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/random.html).
