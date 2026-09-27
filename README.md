# The Butcher – Android-Projekt und Sketch

`firmware/The_Butcher_v5_4/The_Butcher_v5_4.ino` ist der vollständige neue Sketch für ESP32 Arduino Core 3.3.x. Benötigt: ArduinoJson 6.x, ESPAsyncWebServer und AsyncTCP. Gegenüber v5.3 speichert er Konfigurationen im eigenen NVS-Bereich `butcher54`; frühere gespeicherte Einstellungen werden deshalb nicht automatisch übernommen. Die Hardware-Pins bleiben entsprechend v5.3.

Die native Android-App nutzt WLAN und die JSON-Schnittstelle des ESP32, keine Web-UI. Beim ersten Start erzeugt der ESP32 einen individuellen AP-Namen und ein individuelles Passwort. Beides wird am USB-Seriellmonitor (115200 Baud) angezeigt. Verbinde das Telefon damit und trage die IP des ESP32 in der App ein. Im Heimnetz die dort vergebene ESP32-IP eingeben.

Zum Bau: In Android Studio diesen Ordner öffnen, Gradle synchronisieren und `Build > Build APK(s)` wählen. Benötigt Android SDK 35, JDK 17 und Android Gradle Plugin 8.7.3. GitHub Actions baut nach dem Push eine Debug-APK als Workflow-Artefakt. Eine vorab geprüfte APK ist nicht enthalten.

Es wurde hier weder ein ESP32-Kompilat noch ein Test am echten Gerät ausgeführt. Prüfe die Verdrahtung und den E-Stop vor dem Anschluss von Relais, Pumpe und Aktoren. Der E-Stop sollte zusätzlich die Aktorversorgung hardwareseitig unterbrechen. GPIO12 ist ein Boot-Strapping-Pin und hängt am bisherigen K2-Relaisanschluss; externe Beschaltung darf den Startpegel nicht stören.

