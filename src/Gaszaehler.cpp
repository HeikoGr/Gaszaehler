#include <Arduino.h> // auskommentieren, für Arduino IDE
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ENTWEDER:
#include "credentials.h"

// ODER: WiFi-Zugangsdaten
// const char* ssid = "ssid";
// const char* password = "password";
// const char *mqtt_server = "192.168.1.99";

// MQTT topics
const char *mqtt_topic_gas = "gas_meter/volume";
const char *mqtt_topic_offset = "gas_meter/offset";

// Pin-Definitionen
#define REED_PIN 32 // ADC1 Pin
#define BUTTON_1 35
#define BUTTON_2 0
#define DEBOUNCE_DELAY 250
#define HYSTERESIS_LOW 500.0   // 30% von VCC
#define HYSTERESIS_HIGH 4000.0 // 70% von VCC

// Variablen
volatile unsigned long pulseCount = 0;
float gasVolume = 0.0;
float offset = 0.0;
int displayMode = 0;
volatile bool lastState = false;

// Zeitvariablen
unsigned long lastPublishTime = 0;
unsigned long lastDisplayUpdateTime = 0;
unsigned long lastButtonCheckTime = 0;
unsigned long lastSaveTime = 0;
unsigned long lastButton1PressTime = 0;
unsigned long lastButton2PressTime = 0;
unsigned long lastInterruptTime = 0;

// Intervalle
const unsigned long publishInterval = 60000;      // 60 Sekunden
const unsigned long InterruptInterval = 250;      // 60 Sekunden
const unsigned long displayUpdateInterval = 1000; // 1 Sekunde
const unsigned long buttonCheckInterval = 50;     // 50 Millisekunden
const unsigned long saveInterval = 60000;         // 60 Sekunden

// Display
TFT_eSPI tft = TFT_eSPI();

// WiFi- und MQTT-Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Vorherige Werte
unsigned long prevPulseCount = 0;
float prevGasVolume = -1.0;
float prevOffset = -1.0;

// Funktion zum Speichern des Zählerstands in SPIFFS
void saveCountToSPIFFS()
{
  if (pulseCount != prevPulseCount)
  {
    fs::File file = SPIFFS.open("/count.json", FILE_WRITE);
    if (file)
    {
      JsonDocument doc;
      doc["count"] = pulseCount;
      serializeJson(doc, file);
      file.close();
      prevPulseCount = pulseCount;
      Serial.println("Zählerstand gespeichert");
    }
  }
}

// Funktion zum Speichern des Offsets in SPIFFS
void saveOffsetToSPIFFS()
{
  if (offset != prevOffset)
  {
    fs::File file = SPIFFS.open("/offset.json", FILE_WRITE);
    if (file)
    {
      JsonDocument doc;
      doc["offset"] = offset;
      serializeJson(doc, file);
      file.close();
      prevOffset = offset;
      Serial.println("Offset gespeichert");
    }
  }
}

// Funktion zum Laden des Zählerstands aus SPIFFS
void loadCountFromSPIFFS()
{
  Serial.println("Versuche den Zählerstand zu laden.");
  if (SPIFFS.exists("/count.json"))
  {
    fs::File file = SPIFFS.open("/count.json", FILE_READ);
    if (file)
    {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, file);
      if (!error)
      {
        pulseCount = doc["count"];
        prevPulseCount = pulseCount;
        Serial.printf("Zählerstand geladen: %.3i\n", pulseCount);
      }
      file.close();
    }
  }
}

// Funktion zum Laden des Offsets aus SPIFFS
void loadOffsetFromSPIFFS()
{
  Serial.println("Versuche den Offset zu laden.");
  if (SPIFFS.exists("/offset.json"))
  {
    fs::File file = SPIFFS.open("/offset.json", FILE_READ);
    if (file)
    {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, file);
      if (!error)
      {
        offset = doc["offset"];
        prevOffset = offset;
        Serial.printf("Offset geladen: %.3f\n", offset);
      }
      file.close();
    }
  }
}

// Funktion zur Aktualisierung des Displays
void updateDisplay()
{
  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);
  switch (displayMode)
  {
  case 0:
    tft.printf("Gas: %.3f m3", gasVolume);
    tft.setCursor(0, 30);
    tft.printf("Offset: %.3f m3", offset);
    break;
  case 1:
    tft.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "Verbunden" : "Getrennt");
    tft.setCursor(0, 30);
    tft.printf("SSID: %s", WiFi.SSID().c_str());
    tft.setCursor(0, 60);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    break;
  case 2:
    tft.printf("MQTT: %s", client.connected() ? "Verbunden" : "Getrennt");
    tft.setCursor(0, 30);
    tft.printf("IP: %s", mqtt_server);
    break;
  }
}

// Funktion zur Aktualisierung des Displays, falls sich Werte geändert haben
void updateDisplayIfChanged()
{
  gasVolume = pulseCount / 1000.0 + offset;
  if (gasVolume != prevGasVolume || offset != prevOffset)
  {
    updateDisplay();
    prevGasVolume = gasVolume;
    prevOffset = offset;
  }
}

// Funktion zur Behandlung der Tasten
void handleButtons()
{
  unsigned long currentMillis = millis();

  if (digitalRead(BUTTON_1) == LOW)
  {
    if (currentMillis - lastButton1PressTime > DEBOUNCE_DELAY)
    {
      displayMode = (displayMode + 1) % 3;
      updateDisplay();
      lastButton1PressTime = currentMillis;
    }
  }
  if (digitalRead(BUTTON_2) == LOW)
  {
    if (currentMillis - lastButton2PressTime > DEBOUNCE_DELAY)
    {
      // pulseCount = 0; 
      saveCountToSPIFFS();
      updateDisplay();
      lastButton2PressTime = currentMillis;
    }
  }
}

// Funktion zur Veröffentlichung des Gasvolumens über MQTT
void publishGasVolume()
{
  char msg[50];
  snprintf(msg, 50, "%.3f", gasVolume);
  client.publish(mqtt_topic_gas, msg);
  Serial.printf("Gasvolumen veröffentlicht: %.3f m3\n", gasVolume);
}

// Funktion zur Einrichtung der WiFi-Verbindung
void setup_wifi()
{
  delay(10);
  Serial.println();
  Serial.print("Verbinde mit ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi verbunden");
  Serial.println("IP-Adresse: ");
  Serial.println(WiFi.localIP());
}

// Callback-Funktion für MQTT-Nachrichten
void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  if (String(topic) == mqtt_topic_offset)
  {
    offset = message.toFloat();
    Serial.printf("Neuer Offset-Wert empfangen: %.3f m3\n", offset);
    saveOffsetToSPIFFS();
  }
}

// Funktion zur Wiederverbindung mit dem MQTT-Broker
void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Versuche MQTT-Verbindung...");
    if (client.connect("ESP32Client"))
    {
      Serial.println("verbunden");
      client.subscribe(mqtt_topic_offset);
    }
    else
    {
      Serial.print("fehlgeschlagen, rc=");
      Serial.print(client.state());
      Serial.println(" Versuche es in 5 Sekunden erneut");
      delay(5000);
    }
  }
}

// Setup-Funktion
void setup()
{
  Serial.begin(115200);
  Serial.println("Starte Gaszähler ...");

  // SPIFFS initialisieren
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS-Initialisierung fehlgeschlagen!");
    return;
  }

  loadCountFromSPIFFS();
  loadOffsetFromSPIFFS();

  // Display initialisieren
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  // Reed-Kontakt-Pin initialisieren
  pinMode(REED_PIN, INPUT);

  // Buttons initialisieren
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);

  // Mit WiFi verbinden
  setup_wifi();

  // MQTT-Client einrichten
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Initialen Gasvolumenwert setzen
  gasVolume = pulseCount / 1000.0 + offset;

  Serial.println("Setup abgeschlossen.");
}

// Hauptschleife
void loop()
{
  unsigned long currentMillis = millis();

  // MQTT-Verbindung prüfen
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  // Reed-Kontakt analog lesen und Hysterese anwenden
  float voltage = analogRead(REED_PIN); // Angenommen 3.3V VCC
  if (currentMillis - lastInterruptTime >= InterruptInterval)
  {
    lastInterruptTime = millis();
    if (lastState && voltage <= HYSTERESIS_LOW)
    {
      lastState = false;
    }
    else if (!lastState && voltage > HYSTERESIS_HIGH)
    {
      lastState = true;
      pulseCount++;
    }
  }

  // MQTT Veröffentlichung
  if (currentMillis - lastPublishTime >= publishInterval)
  {
    publishGasVolume();
    lastPublishTime = currentMillis;
  }

  // Display-Aktualisierung
  if (currentMillis - lastDisplayUpdateTime >= displayUpdateInterval)
  {
    updateDisplayIfChanged();
    lastDisplayUpdateTime = currentMillis;
  }

  // Button-Überprüfung
  if (currentMillis - lastButtonCheckTime >= buttonCheckInterval)
  {
    handleButtons();
    lastButtonCheckTime = currentMillis;
  }

  // SPIFFS-Speicherung
  if (currentMillis - lastSaveTime >= saveInterval)
  {
    saveCountToSPIFFS();
    lastSaveTime = currentMillis;
  }
}