#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Button2.h>
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

// Version
const char *const version = "V 0.0.1";

// Pin definitions
#define REED_PIN 32 // ADC1 pin
#define BUTTON_1 35
#define BUTTON_2 0
#define HYSTERESIS_LOW 500.0
#define HYSTERESIS_HIGH 4000.0

// Time intervals
constexpr unsigned long PUBLISH_INTERVAL = 1 * 60 * 1000;        // 60 seconds
constexpr unsigned long INTERRUPT_INTERVAL = 50;                 // 50 milliseconds
constexpr unsigned long SAVE_INTERVAL = 10 * 60 * 10000;         // 10 minutes
constexpr unsigned long WIFI_RECONNECT_INTERVAL = 1 * 20 * 1000; // 20 seconds
constexpr unsigned long MQTT_RECONNECT_INTERVAL = 1 * 30 * 1000; // 30 seconds

// MQTT Topics
const char *const mqtt_topic_gas = "gas_meter/volume";
const char *const mqtt_topic_currentVal = "gas_meter/currentVal";

// State values
uint32_t offset = 0;
char mqtt_server[40] = "10.5.0.240";
char mqtt_port[6] = "1883";
char mqtt_user[40] = "mqtt";
char mqtt_password[40] = "foobar";
uint32_t pulseCount = 0;

struct ConnectionStatus
{
    bool wifiConnected = false;
    bool mqttConnected = false;
    bool prevWifiStatus = false;
    bool prevMqttStatus = false;
};

struct TimeStamps
{
    volatile unsigned long lastPublishTime = 0;
    volatile unsigned long lastSaveTime = 0;
    volatile unsigned long lastInterruptTime = 0;
    volatile unsigned long lastMQTTreconnectTime = 0;
    volatile unsigned long lastWiFiconnectTime = 0;
};

ConnectionStatus connectionStatus;
TimeStamps timeStamps;

uint32_t gasVolume = 0;
volatile bool lastState = false;
uint32_t prevPulseCount = 0;
uint32_t prevOffset = 0;
int displayMode = 0;
char prevMqttServer[40];
char prevMqttPort[6];

// set gas meter manually
long number = 0; // Verwendet long für größeren Wertebereich
int cursorPosition = 0;
const int maxDigits = 9; // 6 Vorkomma + Dezimalpunkt + 2 Nachkomma

// Display
TFT_eSPI tft = TFT_eSPI();
String chipID;
String clientID;

// WiFi Manager
WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_user("username", "mqtt username", mqtt_user, 40);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);

// PubSub (MQTT)
WiFiClient espClient;
PubSubClient client(espClient);

// Button2 instances
Button2 button1;
Button2 button2;

// Setup function
void setup()
{
    Serial.begin(115200);
    Serial.println("Starting gas meter " + String(version));

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
        if (spiffsManager.loadData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password))
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
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_password);

    wm.setConfigPortalBlocking(false);
    wm.setSaveParamsCallback(WMsaveParamsCallback);
    wm.setConfigPortalTimeout(60);

    if (wm.autoConnect("GaszaehlerAP"))
    {
        Serial.println("connected...yeey :)");
        timeStamps.lastWiFiconnectTime = millis();
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

    // init Button2
    button1.begin(BUTTON_1, INPUT_PULLUP, true);
    button2.begin(BUTTON_2, INPUT_PULLUP, true);

    // set Button2 handler
    button1.setTapHandler(handleButton1Click);
    button2.setClickHandler(handleButton2Click);
    button2.setLongClickDetectedHandler(handleButton2LongPress);
    button2.setLongClickTime(400);

    reconnect_mqtt();
    updateDisplay();
    timeStamps.lastMQTTreconnectTime = millis();

    Serial.println("Setup completed.");
}

// Main loop
void loop()
{
    connectionStatus.wifiConnected = (WiFi.status() == WL_CONNECTED);
    connectionStatus.mqttConnected = client.connected();

    if (!connectionStatus.wifiConnected && millis() - timeStamps.lastWiFiconnectTime >= WIFI_RECONNECT_INTERVAL)
    {
        WiFi.reconnect();
        timeStamps.lastWiFiconnectTime = millis();
    }

    if (connectionStatus.wifiConnected != connectionStatus.prevWifiStatus || connectionStatus.mqttConnected != connectionStatus.prevMqttStatus)
    {
        updateDisplay();
    }

    wm.process();

    // Check MQTT connection
    if (connectionStatus.wifiConnected && !connectionStatus.mqttConnected && millis() - timeStamps.lastMQTTreconnectTime >= MQTT_RECONNECT_INTERVAL)
    {
        reconnect_mqtt();
    }
    else
    {
        client.loop();
    }

    // Read reed contact analog and apply hysteresis
    float voltage = analogRead(REED_PIN);
    if (millis() - timeStamps.lastInterruptTime >= INTERRUPT_INTERVAL)
    {
        timeStamps.lastInterruptTime = millis();
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

    button1.loop();
    button2.loop();

    if (millis() - timeStamps.lastPublishTime >= PUBLISH_INTERVAL)
    {
        publishGasVolume(); // MQTT publishing
    }
    if (millis() - timeStamps.lastSaveTime >= SAVE_INTERVAL)
    {
        saveDataToSPIFFS(); // SPIFFS saving
    }

    connectionStatus.prevWifiStatus = connectionStatus.wifiConnected; // Update previous status
    connectionStatus.prevMqttStatus = connectionStatus.mqttConnected; // Update previous status
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
    spiffsManager.saveData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password);
    timeStamps.lastSaveTime = millis();
}

// Function to reconnect to the MQTT broker
boolean reconnect_mqtt()
{
    // Set up MQTT client
    client.setServer(mqtt_server, static_cast<uint16_t>(std::stoi(mqtt_port)));
    client.setCallback(MQTTcallbackReceive);
    Serial.printf("Trying to connect to MQTT server %s:%s:%s:%s ... \n", mqtt_server, mqtt_port, mqtt_user, mqtt_password);
    if (client.connect(clientID.c_str(), mqtt_user, mqtt_password))
    {
        Serial.printf("MQTT connected to %s\n", mqtt_server);
        String mqttTopic = clientID + "/" + mqtt_topic_currentVal;
        client.subscribe(mqttTopic.c_str());
    }
    else
    {
        Serial.printf("Failed to connect to MQTT server. Error: %i", client.state());
    }
    timeStamps.lastMQTTreconnectTime = millis();
    return client.connected();
}

// Function to publish gas volume via MQTT
void publishGasVolume()
{
    timeStamps.lastPublishTime = millis();
    if (!client.connected())
    {
        Serial.printf("Publishing not possible! MQTT not connected.\n");
        return;
    }
    gasVolume = pulseCount + offset;
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

void drawStatusBar(const String &title)
{
    tft.fillRect(0, 0, 240, 27, TFT_DARKGREY);  // Status bar
    tft.fillRect(0, 115, 240, 1, TFT_DARKGREY); // Accent line
    tft.setCursor(2, 3);
    tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
    tft.setTextSize(3);
    tft.print(title);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
}

void drawWifiStatus()
{
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    uint16_t wifiColor = wifiConnected ? TFT_GREEN : TFT_RED;
    tft.fillRect(188, 1, 24, 24, wifiColor);
    tft.pushImage(188, 1, 24, 24, wifiIcon, TFT_WHITE);
    connectionStatus.prevWifiStatus = wifiConnected; // Update previous status
}

void drawMqttStatus()
{
    bool mqttConnected = client.connected();
    uint16_t mqttColor = mqttConnected ? TFT_GREEN : TFT_RED;
    tft.fillRect(215, 1, 24, 24, mqttColor);
    tft.pushImage(215, 1, 24, 24, mqttIcon, TFT_WHITE);
    connectionStatus.prevMqttStatus = mqttConnected; // Update previous status
}

void updateDisplay()
{
    String title, line1, line2, actionBtn2 = "";
    switch (displayMode)
    {
    case 1:
        title = "Wifi";
        line1 = "SSID:\n " + WiFi.SSID();
        line2 = "IP address:\n " + WiFi.localIP().toString();
        actionBtn2 = "reset wifi & mqtt >";
        break;
    case 2:
        title = "MQTT";
        line1 = "IP :\n " + String(mqtt_server);
        line2 = "device name :\n " + clientID;
        break;
    case 3:
        title = "misc.";
        line1 = "Version: " + String(version);
        line2 = "";
        actionBtn2 = " edit meter value >";
        break;
    case 4:
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 10);
        tft.print("             next >");
        tft.setCursor(30, 60);

        char buffer[15];
        sprintf(buffer, "%06ld.%02ld  save", number / 100, number % 100);
        tft.print(buffer);

        // Cursor anzeigen
        int xPos;
        xPos = 30 + cursorPosition * 12;
        if (cursorPosition == 8)
        {
            xPos += 36; // Für den Dezimalpunkt
            tft.drawRect(xPos, 77, 46, 2, TFT_RED);
            tft.setCursor(0, 120);
            tft.print("             save >");
            return;
        }
        else if (cursorPosition > 5)
        {
            xPos += 12; // Für den Dezimalpunkt
        }
        tft.drawRect(xPos, 77, 10, 2, TFT_RED);
        tft.setCursor(0, 120);
        tft.print("               +1 >");
        return;
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
    drawStatusBar(title);
    drawWifiStatus();
    drawMqttStatus();

    tft.setCursor(2, 35);
    tft.print(line1);
    tft.setCursor(2, 75);
    tft.print(line2);
    tft.setCursor(0, 120);
    tft.print(actionBtn2);
}

void incrementDigit()
{
    long multiplier = 1;
    for (int i = 0; i < (7 - cursorPosition); i++)
    {
        multiplier *= 10;
    }

    long digit = (number / multiplier) % 10;
    digit = (digit + 1) % 10;
    number = (number / (multiplier * 10)) * (multiplier * 10) + digit * multiplier + number % multiplier;

    updateDisplay();
}

void moveCursor()
{
    cursorPosition = (cursorPosition + 1) % maxDigits;
    updateDisplay();
}

void handleButton1Click(Button2 &btn)
{
    if (displayMode == 4)
    {
        moveCursor();
    }
    else
    {
        number = pulseCount + offset;
        cursorPosition = 0;
        displayMode = (displayMode + 1) % 4;
        updateDisplay();
    }
    return;
}

void handleButton2Click(Button2 &btn)
{
    switch (displayMode)
    {
    case 0: // same as 1
    case 1:
        reconnect_mqtt();
        saveDataToSPIFFS();
        publishGasVolume();
        if (displayMode == 1)
        {
            wm.resetSettings();
            delay(20);
            ESP.restart();
        }
        break;
    case 3:
        displayMode = 4;
        updateDisplay();
        break;
    case 4:
        if (cursorPosition == 8)
        {
            pulseCount = 0;
            offset = number;
            displayMode = 0;
            saveDataToSPIFFS();
            publishGasVolume();
        }
        else
        {
            incrementDigit();
        }
        updateDisplay();
        break;
    default:
        saveDataToSPIFFS();
        publishGasVolume();
        break;
    }
    return;
}

void handleButton2LongPress(Button2 &btn)
{
    Serial.println("Button 2 long press detected");
    captureAndSendScreenshotRLE(tft);
}

// Callback function for saving WiFiManager parameters
void WMsaveParamsCallback()
{
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_user, custom_mqtt_user.getValue());
    strcpy(mqtt_password, custom_mqtt_password.getValue());
    Serial.printf("Got MQTT params from WifiManager: %s:%s:%s:%s\n", mqtt_server, mqtt_port, mqtt_user, mqtt_password);
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