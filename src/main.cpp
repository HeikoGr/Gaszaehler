#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <Button2.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
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
char prevMqttUser[40];
char prevMqttPassword[40];

// set gas meter manually
long number = 0; // Verwendet long für größeren Wertebereich
int cursorPosition = 0;
const int maxDigits = 9; // 6 Vorkomma + Dezimalpunkt + 2 Nachkomma

// Display
TFT_eSPI tft = TFT_eSPI();
String chipID;
String clientID;

void snapshotPersistentState()
{
    prevPulseCount = pulseCount;
    prevOffset = offset;
    strlcpy(prevMqttServer, mqtt_server, sizeof(prevMqttServer));
    strlcpy(prevMqttPort, mqtt_port, sizeof(prevMqttPort));
    strlcpy(prevMqttUser, mqtt_user, sizeof(prevMqttUser));
    strlcpy(prevMqttPassword, mqtt_password, sizeof(prevMqttPassword));
}

// WiFi Manager
WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_user("username", "mqtt username", mqtt_user, 40);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);
// PubSub (MQTT)
WiFiClient espClient;
PubSubClient client(espClient);
// Web server
WebServer webServer(80);
// Button2 instances
Button2 button1;
Button2 button2;
// Button2 button2;


// Simple control surface served at runtime
const char WEB_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gaszähler</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600&display=swap');
:root {
    --bg:#030712;
    --card:rgba(6,10,26,0.85);
    --accent:#facc15;
    --accent-2:#38bdf8;
    --text:#f8fafc;
    --muted:#94a3b8;
}
* { box-sizing:border-box; }
body {
    margin:0;
    min-height:100vh;
    background:radial-gradient(circle at top,#1e293b,#020617 60%);
    font-family:'Space Grotesk',sans-serif;
    color:var(--text);
    display:flex;
    justify-content:center;
    padding:32px;
}
main {
    width:min(960px,100%);
    display:grid;
    gap:24px;
}
.card {
    background:var(--card);
    padding:24px;
    border-radius:22px;
    border:1px solid rgba(255,255,255,0.08);
    box-shadow:0 30px 50px rgba(0,0,0,0.4);
}
.header h1 {
    margin:0;
    font-size:2.4rem;
}
.header p { color:var(--muted); }
.status-dot {
    display:inline-block;
    width:12px;
    height:12px;
    border-radius:50%;
    margin-left:8px;
    background:#ef4444;
}
.status-dot.online { background:#22c55e; }
.muted { color:var(--muted); font-size:0.9rem; }
form { display:flex; flex-direction:column; gap:12px; }
label {
    font-size:0.85rem;
    letter-spacing:0.05em;
    text-transform:uppercase;
    color:var(--muted);
}
input {
    padding:12px 14px;
    border-radius:12px;
    border:1px solid rgba(255,255,255,0.15);
    background:rgba(255,255,255,0.04);
    color:var(--text);
    font-size:1rem;
}
button {
    border:none;
    border-radius:999px;
    padding:12px 20px;
    font-size:1rem;
    font-weight:600;
    letter-spacing:0.03em;
    color:#0f172a;
    cursor:pointer;
    background:linear-gradient(120deg,var(--accent),var(--accent-2));
}
button:disabled { opacity:0.4; cursor:not-allowed; }
.feedback { min-height:1.2rem; font-size:0.85rem; color:var(--muted); }
.grid-two {
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(200px,1fr));
    gap:12px;
}
@media (max-width:768px) {
    body { padding:16px; }
    main { gap:18px; }
}
</style>
</head>
<body>
<main>
    <section class="header">
        <h1>Gaszähler</h1>
        <p>Live-Status & Steuerung</p>
    </section>
    <section class="card" id="status-card">
        <h2 id="volume">-- m³</h2>
        <p class="muted">MQTT <span id="mqtt-dot" class="status-dot"></span></p>
        <p id="mqtt-info" class="muted"></p>
        <p id="uptime" class="muted"></p>
    </section>
    <section class="card">
        <h3>Zählerstand korrigieren</h3>
        <form id="consumption-form">
            <label for="consumption">Neuer Wert (m³)</label>
            <input type="number" step="0.01" min="0" id="consumption" name="value" required>
            <button type="submit">Speichern</button>
            <span class="feedback" id="consumption-feedback"></span>
        </form>
    </section>
    <section class="card">
        <h3>MQTT Parameter</h3>
        <form id="mqtt-form">
            <div class="grid-two">
                <div>
                    <label for="mqtt-server">Server</label>
                    <input type="text" id="mqtt-server" name="server" required>
                </div>
                <div>
                    <label for="mqtt-port">Port</label>
                    <input type="number" id="mqtt-port" name="port" min="1" max="65535" required>
                </div>
            </div>
            <div class="grid-two">
                <div>
                    <label for="mqtt-user">User</label>
                    <input type="text" id="mqtt-user" name="username">
                </div>
                <div>
                    <label for="mqtt-password">Passwort</label>
                    <input type="password" id="mqtt-password" name="password">
                </div>
            </div>
            <button type="submit">Übernehmen & Verbinden</button>
            <span class="feedback" id="mqtt-feedback"></span>
        </form>
    </section>
    <section class="card">
        <h3>OTA Update</h3>
        <form id="ota-form" action="/update" method="POST" enctype="multipart/form-data">
            <label for="firmware">Firmware (.bin)</label>
            <input type="file" id="firmware" name="firmware" accept=".bin" required>
            <button type="submit">Upload & Flash</button>
            <span class="feedback" id="ota-feedback"></span>
        </form>
    </section>
</main>
<script>
const volumeEl = document.getElementById('volume');
const mqttDot = document.getElementById('mqtt-dot');
const mqttInfo = document.getElementById('mqtt-info');
const uptimeEl = document.getElementById('uptime');
const consumptionForm = document.getElementById('consumption-form');
const consumptionFeedback = document.getElementById('consumption-feedback');
const consumptionInput = document.getElementById('consumption');
const mqttForm = document.getElementById('mqtt-form');
const mqttFeedback = document.getElementById('mqtt-feedback');
const otaForm = document.getElementById('ota-form');
const otaFeedback = document.getElementById('ota-feedback');

const nf = new Intl.NumberFormat('de-DE', { minimumFractionDigits: 2, maximumFractionDigits: 2 });

async function refreshStatus() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) throw new Error('Status HTTP ' + response.status);
        const data = await response.json();
        volumeEl.textContent = data.gasVolumeFormatted + ' m³';
        mqttDot.classList.toggle('online', data.mqttConnected);
        mqttInfo.textContent = `${data.mqttServer}:${data.mqttPort} · Topic ${data.mqttTopicGas}`;
        uptimeEl.textContent = `Uptime: ${formatUptime(data.uptimeSeconds)}`;
        consumptionInput.placeholder = nf.format(data.gasVolumeM3 || 0);
        if (!consumptionInput.value) {
            consumptionInput.value = nf.format(data.gasVolumeM3 || 0);
        }
        mqttForm.server.value = data.mqttServer || '';
        mqttForm.port.value = data.mqttPort || '';
        mqttForm.username.value = data.mqttUser || '';
        mqttForm.password.value = data.maskedPassword || '';
    } catch (error) {
        mqttInfo.textContent = 'Status nicht verfügbar';
    }
}

function formatUptime(seconds) {
    const hrs = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    return `${hrs}h ${mins}m`;
}

consumptionForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const value = consumptionInput.value;
    consumptionFeedback.textContent = 'Wird übertragen...';
    try {
        const body = new URLSearchParams({ value });
        const response = await fetch('/api/consumption', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body
        });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        consumptionFeedback.textContent = `Gespeichert: ${nf.format(data.value)} m³`;
        await refreshStatus();
    } catch (error) {
        consumptionFeedback.textContent = 'Fehler: ' + error.message;
    }
});

mqttForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    mqttFeedback.textContent = 'Neue Parameter werden übernommen...';
    const formData = new URLSearchParams({
        server: mqttForm.server.value,
        port: mqttForm.port.value,
        username: mqttForm.username.value,
        password: mqttForm.password.value
    });
    try {
        const response = await fetch('/api/mqtt', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: formData
        });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        mqttFeedback.textContent = data.mqttConnected ? 'Verbunden.' : 'Einstellungen gespeichert, Verbindung wird versucht...';
        await refreshStatus();
    } catch (error) {
        mqttFeedback.textContent = 'Fehler: ' + error.message;
    }
});
otaForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const file = document.getElementById('firmware').files[0];
    if (!file) {
        otaFeedback.textContent = 'Bitte eine Firmware auswählen.';
        return;
    }
    otaFeedback.textContent = 'Upload läuft...';
    const formData = new FormData();
    formData.append('firmware', file, file.name);
    try {
        const response = await fetch('/update', { method: 'POST', body: formData });
        const data = await response.json();
        if (response.ok && data.success) {
            otaFeedback.textContent = 'Update erfolgreich. Gerät startet neu.';
            setTimeout(() => location.reload(), 3000);
        } else {
            throw new Error(data.message || 'Update fehlgeschlagen');
        }
    } catch (error) {
        otaFeedback.textContent = 'Fehler: ' + error.message;
    }
});

refreshStatus();
setInterval(refreshStatus, 5000);
</script>
</body>
</html>
)rawliteral";
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

        snapshotPersistentState();

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

    if (connectionStatus.wifiConnected != connectionStatus.prevWifiStatus || connectionStatus.mqttConnected != connectionStatus.prevMqttStatus)
    {
        updateDisplay();
    }

    wm.process();
    webServer.handleClient();

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
        strcmp(mqtt_server, prevMqttServer) == 0 && strcmp(mqtt_port, prevMqttPort) == 0 &&
        strcmp(mqtt_user, prevMqttUser) == 0 && strcmp(mqtt_password, prevMqttPassword) == 0)
    {
        Serial.println("No new data to save");
        return;
    }
    if (spiffsManager.saveData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password))
    {
        snapshotPersistentState();
        timeStamps.lastSaveTime = millis();
    }
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

void handleRootRequest()
{
    webServer.send_P(200, "text/html", WEB_DASHBOARD);
}

void handleStatusRequest()
{
    connectionStatus.wifiConnected = (WiFi.status() == WL_CONNECTED);
    connectionStatus.mqttConnected = client.connected();
    StaticJsonDocument<768> doc;
    uint32_t currentVolume = pulseCount + offset;
    doc["gasVolumeRaw"] = currentVolume;
    doc["gasVolumeM3"] = static_cast<float>(currentVolume) / 100.0f;
    doc["gasVolumeFormatted"] = formatWithHundredsSeparator(currentVolume);
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
    doc["offset"] = offset;
    doc["pulseCount"] = pulseCount;

    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
}

void handleConsumptionUpdate()
{
    if (!webServer.hasArg("value"))
    {
        webServer.send(400, "application/json", "{\"error\":\"value missing\"}");
        return;
    }
    String target = webServer.arg("value");
    target.trim();
    target.replace(',', '.');
    if (target.length() == 0)
    {
        webServer.send(400, "application/json", "{\"error\":\"value empty\"}");
        return;
    }
    double newValue = target.toDouble();
    if (newValue < 0)
    {
        webServer.send(400, "application/json", "{\"error\":\"value negative\"}");
        return;
    }
    uint32_t scaled = static_cast<uint32_t>(newValue * 100.0 + 0.5);
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

    StaticJsonDocument<256> doc;
    doc["status"] = "ok";
    doc["value"] = static_cast<float>(scaled) / 100.0f;
    doc["gasVolumeFormatted"] = formatWithHundredsSeparator(pulseCount + offset);
    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
}

void handleMqttConfigUpdate()
{
    if (!webServer.hasArg("server") || !webServer.hasArg("port"))
    {
        webServer.send(400, "application/json", "{\"error\":\"server and port required\"}");
        return;
    }
    String serverArg = webServer.arg("server");
    serverArg.trim();
    String portArg = webServer.arg("port");
    portArg.trim();
    if (serverArg.isEmpty() || portArg.isEmpty())
    {
        webServer.send(400, "application/json", "{\"error\":\"invalid input\"}");
        return;
    }

    long portLong = portArg.toInt();
    if (portLong <= 0 || portLong > 65535)
    {
        webServer.send(400, "application/json", "{\"error\":\"port out of range\"}");
        return;
    }

    String userArg = webServer.hasArg("username") ? webServer.arg("username") : "";
    String passArg = webServer.hasArg("password") ? webServer.arg("password") : "";

    strlcpy(mqtt_server, serverArg.c_str(), sizeof(mqtt_server));
    strlcpy(mqtt_port, portArg.c_str(), sizeof(mqtt_port));
    strlcpy(mqtt_user, userArg.c_str(), sizeof(mqtt_user));
    strlcpy(mqtt_password, passArg.c_str(), sizeof(mqtt_password));

    saveDataToSPIFFS();
    bool connected = reconnect_mqtt();

    StaticJsonDocument<256> doc;
    doc["status"] = "ok";
    doc["mqttConnected"] = connected;
    doc["mqttServer"] = mqtt_server;
    doc["mqttPort"] = mqtt_port;
    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
}

void handleFirmwareUpload()
{
    HTTPUpload &upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
        Serial.printf("OTA: Upload start %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            Update.printError(Serial);
        }
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
        Update.end();
        Serial.println("OTA: Upload aborted");
    }
}

void setupWebInterface()
{
    webServer.on("/", HTTP_GET, handleRootRequest);
    webServer.on("/api/status", HTTP_GET, handleStatusRequest);
    webServer.on("/api/consumption", HTTP_POST, handleConsumptionUpdate);
    webServer.on("/api/mqtt", HTTP_POST, handleMqttConfigUpdate);
    webServer.on("/update", HTTP_POST,
                 []()
                 {
                     bool success = !Update.hasError();
                     StaticJsonDocument<128> doc;
                     doc["success"] = success;
                     doc["message"] = success ? "Update erfolgreich" : "Update fehlgeschlagen";
                     String payload;
                     serializeJson(doc, payload);
                     webServer.send(success ? 200 : 500, "application/json", payload);
                     if (success)
                     {
                         delay(200);
                         ESP.restart();
                     }
                 },
                 handleFirmwareUpload);
    webServer.onNotFound([]()
                         { webServer.send(404, "application/json", "{\"error\":\"not found\"}"); });
    webServer.begin();
    Serial.println("HTTP dashboard available on http://" + WiFi.localIP().toString());
}