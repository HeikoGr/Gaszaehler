## Überblick des Gaszähler-Projekts
Das TTGO-T ESP32-Board mit integriertem Display kann für einen Standard Balken-Gaszähler (BK-G4) verwendet werden, der Impulse von einem Reed-Schalter zählt und die Daten über MQTT veröffentlicht. Dieses Projekt kombiniert Hardware-Schnittstellen, drahtlose Konnektivität und Datenprotokollierung zu einer IoT-Lösung für die Gasverbrauchsüberwachung.

Grundlage für dieses Projekt ist das 3d gedruckte Gehäuse von 3dFabrik von hier https://www.thingiverse.com/thing:5594161

Der Quellcode und große Teile dieses Textes sind mit künstlicher Intelligenz erstellt worden, um einen Anfang zu haben.

# TODO:
- Mommentan funktioniert das Script nur, wenn WiFi und MQTT verbunden sind. Beides sollte sinnvollerweise optional sein.
- evtl sollte der beliebte WiFi Manager von tzapu integriert werden, dann könnte ich ein vorkompiliertes binary zur Verfügung stellen, wenn WiFi und MQTT Server on the fly editiert werden können.
- Bessere Dokumentation (Offset kann und sollte per MQTT gesetzt werden).
- Sinnvolle Funktion für den zweiten Button.
- Das Farbdisplay nutzen.
- Mehr sinnvolle Ausgaben auf dem Display
- Skript für Zähler mit 1000 Impulsen je m³-Gas anpassen

## Benutzung
Am einfachsten kann das Script mit VSCODE / PlatformIO genutzt werden. Dort einfach das Projekt importieren, WiFi und MQTT Angaben ergänzen und auf ein TTGO T-Display schreiben.

Eine Kompilierung mit der Arduino IDE ist auch möglich. hierzu muss die erste Zeile auskommentiert werden ("#include <Arduino.h>") und das Script in Gaszaehler.ino umbenannt werden.

### Hauptfunktionen:
- Erfassung von Impulsen eines Reed-Schalter-Sensors am Gaszähler 
- Anzeige der Gasverbrauchsdaten auf dem integrierten TFT-Bildschirm 
- Veröffentlichung von Gasverbrauchsmetriken an einen MQTT-Broker
- Konfigurierbare WLAN- und MQTT-Einstellungen 
- Dauerhafte Speicherung von Zählerdaten mittels SPIFFS 
- Tastenbedienung für verschiedene Anzeigemodi und Konfiguration

### MQTT
Das Projekt nutzt MQTT zur Veröffentlichung von Gasverbrauchsdaten. 
Das Skript erfasst lediglich die entdeckten Impulse. Der aktuelle Zählerstand wird durch einen Offset errechnet. 

Der Offset selbst wird gebildet, indem per MQTT den aktuellen Stand des zu überwachenden Gaszählers publiziert (mit PUNKT als Tausendertrennzeichen).

### SPIFFS-Speicher
SPIFFS (SPI Flash File System) wird verwendet, um kritische Daten wie den Impulszähler dauerhaft im Flash-Speicher des ESP32 zu speichern. Dies gewährleistet, dass der Gaszählerstand auch bei Stromausfall oder Geräteneustart erhalten bleibt.

## Implementierungsdetails:
- Speichern und Laden des Zählerstands aus einer JSON-Datei in SPIFFS
- Schreibvorgänge nur bei Wertänderungen zur Minimierung des Flash-Verschleißes
- Möglichkeit zur Speicherung von Konfigurationseinstellungen für einfache Updates

### Hysterese und Entprellung
Die Implementierung von Hysterese und Entprellung ist entscheidend für eine genaue Impulszählung vom Reed-Schalter.
Die Hysterese verhindert Fehlauslösungen durch kleine Spannungsschwankungen und verwendet zwei Schwellenwerte: einen niedrigeren für Schließung und einen höheren für Öffnung.

### Entprellung:
Filtert schnelle Ein-Aus-Übergänge durch mechanisches Prellen des Schalters Typischerweise Verwendung einer Zeitverzögerung von 50-100ms zwischen Zustandsänderungen Diese Methoden werden in Software unter Verwendung von analogen Spannungsmessungen und zeitbasierten Prüfungen implementiert. Die Kombination von Hysterese und Entprellung ermöglicht eine hohe Genauigkeit bei der Impulszählung und minimiert Fehler durch elektrisches Rauschen oder mechanische Vibrationen.

Durch diese Techniken kann der Gaszähler präzise Messungen durchführen und zuverlässige Daten liefern, selbst unter nicht idealen Bedingungen.
## WiFi-Konfiguration
1. Kopiere `wifi_credentials_template.h` zu `wifi_credentials.h`
2. Öffne `wifi_credentials.h` und gib deine WiFi-Zugangsdaten und deinen MQTT Server ein.

## Alternativen
https://github.com/StefanStrobel/ArduCounter/ zählt an mehr als einem PIN, unterstützt auch das Display und ist stark mit FHEM (fhem.de) verknüpft. Leider keine Unterstützung für MQTT.