# Gaszähler-Projekt

Das TTGO-T ESP32-Board mit integriertem Display kann für einen Standard Balgen-Gaszähler (BK-G4) verwendet werden, der Impulse von einem Reed-Schalter zählt und die Daten über MQTT veröffentlicht. Dieses Projekt kombiniert Hardware-Schnittstellen, drahtlose Konnektivität und Datenprotokollierung zu einer IoT-Lösung für die Gasverbrauchsüberwachung.

Grundlage für dieses Projekt ist das 3D-gedruckte Gehäuse von 3dFabrik: [Thingiverse Link](https://www.thingiverse.com/thing:5594161)

## Changelog
- **V 0.0.1**: Wifi reconnect hinzugefügt

## Hauptfunktionen
- Erfassung von Impulsen eines Reed-Schalter-Sensors am Gaszähler
- Anzeige der Gasverbrauchsdaten auf dem integrierten TFT-Bildschirm
- Veröffentlichung von Gasverbrauchsmetriken an einen MQTT-Broker
- Konfigurierbare WLAN- und MQTT-Einstellungen
- Dauerhafte Speicherung von Zählerdaten mittels SPIFFS
- Tastenbedienung für verschiedene Anzeigemodi und Konfiguration

## Voraussetzungen
- Balgengaszähler mit Magnetimpulsgeber
- Gehäuse (z.B. [Thingiverse Link](https://www.thingiverse.com/thing:5594161))
- Reedkontakt
- 1k-Ohm Widerstand
- Lilygo TTGO T-Display ([Produktlink](https://www.lilygo.cc/products/lilygo%C2%AE-ttgo-t-display-1-14-inch-lcd-esp32-control-board?variant=42159376433333))

## Hardware
Ich habe den Reed-Kontakt entgegen vieler (online) Empfehlungen NICHT gegen GND, sondern gegen 3,3 Volt geschaltet.

Das Schaltbild sieht also so aus:

<img src="img/shematic.png" width="240">

GPIO32 wird mit einem 1k-Ohm Pull-Down Widerstand mit Ground verbunden. Wenn der Reedkontakt mit einem Magnet geschlossen wird, hat GPIO32 eine Verbindung zu den 3,3 Volt. Das Gaszähler-Skript erkennt den Sprung auf 3,3 Volt und zählt einen Impuls (nur auf der ansteigenden Flanke, wenn der Zähler bei geschlossenem Kontakt stehen bleibt, sollte dies kein Problem darstellen).

## Kompilieren
Am einfachsten kann das Script mit VSCODE / PlatformIO genutzt werden. Dort einfach das Projekt importieren, WiFi und MQTT Angaben ergänzen und auf ein TTGO T-Display überspielen.

Eine Kompilierung mit der Arduino IDE ist auch möglich. Hierzu muss die erste Zeile auskommentiert werden (`#include <Arduino.h>`) und das Script in `Gaszaehler.ino` umbenannt werden.

## Benutzung
### Display
Das Display kann 4 verschiedene Seiten anzeigen, welche mit dem oberen Taster durchgeschaltet werden:

1. **Gaszähler**: Hier wird der aktuelle Zählerstand angezeigt.

   <img src="img/screenshot01.png" width="240">

2. **Wifi**: Hier wird die SSID des Wifi-Netzwerks sowie die aktuelle IP-Adresse angezeigt. Mit der unteren Taste können die Wifi-Einstellungen zurückgesetzt werden. Der ESP32-Gaszähler erstellt dann ein eigenes Wifi mit dem Namen "Gaszaehler-AP" (ohne Passwort). Hier kann dann das eigene Wifi-Netzwerk sowie der MQTT-Server eingetragen werden.

   <img src="img/screenshot02.png" width="240">

3. **MQTT**: Hier wird die IP-Adresse des MQTT-Servers sowie der Gerätename des Gaszählers angezeigt. Der MQTT-Server kann derzeit nur über das Wifi-Captive Portal geändert werden.

   <img src="img/screenshot03.png" width="240">

4. **Misc.**: Zeigt die Version des Gaszähler-Scripts und bietet die Möglichkeit, den Bildschirm zum Ändern des Gaszählerstands aufzurufen.

   <img src="img/screenshot04.png" width="240">

5. **Zählerstand anpassen**: Der Zählerstand kann ganz ohne MQTT auch direkt am Display eingestellt werden.

   <img src="img/screenshot05.png" width="240">

### Zählerstand anpassen
- Der aktuelle Gaszählerstand kann über das Topic `<GERÄTENAME>/gas_meter/currentVal` verändert werden.
- Alternativ mit der oberen Taste zum vierten Bildschirm wechseln und dort den Zählerstand verändern und speichern. Ein Abbrechen ist nicht möglich. Entweder korrekt erfassen oder den ESP32 reseten (Button/Strom).

Der Gaszähler wird dann den internen Impulszähler auf "0" setzen und die folgenden Impulse zum gespeicherten Gaszählerwert addiert ausgeben.

### SPIFFS-Speicher
In regelmäßigen Abständen wird der aktuelle Zählerstand sowie die Wifi- und MQTT-Konfigurationsdaten gespeichert. Das Speichern kann auf dem Startbildschirm auch mit dem unteren Button erzwungen werden.

### MQTT
Das Projekt nutzt MQTT zur Veröffentlichung von Gasverbrauchsdaten.

## TODO
- Skript für Zähler mit 1000 Impulsen je m³-Gas anpassen
- Bedienung überarbeiten
- Layout überarbeiten?
- Sicherheitsrückfrage bei Wifi/MQTT reset?

## Alternativen
[ArduCounter](https://github.com/StefanStrobel/ArduCounter/) zählt an mehr als einem PIN, unterstützt auch das Display und ist stark mit FHEM (fhem.de) verknüpft. Leider keine Unterstützung für MQTT.
