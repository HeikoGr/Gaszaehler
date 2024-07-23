#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include <sstream>
#include <iomanip>

// own files
#include "icons.h"
#include "SPIFFSManager.h"
#include "functions.h" // Include the header file
#include "screenshot.h"

// Global variables and constants
SPIFFSManager spiffsManager;

// Pin definitions
#define REED_PIN 32 // ADC1 pin
#define BUTTON_1 35
#define BUTTON_2 0
#define DEBOUNCE_DELAY 300 // Debounce delay in milliseconds
#define HYSTERESIS_LOW 500.0
#define HYSTERESIS_HIGH 4000.0

// Time intervals
const unsigned long publishInterval = 60000;         // 60 seconds
const unsigned long InterruptInterval = 50;          // 50 milliseconds
const unsigned long buttonCheckInterval = 100;       // 100 milliseconds
const unsigned long saveInterval = 600000;           // 10 minutes
const unsigned long WiFireconnectIntervall = 300000; // 5 minutes
const unsigned long MQTTreconnectIntervall = 60000;  // 1 minute

// MQTT Topics
const char *const mqtt_topic_gas = "gas_meter/volume";
const char *const mqtt_topic_currentVal = "gas_meter/currentVal";

// State values
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
const unsigned long longPressDuration = 1000; // Duration for long press (in milliseconds)
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

// Setup function
void setup()
{
    Serial.begin(115200);
    Serial.println("Starting gas meter...");

    // Retrieve the individual chip ID of the ESP32
    chipID = String((uint32_t)ESP.getEfuseMac(), HEX);
    chipID.toUpperCase();
    // Build the client name with the chip ID
    clientID = "Gaszaehler_" + chipID;

    // Initialize SPIFFS
    if (spiffsManager.begin())
    {
        Serial.println("SPIFFS successfully initialized");
        // Load data
        if (spiffsManager.loadData(pulseCount, offset, mqtt_server, mqtt_port))
        {
            Serial.println("Data successfully loaded");
        }
    }
    else
    {
        Serial.println("SPIFFS initialization failed");
    }

    WiFi.mode(WIFI_STA); // Explicitly set mode, ESP defaults to STA+AP
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.setConfigPortalBlocking(false);
    wm.setSaveParamsCallback(WMsaveParamsCallback);

    if (wm.autoConnect("GaszaehlerAP"))
    {
        Serial.println("connected...yeey :)");
    }
    else
    {
        Serial.println("Config portal running");
    }

    // Initialize display
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    // Initialize reed contact and buttons
    pinMode(REED_PIN, INPUT);
    pinMode(BUTTON_1, INPUT_PULLUP);
    pinMode(BUTTON_2, INPUT_PULLUP);

    reconnect_mqtt();
    updateDisplay();
    lastMQTTreconnectTime = millis();

    Serial.println("Setup completed.");
}

// Main loop
void loop()
{
    unsigned long currentMillis = millis();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool mqttConnected = client.connected();

    if (wifiConnected != prevWifiStatus || mqttConnected != prevMqttStatus)
    {
        updateDisplay();
    }

    wm.process();

    // Check MQTT connection
    if (wifiConnected && !mqttConnected && currentMillis - lastMQTTreconnectTime >= MQTTreconnectIntervall)
    {
        if (reconnect_mqtt())
        {
            // lastMQTTreconnectTime = currentMillis;
        }
    }
    else
    {
        client.loop();
    }

    // Read reed contact analog and apply hysteresis
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
            Serial.printf("Pulse registered.\n");
            lastState = true;
            pulseCount++;
            updateDisplay();
        }
    }

    if (currentMillis - lastPublishTime >= publishInterval) publishGasVolume(); // MQTT publishing
    if (currentMillis - lastButtonCheckTime >= buttonCheckInterval) handleButtons(); // Button handling
    if (currentMillis - lastSaveTime >= saveInterval) saveDataToSPIFFS(); // SPIFFS saving

    prevWifiStatus = wifiConnected; // Update previous status
    prevMqttStatus = mqttConnected; // Update previous status
}

// Function to save the counter value to SPIFFS
void saveDataToSPIFFS()
{
    // Only write if something has changed (better to set a dirty flag?)
    if (pulseCount == prevPulseCount && offset == prevOffset &&
        strcmp(mqtt_server, prevMqttServer) == 0 && strcmp(mqtt_port, prevMqttPort) == 0)
    {
        Serial.println("No new data to save");
        return;
    }
    spiffsManager.saveData(pulseCount, offset, mqtt_server, mqtt_port);
    lastSaveTime = millis();
}

// Function to reconnect to the MQTT broker
boolean reconnect_mqtt()
{
    // Set up MQTT client
    client.setServer(mqtt_server, static_cast<uint16_t>(std::stoi(mqtt_port)));
    client.setCallback(MQTTcallbackReceive);
    Serial.printf("Trying to connect to MQTT server %s:%s ... \n", mqtt_server, mqtt_port);
    if (client.connect(clientID.c_str()))
    {
        Serial.printf("MQTT connected to %s\n", mqtt_server);
        String mqttTopic = clientID + "/" + mqtt_topic_currentVal;
        client.subscribe(mqttTopic.c_str());
    }
    else
    {
        Serial.printf("Failed to connect to MQTT server. Error: %i", client.state());
    }
    lastMQTTreconnectTime = millis();
    return client.connected();
}

// Function to publish gas volume via MQTT
void publishGasVolume()
{
    lastPublishTime = millis();
    if (!client.connected())
    {
        Serial.printf("Publishing not possible! MQTT not connected.\n");
        return;
    }
    char msg[50];
    snprintf(msg, 50, "%s", formatWithHundredsSeparator(gasVolume).c_str());
    String mqttTopic = clientID + "/" + mqtt_topic_gas;
    client.publish(mqttTopic.c_str(), msg);
    Serial.printf("Gas volume published: %s m3\n", formatWithHundredsSeparator(gasVolume).c_str()); 
}

// Function to format uint32_t with thousands separator
String formatWithHundredsSeparator(uint32_t value)
{
    std::ostringstream oss;
    oss.imbue(std::locale(""));
    oss << std::fixed << std::setprecision(2) << (value / 100.0);
    return String(oss.str().c_str());
}

// Function to update the display
void updateDisplay()
{
    String title, line1, line2, actionBtn2 = "";
    switch (displayMode)
    {
    case 1:
        title = "Wifi";
        line1 = "SSID:\n " + WiFi.SSID();
        line2 = "IP address:\n " + WiFi.localIP().toString();
        actionBtn2 = "       reset wifi >";
        break;
    case 2:
        title = "MQTT";
        line1 = "IP :\n " + String(mqtt_server);
        line2 = "device name :\n " + clientID;
        break;
    default:
        gasVolume = pulseCount + offset;
        title = "gas meter";
        line1 = "value: " + formatWithHundredsSeparator(gasVolume) + " m3";
        actionBtn2 = "             save >";
        break;
    }

    tft.setCursor(0, 28);
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 240, 27, TFT_DARKGREY);  // Status bar
    tft.fillRect(0, 115, 240, 1, TFT_DARKGREY); // Accent line

    // Draw WIFI status
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    uint16_t wifiColor = wifiConnected ? TFT_GREEN : TFT_RED;
    tft.fillRect(188, 1, 24, 24, wifiColor);
    tft.pushImage(188, 1, 24, 24, wifiIcon, TFT_WHITE);
    prevWifiStatus = wifiConnected; // Update previous status

    // Draw MQTT status
    bool mqttConnected = client.connected();
    uint16_t mqttColor = mqttConnected ? TFT_GREEN : TFT_RED;
    tft.fillRect(215, 1, 24, 24, mqttColor);
    tft.pushImage(215, 1, 24, 24, mqttIcon, TFT_WHITE);
    prevMqttStatus = mqttConnected; // Update previous status

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

// Function to handle button presses
void handleButtons()
{
    unsigned long currentMillis = millis();
    handleButton1(currentMillis);
    handleButton2(currentMillis);
    lastButtonCheckTime = currentMillis;
}

// Function to handle Button 1 press
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

// Function to handle Button 2 press
void handleButton2(unsigned long currentMillis)
{
    if (digitalRead(BUTTON_2) != LOW)
        return;
    if (currentMillis - lastButton2PressTime <= DEBOUNCE_DELAY)
        return;
    updateDisplay();
    
    //captureAndSendScreenshotRLE(tft);

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

// Callback function for saving WiFiManager parameters
void WMsaveParamsCallback()
{
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    Serial.printf("Got MQTT params from WifiManager: %s:%s\n", mqtt_server, mqtt_port);
    saveDataToSPIFFS();
    reconnect_mqtt();
}

// Callback function for receiving MQTT messages
void MQTTcallbackReceive(char *topic, byte *payload, unsigned int length)
{
    String message;
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    if (String(topic) == clientID + "/" + mqtt_topic_currentVal)
    {
        offset = static_cast<uint32_t>(message.toFloat() * 100);
        Serial.printf("Counter value received: %s m3\n", message.c_str());
        Serial.printf("Calculated offset: %s m3\n", formatWithHundredsSeparator(offset).c_str());
        pulseCount = 0;
        updateDisplay();
        saveDataToSPIFFS();
        publishGasVolume();
    }
}