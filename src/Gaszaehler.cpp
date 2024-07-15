#include <Arduino.h> // auskommentieren, für Arduino IDE
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <sstream>
#include <iomanip>

const char *mqtt_topic_gas = "gas_meter/volume";
const char *mqtt_topic_currentVal = "gas_meter/currentVal";

// Pin-Definitionen
#define REED_PIN 32 // ADC1 Pin
#define BUTTON_1 35
#define BUTTON_2 0
#define DEBOUNCE_DELAY 300
#define HYSTERESIS_LOW 500.0
#define HYSTERESIS_HIGH 4000.0

// Variablen
volatile uint32_t pulseCount = 0;
uint32_t gasVolume = 0;
uint32_t offset = 0;
int displayMode = 0;
volatile bool lastState = false;

// Zeitvariablen
unsigned long lastPublishTime = 0;
unsigned long lastButtonCheckTime = 0;
unsigned long lastSaveTime = 0;
unsigned long lastButton1PressTime = 0;
unsigned long lastButton2PressTime = 0;
unsigned long lastInterruptTime = 0;
unsigned long lastMQTTreconnectTime = 0;
unsigned long lastWiFireconnectTime = 0;

// Intervalle
const unsigned long publishInterval = 60000;        // 60 Sekunden
const unsigned long InterruptInterval = 50;         // 50 Millisekunden
const unsigned long buttonCheckInterval = 100;      // 100 Millisekunden
const unsigned long saveInterval = 60000;           // 60 Sekunden
const unsigned long MQTTreconnectIntervall = 10000; // 240 Sekunden
const unsigned long WiFireconnectIntervall = 10000; // 240 Sekunden

// Display
TFT_eSPI tft = TFT_eSPI();

// WiFi- und MQTT-Clients

char mqtt_server[40] = "192.168.0.1";
char mqtt_port[6] = "1883";

WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiClient espClient;
PubSubClient client(espClient);

// Vorherige Werte
unsigned long prevPulseCount = 0;
float prevGasVolume = -1.0;
float prevOffset = -1.0;

// Funktion zur Formatierung von uint32_t mit Tausendertrennzeichen
String formatWithHundredsSeparator(uint32_t value)
{
  std::ostringstream oss;
  oss.imbue(std::locale(""));
  oss << std::fixed << std::setprecision(2) << (value / 100.0);
  return String(oss.str().c_str());
}

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
        Serial.printf("Impulse geladen: %i\n", pulseCount);
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
        Serial.printf("Offset geladen: %s m3\n", formatWithHundredsSeparator(offset).c_str());
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
    gasVolume = pulseCount + offset;
    tft.printf("Gas: %s m3", formatWithHundredsSeparator(gasVolume).c_str());
    tft.setCursor(0, 30);
    tft.printf("Offset: %s m3", formatWithHundredsSeparator(offset).c_str());
    tft.setCursor(0, 60);
    tft.printf("Impulse: %i", pulseCount);
    tft.setCursor(0, 110);
    tft.print("    publish & save >");
    break;
  case 1:
    tft.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "Verbunden" : "Getrennt");
    tft.setCursor(0, 30);
    tft.printf("SSID: %s", WiFi.SSID().c_str());
    tft.setCursor(0, 60);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    tft.setCursor(0, 110);
    tft.print(" reset wifi & mqtt >");
    break;
  case 2:
    tft.printf("MQTT: %s", client.connected() ? "Verbunden" : "Getrennt");
    tft.setCursor(0, 30);
    tft.printf("IP: %s", custom_mqtt_server.getValue());
    break;
  }
}

// Funktion zur Veröffentlichung des Gasvolumens über MQTT
void publishGasVolume()
{
  if (client.connected())
  {
    char msg[50];
    snprintf(msg, 50, "%s", formatWithHundredsSeparator(gasVolume).c_str());
    client.publish(mqtt_topic_gas, msg);
    Serial.printf("Gasvolumen veröffentlicht: %s m3\n", formatWithHundredsSeparator(gasVolume));
  }
  else
  {
    Serial.printf("Veröffentlichen nicht möglich! MQTT nicht verbunden.\n");
  }
}

// Funktion zur Wiederverbindung mit dem MQTT-Broker
void setup_mqtt()
{
  Serial.print("Versuche MQTT-Verbindung...\n");

  // Abrufen der individuellen Chip-ID des ESP32
  String chipId = String((uint32_t)ESP.getEfuseMac(), HEX);
  chipId.toUpperCase();

  // Aufbau des Client-Namens mit der Chip-ID
  String clientId = "Gaszaehler_" + chipId;

  if (client.connect(clientId.c_str()))
  {
    Serial.println("verbunden");
    client.subscribe(mqtt_topic_currentVal);
    publishGasVolume();
  }
  else
  {
    Serial.println("Verbindung zu MQTT-Server fehlgeschlagen.");
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
      if (displayMode == 0)
      {
        saveCountToSPIFFS();
        saveOffsetToSPIFFS();
        publishGasVolume();
        updateDisplay();
      }
      if (displayMode == 1)
      {
        saveCountToSPIFFS();
        saveOffsetToSPIFFS();
        publishGasVolume();
        updateDisplay();
        wm.resetSettings();
        delay(20);
        ESP.restart();
      }

      lastButton2PressTime = currentMillis;
    }
  }
}

// Callback-Funktion für MQTT-Nachrichten
void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  if (String(topic) == mqtt_topic_currentVal)
  {
    offset = static_cast<uint32_t>(message.toFloat() * 100);
    Serial.printf("Zählerstand empfangen: %s m3\n", message.c_str());
    Serial.printf("Offset errechnet: %s m3\n", formatWithHundredsSeparator(offset).c_str());

    pulseCount = 0;

    updateDisplay();
    saveCountToSPIFFS();
    saveOffsetToSPIFFS();
    publishGasVolume();
  }
}

void saveParamsCallback()
{
  Serial.println("Get Params:");
  Serial.print(custom_mqtt_server.getID());
  Serial.print(" : ");
  Serial.println(custom_mqtt_server.getValue());
}

// Setup-Funktion
void setup()
{
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  Serial.begin(115200);
  Serial.println("Starte Gaszähler ...");

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.setConfigPortalBlocking(false);
  wm.setSaveParamsCallback(saveParamsCallback);

  if (wm.autoConnect("GaszaehlerAP"))
  {
    Serial.println("connected...yeey :)");
  }
  else
  {
    Serial.println("Configportal running");
  }

  // SPIFFS initialisieren
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS-Initialisierung fehlgeschlagen!");
    return;
  }

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

  // Initialen Gasvolumenwert setzen
  loadCountFromSPIFFS();
  loadOffsetFromSPIFFS();
  updateDisplay();

  Serial.println("Setup abgeschlossen.");
}

// Hauptschleife
void loop()
{
  wm.process();
  unsigned long currentMillis = millis();

  // MQTT-Verbindung prüfen
  if (WiFi.status() == WL_CONNECTED && !client.connected() && currentMillis - lastMQTTreconnectTime >= MQTTreconnectIntervall)
  {
    lastMQTTreconnectTime = currentMillis;

    // MQTT-Client einrichten

    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());

    client.setServer(mqtt_server, static_cast<uint16_t>(std::stoi(mqtt_port)));
    client.setCallback(callback);

    setup_mqtt();
  }

  // MQTT loop()-Funktion
  client.loop();

  // Reed-Kontakt analog lesen und Hysterese anwenden
  float voltage = analogRead(REED_PIN);
  if (currentMillis - lastInterruptTime >= InterruptInterval)
  {
    lastInterruptTime = currentMillis;
    if (lastState && voltage <= HYSTERESIS_LOW)
    {
      lastState = false;
    }
    else if (!lastState && voltage > HYSTERESIS_HIGH)
    {
      Serial.printf("Impuls registriert.\n");
      lastState = true;
      pulseCount++;

      updateDisplay();
    }
  }

  // MQTT Veröffentlichung
  if (currentMillis - lastPublishTime >= publishInterval)
  {
    publishGasVolume();
    lastPublishTime = currentMillis;
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