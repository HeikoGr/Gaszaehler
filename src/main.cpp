#include <Arduino.h>
#include <WiFi.h>

#include <WiFiManager.h>
#include <Button2.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// own files
#include "Ui.h"
#include "SPIFFSManager.h"
#include "functions.h" // Include the header file
#include "screenshot.h"
// Generated from web/index.html by scripts/embed_web.py (gzip-compressed)
#include "generated/web_index_html.h"

// Global variables and constants
SPIFFSManager spiffsManager;

// Version
const char *const version = "V 0.2.0";

// Pin definitions
#define REED_PIN 32 // ADC1 pin
#define BUTTON_1 35
#define BUTTON_2 0
#define HYSTERESIS_LOW 500.0
#define HYSTERESIS_HIGH 4000.0

// Time intervals
constexpr unsigned long PUBLISH_INTERVAL = 1 * 60 * 1000;        // 60 seconds
constexpr unsigned long INTERRUPT_INTERVAL = 50;                 // 50 milliseconds
constexpr unsigned long SAVE_INTERVAL = 10 * 60 * 1000;          // 10 minutes
constexpr unsigned long WIFI_RECONNECT_INTERVAL = 1 * 20 * 1000; // 20 seconds
constexpr unsigned long MQTT_RECONNECT_INTERVAL = 1 * 30 * 1000; // 30 seconds

// MQTT Topics (mutable so web UI can change them at runtime)
// Default now uses a Home Assistant friendly path under the clientID: clientID/measurement/gas
String mqtt_topic_gas = "measurement/gas";
String mqtt_topic_currentVal = "measurement/current";

// State values
uint32_t offset = 0;
char mqtt_server[40] = "";
char mqtt_port[6] = "1883";
char mqtt_user[40] = "";
char mqtt_password[40] = "";
// Name of the WiFiManager config portal access point
const char *const AP_NAME = "GaszaehlerAP";

// Optional password for the web dashboard and OTA (user "admin"); empty = no login
char web_password[40] = "";
const char *const WEB_USER = "admin";
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
char prevMqttUser[40];
char prevMqttPassword[40];
char prevClientID[64];
char prevMqttTopic[64];
char prevMqttTopicCurrent[64];
char prevWebPassword[40];

// set gas meter manually
long number = 0; // Use long for a larger value range
int cursorPosition = 0; // 0..7 digits, then UI_EDIT_SAVE, UI_EDIT_CANCEL

// Pulse timing for the flow estimate on the display
unsigned long lastPulseMillis = 0;
unsigned long lastPulseInterval = 0;
bool pulseSeen = false;
constexpr unsigned long FLOW_TIMEOUT = 15 * 60 * 1000; // no pulse for 15 min = no flow
constexpr unsigned long DISPLAY_REFRESH_INTERVAL = 30 * 1000;
unsigned long lastDisplayUpdate = 0;
bool prevPortalActive = false;

// Display: everything is drawn into a RAM framebuffer (lib/ui) and pushed in one go -> no flicker
uint16_t *frameBuffer = nullptr;

// Display
TFT_eSPI tft = TFT_eSPI();
String chipID;
String clientID;

// Upper limit for meter values (in 1/100 m3) so that pulseCount + offset never overflows
constexpr uint32_t MAX_METER_VALUE = 99999999; // 999999.99 m3

// Time window for confirming the Wi-Fi reset on the display
constexpr unsigned long WIFI_RESET_CONFIRM_WINDOW = 3000;
unsigned long wifiResetRequestTime = 0;

// Forward declarations
void publishHassDiscovery();
void clearHassDiscovery(const String &id);
void handleRestartRequest();
void updateWebServer();
bool checkAuth();
void saveAndRestart();

// Parse a TCP port, returns 0 if invalid
uint16_t parsePort(const char *str)
{
    if (!str || !*str)
        return 0;
    char *end = nullptr;
    unsigned long val = strtoul(str, &end, 10);
    if (*end != '\0' || val == 0 || val > 65535)
        return 0;
    return static_cast<uint16_t>(val);
}

// Parse a meter value in m3 ("1234.56" or "1234,56", no thousands separators)
// into 1/100 m3. Returns false on invalid or out of range input.
bool parseMeterValue(String str, uint32_t &result)
{
    str.trim();
    str.replace(',', '.');
    if (str.isEmpty())
        return false;
    uint32_t integerPart = 0;
    uint32_t fraction = 0;
    int fractionDigits = -1; // -1: no decimal point seen yet
    for (size_t i = 0; i < str.length(); i++)
    {
        char c = str[i];
        if (c == '.' && fractionDigits < 0)
        {
            fractionDigits = 0;
        }
        else if (isdigit(c))
        {
            if (fractionDigits < 0)
            {
                integerPart = integerPart * 10 + (c - '0');
                if (integerPart > MAX_METER_VALUE / 100)
                    return false;
            }
            else
            {
                if (++fractionDigits > 2)
                    return false;
                fraction = fraction * 10 + (c - '0');
            }
        }
        else
        {
            return false;
        }
    }
    if (fractionDigits == 1)
        fraction *= 10;
    result = integerPart * 100 + fraction;
    return result <= MAX_METER_VALUE;
}

void snapshotPersistentState()
{
    prevPulseCount = pulseCount;
    prevOffset = offset;
    strlcpy(prevMqttServer, mqtt_server, sizeof(prevMqttServer));
    strlcpy(prevMqttPort, mqtt_port, sizeof(prevMqttPort));
    strlcpy(prevMqttUser, mqtt_user, sizeof(prevMqttUser));
    strlcpy(prevMqttPassword, mqtt_password, sizeof(prevMqttPassword));
    strlcpy(prevClientID, clientID.c_str(), sizeof(prevClientID));
    strlcpy(prevMqttTopic, mqtt_topic_gas.c_str(), sizeof(prevMqttTopic));
    strlcpy(prevMqttTopicCurrent, mqtt_topic_currentVal.c_str(), sizeof(prevMqttTopicCurrent));
    strlcpy(prevWebPassword, web_password, sizeof(prevWebPassword));
}

// WiFi Manager
WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_user("username", "mqtt username", mqtt_user, 40);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password (leave empty to keep)", "", 39, "type='password'");
WiFiManagerParameter custom_web_password("webpass", "web UI password, user 'admin' (optional, leave empty to keep)", "", 39, "type='password'");
// PubSub (MQTT)
WiFiClient espClient;
PubSubClient client(espClient);
// MQTT diagnostics
String lastMqttStatus = "never";
unsigned long lastMqttAttemptTime = 0; // millis()
int lastMqttErrorCode = 0;
// Home Assistant discovery published flag
bool hassDiscoveryPublished = false;
// Web server (only runs while the WiFiManager config portal is not active, both use port 80)
WebServer webServer(80);
bool webServerRunning = false;
// Button2 instances
Button2 button1;
Button2 button2;
// Button2 button2;


void setup()
{
    Serial.begin(115200);
    Serial.println("Starting gas meter " + String(version));

    // Retrieve the individual chip ID of the ESP32
    chipID = String((uint32_t)ESP.getEfuseMac(), HEX);
    chipID.toUpperCase();
    // Build the client name with the chip ID
    clientID = "Gaszaehler_" + chipID;

    // Initialize display first so the user sees something while Wi-Fi connects
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true); // framebuffer holds native-endian RGB565
    tft.fillScreen(TFT_BLACK);
    frameBuffer = static_cast<uint16_t *>(malloc(UI_WIDTH * UI_HEIGHT * sizeof(uint16_t)));
    if (frameBuffer)
    {
        UiState boot;
        boot.screen = Screen::Boot;
        boot.version = version;
        Canvas canvas(frameBuffer, UI_WIDTH, UI_HEIGHT);
        renderUi(canvas, boot);
        tft.pushImage(0, 0, UI_WIDTH, UI_HEIGHT, frameBuffer);
    }

    // Initialize SPIFFS
    if (spiffsManager.begin())
    {
        Serial.println("SPIFFS successfully initialized");
        char storedClientID[64] = "";
        char storedTopic[64] = "";
        char storedTopicCurrent[64] = "";
        if (spiffsManager.loadData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password, storedClientID, storedTopic, storedTopicCurrent, web_password))
        {
            Serial.println("Data successfully loaded");
            if (strlen(storedClientID) > 0)
            {
                clientID = String(storedClientID);
                Serial.printf("Loaded clientID: %s\n", clientID.c_str());
            }
            if (strlen(storedTopic) > 0)
            {
                mqtt_topic_gas = String(storedTopic);
                Serial.printf("Loaded mqtt topic base: %s\n", mqtt_topic_gas.c_str());
            }
            if (strlen(storedTopicCurrent) > 0)
            {
                mqtt_topic_currentVal = String(storedTopicCurrent);
                Serial.printf("Loaded mqtt topic current: %s\n", mqtt_topic_currentVal.c_str());
            }
        }
    }
    else
    {
        Serial.println("SPIFFS initialization failed");
    }

    snapshotPersistentState();

    WiFi.mode(WIFI_STA); // Explicitly set mode, ESP defaults to STA+AP

    // Show the stored values in the config portal (the parameters were created with compile-time defaults)
    custom_mqtt_server.setValue(mqtt_server, sizeof(mqtt_server) - 1);
    custom_mqtt_port.setValue(mqtt_port, sizeof(mqtt_port) - 1);
    custom_mqtt_user.setValue(mqtt_user, sizeof(mqtt_user) - 1);
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_password);
    wm.addParameter(&custom_web_password);

    wm.setConfigPortalBlocking(false);
    wm.setSaveParamsCallback(WMsaveParamsCallback);
    wm.setConfigPortalTimeout(300);

    // Allow larger MQTT messages (e.g., HA discovery payloads)
    client.setBufferSize(1024);

    if (wm.autoConnect(AP_NAME))
    {
        Serial.println("connected...yeey :)");
        timeStamps.lastWiFiconnectTime = millis();
    }
    else
    {
        Serial.println("Config portal running");
    }

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

    setupWebInterface();
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

    wm.process();
    updateWebServer();

    bool portalActive = wm.getConfigPortalActive();
    if (connectionStatus.wifiConnected != connectionStatus.prevWifiStatus || connectionStatus.mqttConnected != connectionStatus.prevMqttStatus ||
        portalActive != prevPortalActive || millis() - lastDisplayUpdate >= DISPLAY_REFRESH_INTERVAL)
    {
        prevPortalActive = portalActive;
        updateDisplay();
    }

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
            unsigned long now = millis();
            lastPulseInterval = pulseSeen ? now - lastPulseMillis : 0;
            lastPulseMillis = now;
            pulseSeen = true;
            updateDisplay();
        }
    }

    button1.loop();
    button2.loop();

    // Wi-Fi reset confirmation expired: restore the normal button hint
    if (wifiResetRequestTime != 0 && millis() - wifiResetRequestTime >= WIFI_RESET_CONFIRM_WINDOW)
    {
        wifiResetRequestTime = 0;
        updateDisplay();
    }

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
        strcmp(mqtt_server, prevMqttServer) == 0 && strcmp(mqtt_port, prevMqttPort) == 0 &&
        strcmp(mqtt_user, prevMqttUser) == 0 && strcmp(mqtt_password, prevMqttPassword) == 0 &&
        strcmp(clientID.c_str(), prevClientID) == 0 && strcmp(mqtt_topic_gas.c_str(), prevMqttTopic) == 0 && strcmp(mqtt_topic_currentVal.c_str(), prevMqttTopicCurrent) == 0 &&
        strcmp(web_password, prevWebPassword) == 0)
    {
        Serial.println("No new data to save");
        return;
    }
    if (spiffsManager.saveData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password, (char*)clientID.c_str(), (char*)mqtt_topic_gas.c_str(), (char*)mqtt_topic_currentVal.c_str(), web_password))
    {
        snapshotPersistentState();
        timeStamps.lastSaveTime = millis();
        // If MQTT is connected and discovery not yet published (or topics changed), attempt publishing discovery
        if (client.connected() && !hassDiscoveryPublished) {
            publishHassDiscovery();
        }
    }
}

// Human readable description of PubSubClient::state()
const char *mqttStateText(int state)
{
    switch (state)
    {
    case MQTT_CONNECTION_TIMEOUT: return "connection timeout";
    case MQTT_CONNECTION_LOST: return "connection lost";
    case MQTT_CONNECT_FAILED: return "TCP connect failed";
    case MQTT_DISCONNECTED: return "disconnected";
    case MQTT_CONNECTED: return "connected";
    case MQTT_CONNECT_BAD_PROTOCOL: return "bad protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "bad client ID";
    case MQTT_CONNECT_UNAVAILABLE: return "server unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "bad credentials";
    case MQTT_CONNECT_UNAUTHORIZED: return "unauthorized";
    default: return "unknown error";
    }
}

// Function to reconnect to the MQTT broker
boolean reconnect_mqtt()
{
    lastMqttAttemptTime = millis();
    timeStamps.lastMQTTreconnectTime = millis();

    if (client.connected())
        return true;

    uint16_t port = parsePort(mqtt_port);
    if (strlen(mqtt_server) == 0 || port == 0)
    {
        lastMqttStatus = "not configured";
        lastMqttErrorCode = -1;
        Serial.println("MQTT server or port not configured; skipping MQTT connect");
        return false;
    }

    // Set up MQTT client
    client.setServer(mqtt_server, port);
    client.setCallback(MQTTcallbackReceive);
    Serial.printf("Trying to connect to MQTT server %s:%u (user:%s) ... \n", mqtt_server, port, mqtt_user);

    // Attempt MQTT connect with Last Will set to 'offline' on availability topic
    String availTopic = clientID + "/availability";
    bool connected = client.connect(
        clientID.c_str(),
        strlen(mqtt_user) ? mqtt_user : nullptr,
        strlen(mqtt_password) ? mqtt_password : nullptr,
        availTopic.c_str(),
        1,
        true,
        "offline");

    if (connected)
    {
        lastMqttStatus = "connected";
        lastMqttErrorCode = 0;
        Serial.printf("MQTT connected to %s\n", mqtt_server);
        client.publish(availTopic.c_str(), "online", true);
        String mqttTopic = clientID + "/" + mqtt_topic_currentVal;
        client.subscribe(mqttTopic.c_str());
        publishHassDiscovery();
    }
    else
    {
        lastMqttErrorCode = client.state();
        lastMqttStatus = String(mqttStateText(lastMqttErrorCode)) + " (state=" + String(lastMqttErrorCode) + ")";
        Serial.printf("Failed to connect to MQTT server: %s\n", lastMqttStatus.c_str());
    }

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
    // Human readable (kept for backwards compatibility)
    String humanMsg = formatVolume(gasVolume);
    String mqttTopicHuman = clientID + "/" + mqtt_topic_gas;
    client.publish(mqttTopicHuman.c_str(), humanMsg.c_str());

    // Numeric raw value (Home Assistant friendly) - retained so HA can read it after restarts.
    // Formatted from the integer value; a float would lose the last digit above ~131072 m3.
    String mqttTopicRaw = mqttTopicHuman + "/state";
    bool ok = client.publish(mqttTopicRaw.c_str(), humanMsg.c_str(), true);

    Serial.printf("Gas volume published: %s m3\n", humanMsg.c_str());

    // If discovery hasn't been published yet, try now (first successful publish)
    if (!hassDiscoveryPublished && ok) {
        publishHassDiscovery();
    }
}

// Remove retained discovery/availability messages of a previous client ID,
// otherwise Home Assistant keeps orphaned entities around
void clearHassDiscovery(const String &id)
{
    if (!client.connected())
        return;
    client.publish((String("homeassistant/sensor/") + id + "_gas_volume/config").c_str(), "", true);
    client.publish((String("homeassistant/sensor/") + id + "_current_value/config").c_str(), "", true);
    client.publish((String("homeassistant/number/") + id + "_meter_set/config").c_str(), "", true);
    client.publish((id + "/availability").c_str(), "", true);
    Serial.printf("Cleared Home Assistant discovery for old client ID %s\n", id.c_str());
}

// Publish Home Assistant MQTT discovery payloads for this device
void publishHassDiscovery()
{
    if (!client.connected()) return;

    String baseHuman = clientID + "/" + mqtt_topic_gas; // human readable topic
    String currentTopic = clientID + "/" + mqtt_topic_currentVal;
    String availTopic = clientID + "/availability";

    // Track publish results
    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;

    // Device info block
    JsonDocument device;
    device["name"] = clientID;
    device["sw_version"] = version;
    device["identifiers"].to<JsonArray>().add(clientID);
    device["model"] = "Gaszaehler";
    device["manufacturer"] = "DIY";

    // Sensor: total (cumulative) gas volume
    {
        JsonDocument doc;
        doc["name"] = String(clientID + " Gas Volume");
        doc["unique_id"] = String(clientID + "_gas_volume");
        doc["state_topic"] = baseHuman + "/state";
        doc["unit_of_measurement"] = "m³";
        doc["value_template"] = "{{ value | float }}";
        doc["state_class"] = "total_increasing";
        doc["device_class"] = "gas";
        doc["icon"] = "mdi:fire";
        doc["availability_topic"] = availTopic;
        doc["device"] = device;

        String payload;
        serializeJson(doc, payload);
        String discoveryTopic = String("homeassistant/sensor/") + clientID + "_gas_volume/config";
        Serial.printf("Publishing discovery topic: %s (len=%u)\n", discoveryTopic.c_str(), (unsigned)payload.length());
        Serial.println(payload);
        ok1 = client.publish(discoveryTopic.c_str(), payload.c_str(), true);
        Serial.printf(" -> publish returned: %s\n", ok1 ? "true" : "false");
    }

    // Number: lets Home Assistant set the meter reading (publishes to the correction topic),
    // shows the current total as its state
    {
        JsonDocument doc;
        doc["name"] = String(clientID + " Set Meter Reading");
        doc["unique_id"] = String(clientID + "_meter_set");
        doc["command_topic"] = currentTopic;
        doc["state_topic"] = baseHuman + "/state";
        doc["unit_of_measurement"] = "m³";
        doc["device_class"] = "gas";
        doc["min"] = 0;
        doc["max"] = MAX_METER_VALUE / 100.0;
        doc["step"] = 0.01;
        doc["mode"] = "box";
        doc["entity_category"] = "config";
        doc["icon"] = "mdi:counter";
        doc["availability_topic"] = availTopic;
        doc["device"] = device;

        String payload;
        serializeJson(doc, payload);
        String discoveryTopic = String("homeassistant/number/") + clientID + "_meter_set/config";
        Serial.printf("Publishing discovery topic: %s (len=%u)\n", discoveryTopic.c_str(), (unsigned)payload.length());
        Serial.println(payload);
        ok2 = client.publish(discoveryTopic.c_str(), payload.c_str(), true);
        Serial.printf(" -> publish returned: %s\n", ok2 ? "true" : "false");

        // Remove the sensor entity of firmware <= 0.1.1 that this number replaces
        client.publish((String("homeassistant/sensor/") + clientID + "_current_value/config").c_str(), "", true);
    }
    // Publish availability as online (retain)
    Serial.printf("Publishing availability topic: %s\n", (clientID + "/availability").c_str());
    ok3 = client.publish((clientID + "/availability").c_str(), "online", true);
    Serial.printf(" -> publish returned: %s\n", ok3 ? "true" : "false");

    // Mark discovery published only if all publishes succeeded
    if (ok1 && ok2 && ok3) {
        hassDiscoveryPublished = true;
        Serial.println("Home Assistant discovery published (retained)");
    } else {
        hassDiscoveryPublished = false;
        Serial.println("Home Assistant discovery publish attempt; some messages may have failed");
    }
}

// Format a volume in 1/100 m3 as "12345.67"
String formatVolume(uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu.%02lu", (unsigned long)(value / 100), (unsigned long)(value % 100));
    return String(buf);
}

// Estimated flow in liters per hour (one pulse = 0.01 m3 = 10 l)
uint32_t currentFlowLitersPerHour()
{
    if (!pulseSeen || lastPulseInterval == 0)
        return 0;
    unsigned long sinceLast = millis() - lastPulseMillis;
    if (sinceLast > FLOW_TIMEOUT)
        return 0;
    // If the current gap is already longer than the last interval, the flow has slowed down
    unsigned long interval = max(lastPulseInterval, sinceLast);
    return static_cast<uint32_t>(10ULL * 3600000ULL / interval);
}

void updateDisplay()
{
    lastDisplayUpdate = millis();
    if (!frameBuffer)
        return;

    static const Screen screens[] = {Screen::Gas, Screen::Wifi, Screen::Mqtt, Screen::Info, Screen::Edit};
    static String ssid, ip;
    ssid = WiFi.SSID();
    ip = WiFi.localIP().toString();

    UiState s;
    s.screen = wm.getConfigPortalActive() ? Screen::Portal : screens[constrain(displayMode, 0, 4)];
    s.wifiConnected = (WiFi.status() == WL_CONNECTED);
    s.mqttConnected = client.connected();
    s.volume = pulseCount + offset;
    s.flowLitersPerHour = currentFlowLitersPerHour();
    s.secondsSinceLastPulse = pulseSeen ? (millis() - lastPulseMillis) / 1000 : UI_NO_PULSE;
    s.ssid = ssid.c_str();
    s.ip = ip.c_str();
    s.rssi = WiFi.RSSI();
    s.wifiResetPending = wifiResetRequestTime != 0 && millis() - wifiResetRequestTime < WIFI_RESET_CONFIRM_WINDOW;
    s.mqttServer = mqtt_server;
    s.mqttPort = mqtt_port;
    s.clientId = clientID.c_str();
    s.mqttStatus = lastMqttStatus.c_str();
    s.version = version;
    s.uptimeSeconds = millis() / 1000;
    s.pulseCount = pulseCount;
    s.editValue = number;
    s.editCursor = cursorPosition;
    s.apName = AP_NAME;
    s.apIp = "192.168.4.1";

    Canvas canvas(frameBuffer, UI_WIDTH, UI_HEIGHT);
    renderUi(canvas, s);
    tft.pushImage(0, 0, UI_WIDTH, UI_HEIGHT, frameBuffer);
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
    cursorPosition = (cursorPosition + 1) % UI_EDIT_POSITIONS;
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
        number = min(pulseCount + offset, MAX_METER_VALUE);
        cursorPosition = 0;
        wifiResetRequestTime = 0;
        displayMode = (displayMode + 1) % 4;
        updateDisplay();
    }
    return;
}

void handleButton2Click(Button2 &btn)
{
    switch (displayMode)
    {
    case 0:
        reconnect_mqtt();
        saveDataToSPIFFS();
        publishGasVolume();
        break;
    case 1:
        // Require a second click within the confirmation window to avoid accidental Wi-Fi resets
        if (wifiResetRequestTime != 0 && millis() - wifiResetRequestTime < WIFI_RESET_CONFIRM_WINDOW)
        {
            // Physical access is the recovery path for a forgotten web password
            web_password[0] = '\0';
            wm.resetSettings();
            saveAndRestart();
        }
        wifiResetRequestTime = millis();
        updateDisplay();
        break;
    case 3:
        number = min(pulseCount + offset, MAX_METER_VALUE);
        cursorPosition = 0;
        displayMode = 4;
        updateDisplay();
        break;
    case 4:
        if (cursorPosition == UI_EDIT_SAVE)
        {
            pulseCount = 0;
            offset = number;
            displayMode = 0;
            saveDataToSPIFFS();
            publishGasVolume();
        }
        else if (cursorPosition == UI_EDIT_CANCEL)
        {
            displayMode = 3;
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
    if (frameBuffer)
        captureAndSendScreenshotRLE(frameBuffer, UI_WIDTH, UI_HEIGHT);
}

// Callback function for saving WiFiManager parameters
void WMsaveParamsCallback()
{
    strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
    if (parsePort(custom_mqtt_port.getValue()) != 0)
        strlcpy(mqtt_port, custom_mqtt_port.getValue(), sizeof(mqtt_port));
    strlcpy(mqtt_user, custom_mqtt_user.getValue(), sizeof(mqtt_user));
    // Empty password field means "keep the stored password"
    if (strlen(custom_mqtt_password.getValue()) > 0)
        strlcpy(mqtt_password, custom_mqtt_password.getValue(), sizeof(mqtt_password));
    if (strlen(custom_web_password.getValue()) > 0)
        strlcpy(web_password, custom_web_password.getValue(), sizeof(web_password));
    Serial.printf("Got MQTT params from WifiManager: %s:%s (user: %s)\n", mqtt_server, mqtt_port, mqtt_user);
    saveDataToSPIFFS();
    reconnect_mqtt();
}

// Callback function for receiving MQTT messages
void MQTTcallbackReceive(char *topic, byte *payload, unsigned int length)
{
    String currentTopic = clientID + "/" + mqtt_topic_currentVal;
    if (currentTopic != topic)
        return;
    // Empty payload: our own retained-message cleanup (see below) or someone clearing the topic
    if (length == 0)
        return;

    String message;
    for (unsigned int i = 0; i < length && i < 32; i++)
    {
        message += (char)payload[i];
    }
    Serial.printf("Counter value received: %s m3\n", message.c_str());

    uint32_t newValue;
    if (length > 32 || !parseMeterValue(message, newValue))
    {
        Serial.println("Ignoring invalid counter value");
    }
    else
    {
        offset = newValue;
        pulseCount = 0;
        updateDisplay();
        saveDataToSPIFFS();
        publishGasVolume();
    }
    // Clear a retained correction, otherwise it would be applied again on every reconnect
    // and silently discard all pulses counted since then
    client.publish(currentTopic.c_str(), "", true);
}

void handleRootRequest()
{
    if (!checkAuth())
        return;
    webServer.sendHeader("Content-Encoding", "gzip");
    webServer.send_P(200, "text/html", reinterpret_cast<const char *>(WEB_INDEX_HTML_GZ), WEB_INDEX_HTML_GZ_LEN);
}

void handleStatusRequest()
{
    if (!checkAuth())
        return;
    connectionStatus.wifiConnected = (WiFi.status() == WL_CONNECTED);
    connectionStatus.mqttConnected = client.connected();
    JsonDocument doc;
    uint32_t currentVolume = pulseCount + offset;
    doc["gasVolumeRaw"] = currentVolume;
    doc["gasVolumeM3"] = currentVolume / 100.0;
    doc["gasVolumeFormatted"] = formatVolume(currentVolume);
    doc["mqttConnected"] = connectionStatus.mqttConnected;
    doc["mqttServer"] = mqtt_server;
    doc["mqttPort"] = mqtt_port;
    doc["mqttUser"] = mqtt_user;
    doc["maskedPassword"] = strlen(mqtt_password) ? "********" : "";
    doc["wifiConnected"] = connectionStatus.wifiConnected;
    doc["uptimeSeconds"] = millis() / 1000;
    doc["version"] = version;
    doc["clientID"] = clientID;
    doc["mqttTopicGas"] = String(clientID + "/" + mqtt_topic_gas);
    doc["mqttTopicCurrent"] = String(clientID + "/" + mqtt_topic_currentVal);
    doc["mqttTopicBase"] = mqtt_topic_gas;
    doc["mqttTopicCurrentBase"] = mqtt_topic_currentVal;
    doc["offset"] = offset;
    doc["pulseCount"] = pulseCount;
    doc["mqttLastStatus"] = lastMqttStatus;
    doc["mqttLastAttemptUptime"] = static_cast<uint32_t>(timeStamps.lastMQTTreconnectTime / 1000);
    doc["mqttLastError"] = lastMqttErrorCode;

    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
}

void sendJsonError(int code, const char *message)
{
    JsonDocument doc;
    doc["error"] = message;
    String payload;
    serializeJson(doc, payload);
    webServer.send(code, "application/json", payload);
}

// Optional login: returns true if no web password is set or the request carries valid credentials,
// otherwise asks the browser for credentials (Digest auth, password never sent in plain text)
bool checkAuth()
{
    if (strlen(web_password) == 0 || webServer.authenticate(WEB_USER, web_password))
        return true;
    webServer.requestAuthentication(DIGEST_AUTH, "Gaszaehler");
    return false;
}

// CSRF protection for state-changing requests: the dashboard sends a custom header,
// which a foreign website cannot add without a CORS preflight (never granted here).
// If the browser sends an Origin header, it must also match the Host.
bool isTrustedRequest()
{
    if (webServer.header("X-Requested-With") != "gaszaehler")
        return false;
    String origin = webServer.header("Origin");
    if (origin.length() > 0 && origin != "http://" + webServer.hostHeader())
        return false;
    return true;
}

bool rejectUntrustedRequest()
{
    if (!checkAuth())
        return true;
    if (isTrustedRequest())
        return false;
    Serial.println("Rejected request without valid X-Requested-With/Origin header");
    sendJsonError(403, "forbidden");
    return true;
}

// MQTT topic segment(s) entered by the user: no wildcards, no empty levels, fits the storage buffer
bool isValidTopic(const String &topic)
{
    if (topic.isEmpty() || topic.length() >= 64)
        return false;
    if (topic.indexOf('+') >= 0 || topic.indexOf('#') >= 0 || topic.indexOf("//") >= 0)
        return false;
    return !topic.startsWith("/") && !topic.endsWith("/");
}

void handleConsumptionUpdate()
{
    if (rejectUntrustedRequest())
        return;
    if (!webServer.hasArg("value"))
    {
        sendJsonError(400, "value missing");
        return;
    }
    uint32_t scaled;
    if (!parseMeterValue(webServer.arg("value"), scaled))
    {
        sendJsonError(400, "invalid value (expected e.g. 12345.67, max 999999.99)");
        return;
    }
    if (scaled >= pulseCount)
    {
        offset = scaled - pulseCount;
    }
    else
    {
        offset = scaled;
        pulseCount = 0;
    }

    updateDisplay();
    saveDataToSPIFFS();
    publishGasVolume();

    JsonDocument doc;
    doc["status"] = "ok";
    doc["value"] = scaled / 100.0;
    doc["gasVolumeFormatted"] = formatVolume(pulseCount + offset);
    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
}

void handleMqttConfigUpdate()
{
    if (rejectUntrustedRequest())
        return;
    if (!webServer.hasArg("server") || !webServer.hasArg("port"))
    {
        sendJsonError(400, "server and port required");
        return;
    }
    String serverArg = webServer.arg("server");
    serverArg.trim();
    String portArg = webServer.arg("port");
    portArg.trim();
    String userArg = webServer.arg("username");
    String passArg = webServer.arg("password");
    String clientIdArg = webServer.arg("clientid");
    clientIdArg.trim();
    String topicArg = webServer.arg("topic");
    topicArg.trim();
    String topicCurrentArg = webServer.arg("topic_current");
    topicCurrentArg.trim();

    if (serverArg.isEmpty() || serverArg.length() >= sizeof(mqtt_server))
    {
        sendJsonError(400, "invalid server");
        return;
    }
    if (parsePort(portArg.c_str()) == 0)
    {
        sendJsonError(400, "port out of range");
        return;
    }
    if (userArg.length() >= sizeof(mqtt_user) || passArg.length() >= sizeof(mqtt_password))
    {
        sendJsonError(400, "username or password too long");
        return;
    }
    if (clientIdArg.length() > 0 && (clientIdArg.length() >= 64 || clientIdArg.indexOf('/') >= 0 || !isValidTopic(clientIdArg)))
    {
        sendJsonError(400, "invalid client ID");
        return;
    }
    if ((topicArg.length() > 0 && !isValidTopic(topicArg)) || (topicCurrentArg.length() > 0 && !isValidTopic(topicCurrentArg)))
    {
        sendJsonError(400, "invalid topic (no + or #, no empty levels)");
        return;
    }

    // If currently connected, announce offline and disconnect so the reconnect is clean
    if (client.connected())
    {
        Serial.println("Disconnecting existing MQTT connection before reconfiguring");
        if (clientIdArg.length() > 0 && clientIdArg != clientID)
        {
            clearHassDiscovery(clientID);
        }
        else
        {
            client.publish((clientID + "/availability").c_str(), "offline", true);
        }
        client.disconnect();
    }

    strlcpy(mqtt_server, serverArg.c_str(), sizeof(mqtt_server));
    strlcpy(mqtt_port, portArg.c_str(), sizeof(mqtt_port));
    strlcpy(mqtt_user, userArg.c_str(), sizeof(mqtt_user));
    // The dashboard never receives the stored password: an empty field means "keep it".
    // Without a username there is no authentication at all, so the password is cleared then.
    if (passArg.length() > 0 || userArg.length() == 0)
    {
        strlcpy(mqtt_password, passArg.c_str(), sizeof(mqtt_password));
    }

    if (clientIdArg.length() > 0 && clientIdArg != clientID)
    {
        clientID = clientIdArg;
        Serial.printf("Setting clientID to: %s\n", clientID.c_str());
    }
    if (topicArg.length() > 0)
    {
        mqtt_topic_gas = topicArg;
        Serial.printf("Setting mqtt topic base to: %s\n", mqtt_topic_gas.c_str());
    }
    if (topicCurrentArg.length() > 0)
    {
        mqtt_topic_currentVal = topicCurrentArg;
        Serial.printf("Setting mqtt topic current to: %s\n", mqtt_topic_currentVal.c_str());
    }
    hassDiscoveryPublished = false;

    saveDataToSPIFFS();
    bool connected = reconnect_mqtt();
    updateDisplay();

    JsonDocument doc;
    doc["status"] = "ok";
    doc["mqttConnected"] = connected;
    doc["mqttServer"] = mqtt_server;
    doc["mqttPort"] = mqtt_port;
    doc["mqttLastStatus"] = lastMqttStatus;
    doc["mqttLastError"] = lastMqttErrorCode;
    doc["mqttLastAttemptUptime"] = static_cast<uint32_t>(timeStamps.lastMQTTreconnectTime / 1000);
    doc["clientID"] = clientID;
    doc["mqttTopicBase"] = mqtt_topic_gas;
    doc["mqttTopicCurrentBase"] = mqtt_topic_currentVal;
    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
}

// OTA state of the current upload
bool otaRejected = false;
bool otaStarted = false;

void handleFirmwareUpload()
{
    HTTPUpload &upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        otaStarted = false;
        // Headers are parsed before the body, so the CSRF check works here, before anything is flashed
        otaRejected = !isTrustedRequest() || (strlen(web_password) > 0 && !webServer.authenticate(WEB_USER, web_password));
        if (otaRejected)
        {
            Serial.println("OTA: rejected untrusted upload");
            return;
        }
        Serial.printf("OTA: Upload start %s\n", upload.filename.c_str());
        otaStarted = Update.begin(UPDATE_SIZE_UNKNOWN);
        if (!otaStarted)
        {
            Update.printError(Serial);
        }
    }
    else if (otaRejected || !otaStarted)
    {
        return;
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (Update.end(true))
        {
            Serial.printf("OTA: Update success (%u bytes)\n", upload.totalSize);
        }
        else
        {
            Update.printError(Serial);
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        Update.abort();
        otaStarted = false;
        Serial.println("OTA: Upload aborted");
    }
}

void handleFirmwareUploadDone()
{
    if (rejectUntrustedRequest())
        return;
    if (otaRejected)
    {
        sendJsonError(403, "forbidden");
        return;
    }
    bool success = otaStarted && Update.isFinished() && !Update.hasError();
    JsonDocument doc;
    doc["success"] = success;
    doc["message"] = success ? "Update successful" : "Update failed";
    String payload;
    serializeJson(doc, payload);
    webServer.send(success ? 200 : 500, "application/json", payload);
    otaStarted = false;
    if (success)
    {
        saveAndRestart();
    }
}

void setupWebInterface()
{
    const char *headerKeys[] = {"X-Requested-With", "Origin"};
    webServer.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
    webServer.on("/", HTTP_GET, handleRootRequest);
    webServer.on("/api/status", HTTP_GET, handleStatusRequest);
    webServer.on("/api/consumption", HTTP_POST, handleConsumptionUpdate);
    webServer.on("/api/restart", HTTP_POST, handleRestartRequest);
    webServer.on("/api/mqtt", HTTP_POST, handleMqttConfigUpdate);
    webServer.on("/update", HTTP_POST, handleFirmwareUploadDone, handleFirmwareUpload);
    webServer.onNotFound([]()
                         { sendJsonError(404, "not found"); });
}

// Run the dashboard only while the config portal is inactive: both servers listen on port 80
void updateWebServer()
{
    bool shouldRun = !wm.getConfigPortalActive() && WiFi.status() == WL_CONNECTED;
    if (shouldRun && !webServerRunning)
    {
        webServer.begin();
        webServerRunning = true;
        Serial.println("HTTP dashboard available on http://" + WiFi.localIP().toString());
    }
    else if (!shouldRun && webServerRunning && wm.getConfigPortalActive())
    {
        // Keep the dashboard on short Wi-Fi drops, stop it only for the portal
        webServer.stop();
        webServerRunning = false;
        Serial.println("HTTP dashboard stopped (config portal active)");
    }
    if (webServerRunning)
        webServer.handleClient();
}

void handleRestartRequest()
{
    if (rejectUntrustedRequest())
        return;
    JsonDocument doc;
    doc["status"] = "restarting";
    doc["message"] = "Device will restart now";
    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
    saveAndRestart();
}

// Save state and restart the device
void saveAndRestart()
{
    saveDataToSPIFFS();
    if (client.connected())
    {
        client.publish((clientID + "/availability").c_str(), "offline", true);
        client.disconnect();
    }
    delay(200);
    ESP.restart();
}
