#include <Arduino.h>
#include <WiFi.h>

#include <WiFiManager.h>
#include <Button2.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <Update.h>
#include <PubSubClient.h>
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
const char *const version = "V 0.1.0";

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

// MQTT Topics (mutable so web UI can change them at runtime)
// Default now uses a Home Assistant friendly path under the clientID: clientID/measurement/gas
String mqtt_topic_gas = "measurement/gas";
String mqtt_topic_currentVal = "measurement/current";

// State values
uint32_t offset = 0;
char mqtt_server[40] = "192.168.178.203";
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
char prevClientID[64];
char prevMqttTopic[64];
char prevMqttTopicCurrent[64];

// set gas meter manually
long number = 0; // Use long for a larger value range
int cursorPosition = 0;
const int maxDigits = 9; // 6 Vorkomma + Dezimalpunkt + 2 Nachkomma

// Display
TFT_eSPI tft = TFT_eSPI();
String chipID;
String clientID;

// Forward declarations
void publishHassDiscovery();
void handleRestartRequest();

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
// MQTT diagnostics
String lastMqttStatus = "never";
unsigned long lastMqttAttemptTime = 0; // millis()
int lastMqttErrorCode = 0;
// Home Assistant discovery published flag
bool hassDiscoveryPublished = false;
// Web server
WebServer webServer(80);
// Button2 instances
Button2 button1;
Button2 button2;
// Button2 button2;


// Simple control surface served at runtime (default language: English)
const char WEB_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gas Meter</title>
<style>
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
    /* use system font stack to avoid external webfont fetch delays */
    font-family:-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    color:var(--text);
    display:flex;
    justify-content:center;
    padding:12px;
}
main {
    width:min(720px,100%);
    display:grid;
    gap:18px;
}
.card {
    background:var(--card);
    padding:14px;
    border-radius:12px;
    border:1px solid rgba(255,255,255,0.06);
    box-shadow:0 18px 30px rgba(0,0,0,0.35);
}
.header h1 {
    margin:0;
    font-size:1.6rem;
}
.header p { color:var(--muted); }
.status-dot {
    display:inline-block;
    width:10px;
    height:10px;
    border-radius:50%;
    margin-left:8px;
    background:#ef4444;
}
.status-dot.online { background:#22c55e; }
.muted { color:var(--muted); font-size:0.9rem; }
form { display:flex; flex-direction:column; gap:12px; }
label {
    font-size:0.78rem;
    letter-spacing:0.05em;
    text-transform:uppercase;
    color:var(--muted);
}
input {
    padding:10px 12px;
    border-radius:8px;
    border:1px solid rgba(255,255,255,0.12);
    background:rgba(255,255,255,0.03);
    color:var(--text);
    font-size:0.95rem;
}
button {
    border:none;
    border-radius:999px;
    padding:10px 14px;
    font-size:0.95rem;
    font-weight:600;
    letter-spacing:0.02em;
    color:#0f172a;
    cursor:pointer;
    background:linear-gradient(120deg,var(--accent),var(--accent-2));
}
button:disabled { opacity:0.4; cursor:not-allowed; }
.feedback { min-height:1.2rem; font-size:0.85rem; color:var(--muted); }
.grid-two {
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(160px,1fr));
    gap:8px;
}
/* Ensure grid children can shrink on very narrow screens and inputs don't overflow */
.grid-two > div { min-width: 0; }
input, select, textarea { width: 100%; box-sizing: border-box; }

/* Mobile-specific: stack two-column grids and make buttons full-width */
@media (max-width:480px) {
    .grid-two { grid-template-columns: 1fr; gap:10px; }
    button { width: 100%; }
    input { font-size: 0.95rem; }
    label { display:block; }
}
@media (max-width:768px) {
    body { padding:10px; }
    main { gap:14px; }
    .card { padding:12px; }
    .header h1 { font-size:1.4rem; }
    input { font-size:0.95rem; }
    button { padding:8px 12px; font-size:0.9rem; }
}
</style>
</head>
<body>
<main>
    <section class="header" style="display:flex;align-items:center;justify-content:space-between;">
        <div>
            <h1 id="hdr-title">Gas Meter</h1>
            <p id="hdr-sub">Live status & control</p>
        </div>
        <div style="display:flex;gap:8px;align-items:center;">
            <!-- Language toggle: default English, click DE flag for German -->
            <button id="lang-en" aria-label="English" style="font-size:20px;">🇬🇧</button>
            <button id="lang-de" aria-label="Deutsch" style="font-size:20px;">🇩🇪</button>
        </div>
    </section>
    <section class="card" id="status-card">
        <h2 id="volume">-- m³</h2>
        <p class="muted">MQTT <span id="mqtt-dot" class="status-dot"></span></p>
        <p id="mqtt-info" class="muted"></p>
        <p id="uptime" class="muted"></p>
    </section>
    <section class="card">
        <h3 id="lbl-correct">Correct meter reading</h3>
        <form id="consumption-form">
            <label for="consumption" id="lbl-new-val">New value (m³)</label>
            <input type="text" inputmode="decimal" pattern="[0-9\s\.'`]+([\\.,][0-9]{1,2})?" id="consumption" name="value" required>
            <button type="submit" id="btn-save">Save</button>
            <span class="feedback" id="consumption-feedback"></span>
        </form>
    </section>
    <section class="card">
        <h3 id="lbl-mqtt">MQTT Parameters</h3>
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
                    <input type="text" id="mqtt-user" name="username" autocomplete="username">
                </div>
                <div>
                    <label for="mqtt-password">Passwort</label>
                    <input type="password" id="mqtt-password" name="password" autocomplete="off">
                </div>
            </div>
            <div class="grid-two">
                <div>
                    <label for="mqtt-clientid">Client ID</label>
                    <input type="text" id="mqtt-clientid" name="clientid">
                </div>
                <div>
                    <label for="mqtt-topic">Topic (base)</label>
                    <input type="text" id="mqtt-topic" name="topic" placeholder="measurement/gas">
                </div>
            </div>
            <div class="grid-two">
                <div></div>
                <div>
                    <label for="mqtt-topic-current">Topic (current)</label>
                    <input type="text" id="mqtt-topic-current" name="topic_current" placeholder="measurement/current">
                </div>
            </div>
            <button type="submit" id="btn-apply">Apply & Connect</button>
            <span class="feedback" id="mqtt-feedback"></span>
        </form>
    </section>
    <section class="card">
        <h3 id="lbl-ota">OTA Update</h3>
        <form id="ota-form" action="/update" method="POST" enctype="multipart/form-data">
            <label for="firmware" id="lbl-fw">Firmware (.bin)</label>
            <input type="file" id="firmware" name="firmware" accept=".bin" required>
            <button type="submit" id="btn-upload">Upload & Flash</button>
            <span class="feedback" id="ota-feedback"></span>
        </form>
    </section>
    <section class="card">
        <h3 id="lbl-restart">Device Control</h3>
        <p class="muted" id="restart-desc">Remote restart of the device (will reconnect to WiFi/MQTT on boot)</p>
        <button id="btn-restart">Restart Device</button>
        <span class="feedback" id="restart-feedback"></span>
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
const restartBtn = document.getElementById('btn-restart');
const restartFeedback = document.getElementById('restart-feedback');

const nf = new Intl.NumberFormat('de-DE', { minimumFractionDigits: 2, maximumFractionDigits: 2 });

async function refreshStatus() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) throw new Error('Status HTTP ' + response.status);
        const data = await response.json();
        volumeEl.textContent = data.gasVolumeFormatted + ' m³';
        mqttDot.classList.toggle('online', data.mqttConnected);
        const lastAttempt = data.mqttLastAttemptUptime || 0;
        mqttInfo.textContent = `${data.mqttServer}:${data.mqttPort} · ${data.mqttLastStatus} · last try ${lastAttempt}s · Topic ${data.mqttTopicGas}`;
        uptimeEl.textContent = `Uptime: ${formatUptime(data.uptimeSeconds)}`;
        const formattedVolume = nf.format(data.gasVolumeM3 || 0);
        consumptionInput.placeholder = formattedVolume;
        // Prefill with current value if empty or if not focused, to keep the latest reading visible
        if (!consumptionInput.value || document.activeElement !== consumptionInput) {
            consumptionInput.value = formattedVolume;
        }
        // Only update form fields if the user is not currently editing them
        if (document.activeElement !== mqttForm.server) mqttForm.server.value = data.mqttServer || '';
        if (document.activeElement !== mqttForm.port) mqttForm.port.value = data.mqttPort || '';
        if (document.activeElement !== mqttForm.username) mqttForm.username.value = data.mqttUser || '';
        // For security, show masked password as placeholder only; do not overwrite while editing
        mqttForm.password.placeholder = data.maskedPassword || '';
        // Client ID and topic base
        if (document.activeElement !== mqttForm.clientid) mqttForm.clientid.value = data.clientID || '';
        if (document.activeElement !== mqttForm.topic) mqttForm.topic.value = data.mqttTopicBase || '';
        if (document.activeElement !== mqttForm.topic_current) mqttForm.topic_current.value = data.mqttTopicCurrentBase || '';
    } catch (error) {
        mqttInfo.textContent = t('statusUnavailable');
    }
}

function formatUptime(seconds) {
    const hrs = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    return `${hrs}h ${mins}m`;
}

// Localization strings
const translations = {
    en: {
        consuming: 'Uploading...',
        saved: 'Saved',
        mqttApplying: 'Applying new parameters...',
        otaUploading: 'Upload in progress...',
        otaNoFile: 'Please select a firmware file.',
        connected: 'Connected.',
        settingsSavedAttempting: 'Settings saved, attempting connection...',
        statusUnavailable: 'Status not available',
        restartPrompt: 'Restart device?',
        restarting: 'Restarting...',
        restartDesc: 'Remote restart of the device (will reconnect to WiFi/MQTT on boot)'
    },
    de: {
        consuming: 'Wird übertragen...',
        saved: 'Gespeichert',
        mqttApplying: 'Neue Parameter werden übernommen...',
        otaUploading: 'Upload läuft...',
        otaNoFile: 'Bitte eine Firmware auswählen.',
        connected: 'Verbunden.',
        settingsSavedAttempting: 'Einstellungen gespeichert, Verbinden...',
        statusUnavailable: 'Status nicht verfügbar',
        restartPrompt: 'Gerät neu starten?',
        restarting: 'Neustart läuft...',
        restartDesc: 'Neustart des Geräts (verbindet sich danach wieder mit WiFi/MQTT)'
    }
};
// Pick saved language or fallback to browser preference
let currentLang = localStorage.getItem('lang') || ((navigator.language || 'en').toLowerCase().startsWith('de') ? 'de' : 'en');

// Call this function to change language and persist preference
function setLang(lang) {
    if (translations[lang]) {
        currentLang = lang;
        localStorage.setItem('lang', lang);
    }
}
/**
 * Returns the translated string for the given key based on the current language.
 * Falls back to English if the key is not found in the selected language.
 * @param {string} key - Translation key to lookup
 * @returns {string} - Translated string
 */
function t(key) {
    return translations[currentLang][key] || translations['en'][key] || '';
}
consumptionForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const raw = consumptionInput.value.trim();
    // Strip thousand separators (space, dot, apostrophe/backtick) and normalize decimal comma to dot
    const normalized = raw
        .replace(/[\s\.'`]/g, '')
        .replace(',', '.');
    const value = normalized;
    consumptionFeedback.textContent = t('consuming');
    try {
        const body = new URLSearchParams({ value });
        const response = await fetch('/api/consumption', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body
        });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        consumptionFeedback.textContent = `${t('saved')}: ${nf.format(data.value)} m³`;
        await refreshStatus();
    } catch (error) {
        consumptionFeedback.textContent = 'Error: ' + error.message;
    }
});

mqttForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    mqttFeedback.textContent = t('mqttApplying');
    const formData = new URLSearchParams({
        server: mqttForm.server.value,
        port: mqttForm.port.value,
        username: mqttForm.username.value,
        password: mqttForm.password.value,
        clientid: mqttForm.clientid.value,
        topic: mqttForm.topic.value,
        topic_current: mqttForm.topic_current.value
    });
    try {
        const response = await fetch('/api/mqtt', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: formData
        });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        if (data.mqttConnected) {
            mqttFeedback.textContent = t('connected');
        } else {
            mqttFeedback.textContent = data.mqttLastStatus || t('settingsSavedAttempting');
        }
        await refreshStatus();
    } catch (error) {
        mqttFeedback.textContent = 'Error: ' + error.message;
    }
});
otaForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const file = document.getElementById('firmware').files[0];
    if (!file) {
        otaFeedback.textContent = t('otaNoFile');
        return;
    }
    otaFeedback.textContent = t('otaUploading');
    const formData = new FormData();
    formData.append('firmware', file, file.name);
    try {
        const response = await fetch('/update', { method: 'POST', body: formData });
        const data = await response.json();
        if (response.ok && data.success) {
            otaFeedback.textContent = 'Update successful. Device will restart.';
            setTimeout(() => location.reload(), 3000);
        } else {
            throw new Error(data.message || 'Update failed');
        }
    } catch (error) {
        otaFeedback.textContent = 'Error: ' + error.message;
    }
});

// Restart button handler
restartBtn.addEventListener('click', async () => {
    if (!confirm(t('restartPrompt'))) return;
    restartFeedback.textContent = t('restarting');
    try {
        const response = await fetch('/api/restart', { method: 'POST' });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        restartFeedback.textContent = data.message || t('restarting');
    } catch (error) {
        restartFeedback.textContent = 'Error: ' + error.message;
    }
});

// Language toggle handlers
document.getElementById('lang-en').addEventListener('click', () => { setLanguage('en'); });
document.getElementById('lang-de').addEventListener('click', () => { setLanguage('de'); });

function setLanguage(lang) {
    currentLang = lang;
    if (lang === 'en') {
        document.getElementById('hdr-title').textContent = 'Gas Meter';
        document.getElementById('hdr-sub').textContent = 'Live status & control';
        document.getElementById('lbl-correct').textContent = 'Correct meter reading';
        document.getElementById('lbl-new-val').textContent = 'New value (m³)';
        document.getElementById('btn-save').textContent = 'Save';
        document.getElementById('lbl-mqtt').textContent = 'MQTT Parameters';
        document.getElementById('btn-apply').textContent = 'Apply & Connect';
        document.getElementById('lbl-ota').textContent = 'OTA Update';
        document.getElementById('lbl-fw').textContent = 'Firmware (.bin)';
        document.getElementById('btn-upload').textContent = 'Upload & Flash';
        document.getElementById('lbl-restart').textContent = 'Device Control';
        document.getElementById('btn-restart').textContent = 'Restart Device';
        document.getElementById('restart-desc').textContent = t('restartDesc');
    } else {
        document.getElementById('hdr-title').textContent = 'Gaszähler';
        document.getElementById('hdr-sub').textContent = 'Live-Status & Steuerung';
        document.getElementById('lbl-correct').textContent = 'Zählerstand korrigieren';
        document.getElementById('lbl-new-val').textContent = 'Neuer Wert (m³)';
        document.getElementById('btn-save').textContent = 'Speichern';
        document.getElementById('lbl-mqtt').textContent = 'MQTT Parameter';
        document.getElementById('btn-apply').textContent = 'Übernehmen & Verbinden';
        document.getElementById('lbl-ota').textContent = 'OTA Update';
        document.getElementById('lbl-fw').textContent = 'Firmware (.bin)';
        document.getElementById('btn-upload').textContent = 'Upload & Flash';
        document.getElementById('lbl-restart').textContent = 'Gerätsteuerung';
        document.getElementById('btn-restart').textContent = 'Neustart';
        document.getElementById('restart-desc').textContent = t('restartDesc');
    }
}

    // Initialize language based on browser preference (de -> German, else English)
    setLanguage(currentLang);

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
        {
            char storedClientID[64] = "";
            char storedTopic[64] = "";
                char storedTopicCurrent[64] = "";
                if (spiffsManager.loadData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password, storedClientID, storedTopic, storedTopicCurrent))
            {
                Serial.println("Data successfully loaded");
                if (strlen(storedClientID) > 0) {
                    clientID = String(storedClientID);
                    Serial.printf("Loaded clientID: %s\n", clientID.c_str());
                }
                if (strlen(storedTopic) > 0) {
                    mqtt_topic_gas = String(storedTopic);
                    Serial.printf("Loaded mqtt topic base: %s\n", mqtt_topic_gas.c_str());
                }
                    if (strlen(storedTopicCurrent) > 0) {
                        mqtt_topic_currentVal = String(storedTopicCurrent);
                        Serial.printf("Loaded mqtt topic current: %s\n", mqtt_topic_currentVal.c_str());
                    }
            }
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
    client.setBufferSize(MQTT_MAX_PACKET_SIZE);

    wm.setConfigPortalBlocking(false);
    wm.setSaveParamsCallback(WMsaveParamsCallback);
    wm.setConfigPortalTimeout(60);

    // Allow larger MQTT messages (e.g., HA discovery payloads)
    client.setBufferSize(1024);

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
        strcmp(mqtt_user, prevMqttUser) == 0 && strcmp(mqtt_password, prevMqttPassword) == 0 &&
        strcmp(clientID.c_str(), prevClientID) == 0 && strcmp(mqtt_topic_gas.c_str(), prevMqttTopic) == 0 && strcmp(mqtt_topic_currentVal.c_str(), prevMqttTopicCurrent) == 0)
    {
        Serial.println("No new data to save");
        return;
    }
    if (spiffsManager.saveData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password, (char*)clientID.c_str(), (char*)mqtt_topic_gas.c_str(), (char*)mqtt_topic_currentVal.c_str()))
    {
        snapshotPersistentState();
        timeStamps.lastSaveTime = millis();
        // If MQTT is connected and discovery not yet published (or topics changed), attempt publishing discovery
        if (client.connected() && !hassDiscoveryPublished) {
            publishHassDiscovery();
        }
    }
}

// Function to reconnect to the MQTT broker
boolean reconnect_mqtt()
{
    // Set up MQTT client
    uint16_t port = static_cast<uint16_t>(std::stoi(mqtt_port));
    client.setServer(mqtt_server, port);
    client.setCallback(MQTTcallbackReceive);
    Serial.printf("Trying to connect to MQTT server %s:%s (user:%s) ... \n", mqtt_server, mqtt_port, mqtt_user);

    lastMqttAttemptTime = millis();
    timeStamps.lastMQTTreconnectTime = millis();

    // Quick TCP connectivity check before attempting the PubSubClient connect
    {
        WiFiClient testClient;
        Serial.printf("Testing TCP connection to %s:%u ...\n", mqtt_server, port);
        bool tcpOk = testClient.connect(mqtt_server, port);
        if (!tcpOk)
        {
            lastMqttStatus = "TCP connect failed";
            lastMqttErrorCode = -1;
            Serial.println("TCP connection to MQTT broker failed; skipping MQTT connect");
            testClient.stop();
            return false;
        }
        Serial.println("TCP connection OK");
        testClient.stop();
    }

    // Attempt MQTT connect (with Last Will set to 'offline' on availability topic)
    String availTopic = clientID + "/availability";

    // Non-blocking wait for 50ms to allow broker to process connection
    unsigned long waitStart = millis();
    while (millis() - waitStart < 50)
    {
        client.loop();
        delay(1);
    }

    // Attempt MQTT connect with LWT
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
        client.loop();
        delay(50);
        String mqttTopic = clientID + "/" + mqtt_topic_currentVal;
        client.subscribe(mqttTopic.c_str());
        publishHassDiscovery();
    }
    else
    {
        lastMqttErrorCode = client.state();
        lastMqttStatus = String("connect failed (state=") + String(lastMqttErrorCode) + String(")");
        Serial.printf("Failed to connect to MQTT server. Error: %i\n", lastMqttErrorCode);
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
    char humanMsg[64];
    snprintf(humanMsg, sizeof(humanMsg), "%s", formatWithHundredsSeparator(gasVolume).c_str());
    String mqttTopicHuman = clientID + "/" + mqtt_topic_gas;
    client.publish(mqttTopicHuman.c_str(), humanMsg);

    // Numeric raw value (Home Assistant friendly) - retained so HA can read it after restarts
    char rawMsg[32];
    float rawValue = static_cast<float>(gasVolume) / 100.0f;
    snprintf(rawMsg, sizeof(rawMsg), "%.2f", rawValue);
    String mqttTopicRaw = mqttTopicHuman + "/state";
    bool ok = client.publish(mqttTopicRaw.c_str(), rawMsg, true);

    Serial.printf("Gas volume published: %s m3 (raw: %s)\n", formatWithHundredsSeparator(gasVolume).c_str(), rawMsg);

    // If discovery hasn't been published yet, try now (first successful publish)
    if (!hassDiscoveryPublished && ok) {
        publishHassDiscovery();
    }
}

// Publish Home Assistant MQTT discovery payloads for this device
void publishHassDiscovery()
{
    if (!client.connected()) return;

    String baseHuman = clientID + "/" + mqtt_topic_gas; // human readable topic
    String baseRaw = baseHuman + "/state";                 // numeric state topic
    String currentTopic = clientID + "/" + mqtt_topic_currentVal;
    String availTopic = clientID + "/availability";

    // Track publish results
    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;

    // Device info block
    DynamicJsonDocument device(256);
    device["identifiers"]; // placeholder to ensure key exists
    device["name"] = clientID;
    device["sw_version"] = version;
    JsonArray ids = device.createNestedArray("identifiers");
    ids.add(clientID);
    device["model"] = "Gaszaehler";
    device["manufacturer"] = "DIY";

    // Sensor: total (cumulative) gas volume
    {
        DynamicJsonDocument doc(512);
        doc["name"] = String(clientID + " Gas Volume");
        doc["unique_id"] = String(clientID + "_gas_volume");
        doc["state_topic"] = baseHuman + "/state"; // Change to new state topic
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

    // Sensor: current instantaneous value
    {
        DynamicJsonDocument doc(512);
        doc["name"] = String(clientID + " Current Value");
        doc["unique_id"] = String(clientID + "_current_value");
        doc["state_topic"] = currentTopic;
        doc["unit_of_measurement"] = "m³";
        doc["value_template"] = "{{ value | float }}";
        doc["state_class"] = "measurement";
        doc["device_class"] = "gas";
        doc["icon"] = "mdi:fire";
        doc["availability_topic"] = availTopic;
        doc["device"] = device;

        String payload;
        serializeJson(doc, payload);
        String discoveryTopic = String("homeassistant/sensor/") + clientID + "_current_value/config";
        Serial.printf("Publishing discovery topic: %s (len=%u)\n", discoveryTopic.c_str(), (unsigned)payload.length());
        Serial.println(payload);
        ok2 = client.publish(discoveryTopic.c_str(), payload.c_str(), true);
        Serial.printf(" -> publish returned: %s\n", ok2 ? "true" : "false");
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

        // show cursor
        int xPos;
        xPos = 30 + cursorPosition * 12;
        if (cursorPosition == 8)
        {
            xPos += 36; // For the decimal point
            tft.drawRect(xPos, 77, 46, 2, TFT_RED);
            tft.setCursor(0, 120);
            tft.print("             save >");
            return;
        }
        else if (cursorPosition > 5)
        {
            xPos += 12; // For the decimal point
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
    DynamicJsonDocument doc(768);
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

    DynamicJsonDocument doc(256);
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
    String clientIdArg = webServer.hasArg("clientid") ? webServer.arg("clientid") : "";
    String topicArg = webServer.hasArg("topic") ? webServer.arg("topic") : "";
    String topicCurrentArg = webServer.hasArg("topic_current") ? webServer.arg("topic_current") : "";

    strlcpy(mqtt_server, serverArg.c_str(), sizeof(mqtt_server));
    strlcpy(mqtt_port, portArg.c_str(), sizeof(mqtt_port));
    strlcpy(mqtt_user, userArg.c_str(), sizeof(mqtt_user));
    strlcpy(mqtt_password, passArg.c_str(), sizeof(mqtt_password));

    // Apply optional runtime-only settings
    if (clientIdArg.length() > 0) {
        clientID = clientIdArg;
        Serial.printf("Setting clientID to: %s\n", clientID.c_str());
        // need to republish discovery under new clientID
        hassDiscoveryPublished = false;
    }
    if (topicArg.length() > 0) {
        mqtt_topic_gas = topicArg;
        Serial.printf("Setting mqtt topic base to: %s\n", mqtt_topic_gas.c_str());
        hassDiscoveryPublished = false;
    }
    if (topicCurrentArg.length() > 0) {
        mqtt_topic_currentVal = topicCurrentArg;
        Serial.printf("Setting mqtt topic current to: %s\n", mqtt_topic_currentVal.c_str());
        hassDiscoveryPublished = false;
    }

    // If currently connected, force a disconnect so reconnect attempt is clean
    if (client.connected())
    {
        Serial.println("Disconnecting existing MQTT connection before reconfiguring");
        // publish offline availability (retain) so Home Assistant marks device unavailable
        String availTopic = clientID + "/availability";
        client.publish(availTopic.c_str(), "offline", true);
        client.disconnect();
        delay(50);
    }

    saveDataToSPIFFS();
    bool connected = reconnect_mqtt();

    DynamicJsonDocument doc(512);
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
    webServer.on("/api/restart", HTTP_POST, handleRestartRequest);
    webServer.on("/api/mqtt", HTTP_POST, handleMqttConfigUpdate);
    webServer.on("/update", HTTP_POST,
                 []()
                 {
                     bool success = !Update.hasError();
                     DynamicJsonDocument doc(128);
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

void handleRestartRequest()
{
    DynamicJsonDocument doc(128);
    doc["status"] = "restarting";
    doc["message"] = "Device will restart now";
    String payload;
    serializeJson(doc, payload);
    webServer.send(200, "application/json", payload);
    delay(150);
    ESP.restart();
}