# The Butcher v5.5 BLE

Android-App und vollständiger ESP32-Sketch für Bluetooth Low Energy. Die App steuert K1–K8, Zeitpläne, Loop, Effekte, AutoFX, WLAN-Einstellungen und Defaults ohne Web-UI. Der bisherige WLAN-Zugang bleibt als Service-Schnittstelle erhalten.

## Installation

1. `firmware/The_Butcher_v5_5_BLE/The_Butcher_v5_5_BLE.ino` auf ein ESP32 Dev Module mit Arduino Core 3.3.x flashen. Bibliotheken: ArduinoJson 6.21.5, ESPAsyncWebServer 3.12.1 und ESP32Async/AsyncTCP 3.4.8.
2. `TheButcher_1.2-beta.apk` auf Android installieren und Bluetooth erlauben.
3. App öffnen, **The Butcher suchen**, Gerät auswählen. Am ESP32 den Reset-Taster auf GPIO18 **3 Sekunden halten**, um genau diese Verbindung freizugeben. Für einen Neustart den Reset-Taster **8 Sekunden halten und loslassen**.
4. Anschließend zeigt die App den Status und alle Einstellungsbereiche. Die Freigabe ist nach einer Trennung erneut nötig.

Die Show-Einstellungen aus v5.4 liegen weiterhin im NVS-Bereich `butcher54`. Ein vorhandener v5.3-Speicher wird nicht automatisch übernommen. Die Pins entsprechen v5.3.

GitHub Actions kompiliert App und Firmware. Ein erfolgreiches Kompilat ersetzt keinen Gerätetest. Prüfe insbesondere die Relaiszuordnung und den physischen E-Stop ohne angeschlossene Aktoren. Der E-Stop sollte deren Versorgung unabhängig vom ESP32 unterbrechen. GPIO12 ist ein Boot-Strapping-Pin an einem der bisherigen Relaisanschlüsse.

## Gestaltung

Android-App 1.2-beta mit mkrativ-Branding, Halloween-Motiv und getrennten Bereichen für Show, Kanäle, Zeitplan, Effekte und WLAN. Das Motiv liegt in `app/src/main/res/drawable/butcher_bg.jpg`.
