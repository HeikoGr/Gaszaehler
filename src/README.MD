## Überblick des Gaszähler-Projekts
Das TTGO-T ESP32-Board mit integriertem Display kann für einen intelligenten Gaszähler verwendet werden, der Impulse von einem Reed-Schalter zählt und die Daten über MQTT veröffentlicht. Dieses Projekt kombiniert Hardware-Schnittstellen, drahtlose Konnektivität und Datenprotokollierung zu einer IoT-Lösung für die Gasverbrauchsüberwachung.
### Hauptfunktionen:
Erfassung von Impulsen eines Reed-Schalter-Sensors am Gaszähler
Anzeige der Gasverbrauchsdaten auf dem integrierten TFT-Bildschirm
Veröffentlichung von Gasverbrauchsmetriken an einen MQTT-Broker
Konfigurierbare WLAN- und MQTT-Einstellungen über WiFiManager
Dauerhafte Speicherung von Zählerdaten mittels SPIFFS
Tastenbedienung für verschiedene Anzeigemodi und Konfiguration
### MQTT und WiFiManager
Das Projekt nutzt MQTT zur Veröffentlichung von Gasverbrauchsdaten und WiFiManager für eine einfache WLAN-Konfiguration. MQTT ermöglicht die Echtzeitübertragung von Daten an einen Broker, was eine Fernüberwachung des Gasverbrauchs ermöglicht. WiFiManager bietet eine benutzerfreundliche Weboberfläche zur Einrichtung von WLAN-Zugangsdaten und MQTT-Parametern ohne Hardcodierung.
### Vorteile:
Erhöhte Flexibilität des Geräts
Funktionsfähigkeit in verschiedenen Netzwerkumgebungen
Sicheres und effizientes Datenmeldesystem
Automatische Wiederverbindung bei Netzwerkproblemen
Konfigurierbare Veröffentlichungsintervalle
### SPIFFS-Speicher
SPIFFS (SPI Flash File System) wird verwendet, um kritische Daten wie den Impulszähler dauerhaft im Flash-Speicher des ESP32 zu speichern. Dies gewährleistet, dass der Gaszählerstand auch bei Stromausfall oder Geräteneustart erhalten bleibt.
## Implementierungsdetails:
Speichern und Laden des Zählerstands aus einer JSON-Datei in SPIFFS
Schreibvorgänge nur bei Wertänderungen zur Minimierung des Flash-Verschleißes
Möglichkeit zur Speicherung von Konfigurationseinstellungen für einfache Updates
### Hysterese und Entprellung
Die Implementierung von Hysterese und Entprellung ist entscheidend für eine genaue Impulszählung vom Reed-Schalter.
### Hysterese:
Verhindert Fehlauslösungen durch kleine Spannungsschwankungen
Verwendet zwei Schwellenwerte: einen niedrigeren für Schließung und einen höheren für Öffnung
### Entprellung:
Filtert schnelle Ein-Aus-Übergänge durch mechanisches Prellen des Schalters
Typischerweise Verwendung einer Zeitverzögerung von 50-100ms zwischen Zustandsänderungen
Diese Methoden werden in Software unter Verwendung von analogen Spannungsmessungen und zeitbasierten Prüfungen implementiert. Die Kombination von Hysterese und Entprellung ermöglicht eine hohe Genauigkeit bei der Impulszählung und minimiert Fehler durch elektrisches Rauschen oder mechanische Vibrationen.
Durch diese Techniken kann der Gaszähler präzise Messungen durchführen und zuverlässige Daten liefern, selbst unter nicht idealen Bedingungen.
## WiFi-Konfiguration
1. Kopieren Sie `wifi_credentials_template.h` zu `wifi_credentials.h`
2. Öffnen Sie `wifi_credentials.h` und geben Sie Ihre WiFi-Zugangsdaten ein