#include <Arduino.h> // entfernen für Arduino IDE
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <sstream>
#include <iomanip>

#include "SPIFFSManager.h"

SPIFFSManager spiffsManager;

#include "icons.h"

// Pin-Definitionen
#define REED_PIN 32 // ADC1 Pin
#define BUTTON_1 35
#define BUTTON_2 0
#define DEBOUNCE_DELAY 300
#define HYSTERESIS_LOW 500.0
#define HYSTERESIS_HIGH 4000.0

// Zeit-Intervalle
const unsigned long publishInterval = 60000;         // 60 Sekunden
const unsigned long InterruptInterval = 50;          // 50 Millisekunden
const unsigned long buttonCheckInterval = 100;       // 100 Millisekunden
const unsigned long saveInterval = 600000;           // 10 Minuten
const unsigned long WiFireconnectIntervall = 300000; // 5 Minuten
const unsigned long MQTTreconnectIntervall = 60000;  // 10 Sekunden

// MQTT Topics
const char *const mqtt_topic_gas = "gas_meter/volume";
const char *const mqtt_topic_currentVal = "gas_meter/currentVal";

// Zustandswerte
uint32_t offset = 0;
char mqtt_server[40] = "192.168.0.1";
char mqtt_port[6] = "1883";
uint32_t pulseCount = 0;
uint32_t gasVolume = 0;
volatile bool lastState = false;
uint32_t prevPulseCount = 0;
uint32_t prevOffset = 0;
bool prevWifiStatus = false;
bool prevMqttStatus = false;
int displayMode = 0;
unsigned long lastPublishTime = 0;
unsigned long lastButtonCheckTime = 0;
unsigned long lastSaveTime = 0;
unsigned long lastButton1PressTime = 0;
unsigned long lastButton2PressTime = 0;
unsigned long lastInterruptTime = 0;
unsigned long lastMQTTreconnectTime = 0;
unsigned long lastWiFireconnectTime = 0;
char prevMqttServer[40];
char prevMqttPort[6];

const unsigned long longPressDuration = 1000; // Dauer für langes Drücken (in Millisekunden)
bool button2LongPressActive = false;

// Display
TFT_eSPI tft = TFT_eSPI();

String chipID;
String clientID;

// WiFi Manager
WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);

// PubSub (MQTT)
WiFiClient espClient;
PubSubClient client(espClient);

// Funktion zur Formatierung von uint32_t mit Tausendertrennzeichen
String formatWithHundredsSeparator(uint32_t value)
{
  std::ostringstream oss;
  oss.imbue(std::locale(""));
  oss << std::fixed << std::setprecision(2) << (value / 100.0);
  return String(oss.str().c_str());
}

// Funktion zur Veröffentlichung des Gasvolumens über MQTT
void publishGasVolume()
{
  if (!client.connected())
  {
    Serial.printf("Veröffentlichen nicht möglich! MQTT nicht verbunden.\n");
    return;
  }
  char msg[50];
  snprintf(msg, 50, "%s", formatWithHundredsSeparator(gasVolume).c_str());

  String mqttTopic = clientID + "/" + mqtt_topic_gas;
  client.publish(mqttTopic.c_str(), msg);
  Serial.printf("Gasvolumen veröffentlicht: %s m3\n", formatWithHundredsSeparator(gasVolume).c_str());
}

// Funktion zum Speichern des Zählerstands in SPIFFS
void saveDataToSPIFFS()
{
  // schreibe nur, wenn sich etwas geändert hat (besser dirty flag setzen?)
  if (pulseCount == prevPulseCount &&
      offset == prevOffset &&
      strcmp(mqtt_server, prevMqttServer) == 0 &&
      strcmp(mqtt_port, prevMqttPort) == 0)
  {
    Serial.println("Keine neuen Daten zu speichern");
    return;
  }

  spiffsManager.saveData(pulseCount, offset, mqtt_server, mqtt_port);
}

void saveParamCallback()
{
  Serial.println("[CALLBACK] saveParamCallback fired");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());

  saveDataToSPIFFS();
}

void drawDisplay(String title, String line1, String line2, String actionBtn2)
{
  tft.setCursor(2, 3);
  tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
  tft.setTextSize(3);
  tft.print(title);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(2, 35);
  tft.print(line1);
  tft.setCursor(2, 75);
  tft.print(line2);
  tft.setCursor(0, 120);
  tft.print(actionBtn2);
}

void drawStatus()
{

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  uint16_t wifiColor = wifiConnected ? TFT_GREEN : TFT_RED;
  tft.fillRect(188, 1, 24, 24, wifiColor);
  tft.pushImage(188, 1, 24, 24, wifiIcon, TFT_WHITE);
  prevWifiStatus = wifiConnected; // Aktualisiere vorherigen Status

  bool mqttConnected = client.connected();
  // Wenn sich der MQTT-Status geändert hat, zeichne das Icon neu
  uint16_t mqttColor = mqttConnected ? TFT_GREEN : TFT_RED;
  tft.fillRect(215, 1, 24, 24, mqttColor);
  tft.pushImage(215, 1, 24, 24, mqttIcon, TFT_WHITE);
  prevMqttStatus = mqttConnected; // Aktualisiere vorherigen Status
}

// Funktion zur Aktualisierung des Displays
void updateDisplay()
{
  tft.setCursor(0, 28);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 240, 27, TFT_DARKGREY);  // Statusleiste
  tft.fillRect(0, 115, 240, 1, TFT_DARKGREY); // Akzentlinie

  String title, line1, line2, actionBtn2 = "";

  switch (displayMode)
  {
  case 1:
    title = "Wifi";
    line1 = "SSID:\n " + WiFi.SSID();
    line2 = "IP-Adresse:\n " + WiFi.localIP().toString();
    actionBtn2 = "        reset wifi >";
    break;
  case 2:
    title = "MQTT";
    line1 = "IP :\n " + String(mqtt_server);
    line2 = "Geraetename :\n " + clientID;
    break;
  default:
    gasVolume = pulseCount + offset;
    title = "Gaszaehler";
    line1 = "Gas: " + formatWithHundredsSeparator(gasVolume) + " m3";
    actionBtn2 = "           sichern >";
    break;
  }

  drawDisplay(title, line1, line2, actionBtn2);
  drawStatus();
}

// Callback-Funktion für MQTT-Nachrichten
void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  if (String(topic) == clientID + "/" + mqtt_topic_currentVal)
  {
    offset = static_cast<uint32_t>(message.toFloat() * 100);
    Serial.printf("Zählerstand empfangen: %s m3\n", message.c_str());
    Serial.printf("Offset errechnet: %s m3\n", formatWithHundredsSeparator(offset).c_str());

    pulseCount = 0;

    updateDisplay();
    saveDataToSPIFFS();
    publishGasVolume();
  }
}

// Funktion zur Wiederverbindung mit dem MQTT-Broker
boolean reconnect_mqtt()
{
  // MQTT-Client einrichten
  client.setServer(mqtt_server, static_cast<uint16_t>(std::stoi(mqtt_port)));
  client.setCallback(callback);

  Serial.printf("Versuche MQTT-Verbindung mit %s:%s ... \n", mqtt_server, mqtt_port);

  if (client.connect(clientID.c_str()))
  {
    Serial.printf("MQTT verbunden mit %s\n", mqtt_server);
    String mqttTopic = clientID + "/" + mqtt_topic_currentVal;
    client.subscribe(mqttTopic.c_str());
  }
  else
  {
    Serial.printf("Verbindung zu MQTT-Server fehlgeschlagen. Fehler: %i", client.state());
    lastMQTTreconnectTime = millis();
  }
  return client.connected();
}

void captureAndSendScreenshotRLE()
{
  const uint16_t width = tft.width();
  const uint16_t height = tft.height();
  const uint32_t totalPixels = width * height;

  Serial.println("Start of RLE Compressed Screenshot");
  Serial.printf("Original size: %u bytes\n", totalPixels * 2);

  uint32_t compressedSize = 0;
  uint16_t currentColor = 0;
  uint8_t count = 0;

  for (uint16_t y = 0; y < height; y++)
  {
    for (uint16_t x = 0; x < width; x++)
    {
      uint16_t pixelColor = tft.readPixel(x, y);

      if (pixelColor == currentColor && count < 255)
      {
        count++;
      }
      else
      {
        if (count > 0)
        {
          Serial.printf("%02X%04X", count, currentColor);
          compressedSize += 3;
        }
        currentColor = pixelColor;
        count = 1;
      }
    }
  }

  // Ausgabe des letzten Farbblocks
  if (count > 0)
  {
    Serial.printf("%02X%04X", count, currentColor);
    compressedSize += 3;
  }

  Serial.printf("\nCompressed size: %u bytes\n", compressedSize);
  Serial.println("End of RLE Compressed Screenshot");
}

void handleButton1(unsigned long currentMillis)
{
  if (digitalRead(BUTTON_1) != LOW)
    return;
  if (currentMillis - lastButton1PressTime <= DEBOUNCE_DELAY)
    return;

  displayMode = (displayMode + 1) % 3;
  updateDisplay();
  lastButton1PressTime = currentMillis;
}

void handleButton2(unsigned long currentMillis)
{
  if (digitalRead(BUTTON_2) != LOW)
    return;
  if (currentMillis - lastButton2PressTime <= DEBOUNCE_DELAY)
    return;

  updateDisplay();
  if (displayMode == 0 || displayMode == 1)
  {
    reconnect_mqtt();
    saveDataToSPIFFS();
    publishGasVolume();
  }

  if (displayMode == 1)
  {
    wm.resetSettings();
    delay(20);
    ESP.restart();
  }

  lastButton2PressTime = currentMillis;
}

void handleButtons()
{
  unsigned long currentMillis = millis();

  handleButton1(currentMillis);
  handleButton2(currentMillis);
}

void saveParamsCallback()
{
  Serial.println("Get Params:");
  Serial.print(custom_mqtt_server.getID());
  Serial.print(" : ");
  Serial.println(custom_mqtt_server.getValue());

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());

  saveDataToSPIFFS();
  reconnect_mqtt();
}

// Setup-Funktion
void setup()
{
  // Abrufen der individuellen Chip-ID des ESP32
  chipID = String((uint32_t)ESP.getEfuseMac(), HEX);
  chipID.toUpperCase();

  // Aufbau des Client-Namens mit der Chip-ID
  clientID = "Gaszaehler_" + chipID;

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  Serial.begin(115200);
  Serial.println("Starte Gaszähler ...");

  if (spiffsManager.begin())
  {
    Serial.println("SPIFFS erfolgreich initialisiert");

    // Daten laden
    if (spiffsManager.loadData(pulseCount, offset, mqtt_server, mqtt_port))
    {
      Serial.println("Daten erfolgreich geladen");
      // Verwenden Sie die geladenen Daten hier
    }
  }
  else
  {
    Serial.println("SPIFFS-Initialisierung fehlgeschlagen");
  }

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
  reconnect_mqtt();
  updateDisplay();
  drawStatus();

  Serial.println("Setup abgeschlossen.");
}

// Hauptschleife
void loop()
{
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  bool mqttConnected = client.connected();
  if (wifiConnected != prevWifiStatus || mqttConnected != prevMqttStatus)
  {
    drawStatus();
  }
  prevWifiStatus = wifiConnected; // Aktualisieren Sie den vorherigen Status
  prevMqttStatus = mqttConnected; // Aktualisieren Sie den vorherigen Status

  wm.process();
  unsigned long currentMillis = millis();

  // MQTT-Verbindung prüfen
  if (WiFi.status() == WL_CONNECTED && !client.connected() && currentMillis - lastMQTTreconnectTime >= MQTTreconnectIntervall)
  {
    if (reconnect_mqtt())
    {
      // lastMQTTreconnectTime = currentMillis;
    }
  }
  else
  {
    // MQTT loop()-Funktion
    client.loop();
  }

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
    saveDataToSPIFFS();
    lastSaveTime = currentMillis;
  }
}