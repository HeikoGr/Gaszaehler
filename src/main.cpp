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
#include "icons.h"
#include "SPIFFSManager.h"
#include "functions.h" // Include the header file
#include "screenshot.h"

// Global variables and constants
SPIFFSManager spiffsManager;

// Version
const char *const version = "V 0.1.1";

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

// Upper limit for meter values (in 1/100 m3) so that pulseCount + offset never overflows
constexpr uint32_t MAX_METER_VALUE = 99999999; // 999999.99 m3

// Time window for confirming the Wi-Fi reset on the display
constexpr unsigned long WIFI_RESET_CONFIRM_WINDOW = 3000;
unsigned long wifiResetRequestTime = 0;

// Forward declarations
void publishHassDiscovery();
void clearHassDiscovery(const String &id);
void handleRestartRequest();
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
}

// WiFi Manager
WiFiManager wm;
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
WiFiManagerParameter custom_mqtt_user("username", "mqtt username", mqtt_user, 40);
WiFiManagerParameter custom_mqtt_password("password", "mqtt password (leave empty to keep)", "", 40, "type='password'");
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
            <input type="text" inputmode="decimal" pattern="[0-9\s\.,'`]+" id="consumption" name="value" required>
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
                    <label for="mqtt-user" id="lbl-user">User</label>
                    <input type="text" id="mqtt-user" name="username" autocomplete="username">
                </div>
                <div>
                    <label for="mqtt-password" id="lbl-password">Password</label>
                    <input type="password" id="mqtt-password" name="password" autocomplete="new-password">
                </div>
            </div>
            <div class="grid-two">
                <div>
                    <label for="mqtt-clientid">Client ID</label>
                    <input type="text" id="mqtt-clientid" name="clientid">
                </div>
                <div>
                    <label for="mqtt-topic" id="lbl-topic">Topic (base)</label>
                    <input type="text" id="mqtt-topic" name="topic" placeholder="measurement/gas">
                </div>
            </div>
            <div class="grid-two">
                <div></div>
                <div>
                    <label for="mqtt-topic-current" id="lbl-topic-current">Topic (current)</label>
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

let nf = new Intl.NumberFormat('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
// All state-changing requests carry this header; browsers cannot send it cross-origin
// without a CORS preflight, which the device never grants (CSRF protection).
const API_HEADERS = { 'X-Requested-With': 'gaszaehler' };

// Parse a user-entered number: accepts "12345.67", "12345,67", "12.345,67", "12,345.67", "12 345,67".
// The last '.' or ',' followed by 1-2 digits is the decimal separator, everything else is grouping.
function normalizeNumber(raw) {
    const s = raw.trim().replace(/[\s'`]/g, '');
    if (!/^[0-9.,]+$/.test(s)) return null;
    const m = s.match(/^(.*?)[.,]([0-9]{1,2})$/);
    let intPart = m ? m[1] : s;
    const frac = m ? m[2] : '';
    // grouping separators must separate blocks of exactly three digits
    if (/[.,]/.test(intPart)) {
        if (!/^[0-9]{1,3}([.,][0-9]{3})+$/.test(intPart)) return null;
        intPart = intPart.replace(/[.,]/g, '');
    }
    if (intPart === '') return null;
    return frac ? intPart + '.' + frac : intPart;
}

let lastPrefill = '';
async function refreshStatus() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) throw new Error('Status HTTP ' + response.status);
        const data = await response.json();
        volumeEl.textContent = nf.format(data.gasVolumeM3 || 0) + ' m³';
        mqttDot.classList.toggle('online', data.mqttConnected);
        const lastAttempt = data.mqttLastAttemptUptime || 0;
        mqttInfo.textContent = `${data.mqttServer}:${data.mqttPort} · ${data.mqttLastStatus} · ${t('lastTry')} ${lastAttempt}s · Topic ${data.mqttTopicGas}`;
        uptimeEl.textContent = `Uptime: ${formatUptime(data.uptimeSeconds)}`;
        const formattedVolume = nf.format(data.gasVolumeM3 || 0);
        consumptionInput.placeholder = formattedVolume;
        // Prefill with the current value unless the user has typed something of their own
        if (!consumptionInput.value || consumptionInput.value === lastPrefill) {
            consumptionInput.value = formattedVolume;
            lastPrefill = formattedVolume;
        }
        // Only update form fields if the user is not currently editing them
        if (document.activeElement !== mqttForm.server) mqttForm.server.value = data.mqttServer || '';
        if (document.activeElement !== mqttForm.port) mqttForm.port.value = data.mqttPort || '';
        if (document.activeElement !== mqttForm.username) mqttForm.username.value = data.mqttUser || '';
        // For security, show masked password as placeholder only; do not overwrite while editing
        mqttForm.password.placeholder = data.maskedPassword ? `${data.maskedPassword} (${t('passwordHint')})` : '';
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
        restartDesc: 'Remote restart of the device (will reconnect to WiFi/MQTT on boot)',
        invalidNumber: 'Invalid number',
        otaSuccess: 'Update successful. Device will restart.',
        otaFailed: 'Update failed',
        passwordHint: 'leave empty to keep',
        lastTry: 'last try',
        error: 'Error'
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
        restartDesc: 'Neustart des Geräts (verbindet sich danach wieder mit WiFi/MQTT)',
        invalidNumber: 'Ungültige Zahl',
        otaSuccess: 'Update erfolgreich. Gerät startet neu.',
        otaFailed: 'Update fehlgeschlagen',
        passwordHint: 'leer lassen = unverändert',
        lastTry: 'letzter Versuch',
        error: 'Fehler'
    }
};
// Pick saved language or fallback to browser preference
let savedLang = null;
try { savedLang = localStorage.getItem('lang'); } catch (e) {}
let currentLang = translations[savedLang] ? savedLang : ((navigator.language || 'en').toLowerCase().startsWith('de') ? 'de' : 'en');

// Call this function to change language and persist preference
function setLang(lang) {
    if (translations[lang]) {
        currentLang = lang;
        try { localStorage.setItem('lang', lang); } catch (e) {}
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
    const value = normalizeNumber(consumptionInput.value);
    if (value === null) {
        consumptionFeedback.textContent = t('invalidNumber');
        return;
    }
    consumptionFeedback.textContent = t('consuming');
    try {
        const body = new URLSearchParams({ value });
        const response = await fetch('/api/consumption', {
            method: 'POST',
            headers: { ...API_HEADERS, 'Content-Type': 'application/x-www-form-urlencoded' },
            body
        });
        if (!response.ok) {
            const err = await response.json().catch(() => ({}));
            throw new Error(err.error || 'HTTP ' + response.status);
        }
        const data = await response.json();
        consumptionFeedback.textContent = `${t('saved')}: ${nf.format(data.value)} m³`;
        consumptionInput.value = '';
        await refreshStatus();
    } catch (error) {
        consumptionFeedback.textContent = t('error') + ': ' + error.message;
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
            headers: { ...API_HEADERS, 'Content-Type': 'application/x-www-form-urlencoded' },
            body: formData
        });
        if (!response.ok) {
            const err = await response.json().catch(() => ({}));
            throw new Error(err.error || 'HTTP ' + response.status);
        }
        const data = await response.json();
        if (data.mqttConnected) {
            mqttFeedback.textContent = t('connected');
        } else {
            mqttFeedback.textContent = data.mqttLastStatus || t('settingsSavedAttempting');
        }
        await refreshStatus();
    } catch (error) {
        mqttFeedback.textContent = t('error') + ': ' + error.message;
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
        const response = await fetch('/update', { method: 'POST', headers: API_HEADERS, body: formData });
        const data = await response.json().catch(() => ({}));
        if (response.ok && data.success) {
            otaFeedback.textContent = t('otaSuccess');
            setTimeout(() => location.reload(), 8000);
        } else {
            throw new Error(data.message || t('otaFailed'));
        }
    } catch (error) {
        otaFeedback.textContent = t('error') + ': ' + error.message;
    }
});

// Restart button handler
restartBtn.addEventListener('click', async () => {
    if (!confirm(t('restartPrompt'))) return;
    restartFeedback.textContent = t('restarting');
    try {
        const response = await fetch('/api/restart', { method: 'POST', headers: API_HEADERS });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json();
        restartFeedback.textContent = data.message || t('restarting');
    } catch (error) {
        restartFeedback.textContent = t('error') + ': ' + error.message;
    }
});

// Language toggle handlers
document.getElementById('lang-en').addEventListener('click', () => { setLanguage('en'); });
document.getElementById('lang-de').addEventListener('click', () => { setLanguage('de'); });

function setLanguage(lang) {
    setLang(lang);
    document.documentElement.lang = currentLang;
    nf = new Intl.NumberFormat(currentLang === 'de' ? 'de-DE' : 'en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
    if (currentLang === 'en') {
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
        document.getElementById('lbl-user').textContent = 'User';
        document.getElementById('lbl-password').textContent = 'Password';
        document.getElementById('lbl-topic').textContent = 'Topic (base)';
        document.getElementById('lbl-topic-current').textContent = 'Topic (current)';
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
        document.getElementById('lbl-user').textContent = 'Benutzer';
        document.getElementById('lbl-password').textContent = 'Passwort';
        document.getElementById('lbl-topic').textContent = 'Topic (Basis)';
        document.getElementById('lbl-topic-current').textContent = 'Topic (Korrektur)';
    }
    // force re-formatting of the prefilled value in the new locale
    consumptionInput.value = '';
    refreshStatus();
}

// Initialize language based on saved choice or browser preference (de -> German, else English)
setLanguage(currentLang);
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

    // Initialize display first so the user sees something while Wi-Fi connects
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(2, 35);
    tft.print("Connecting WiFi...");

    // Initialize SPIFFS
    if (spiffsManager.begin())
    {
        Serial.println("SPIFFS successfully initialized");
        char storedClientID[64] = "";
        char storedTopic[64] = "";
        char storedTopicCurrent[64] = "";
        if (spiffsManager.loadData(pulseCount, offset, mqtt_server, mqtt_port, mqtt_user, mqtt_password, storedClientID, storedTopic, storedTopicCurrent))
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

    // Sensor: current instantaneous value
    {
        JsonDocument doc;
        doc["name"] = String(clientID + " Current Value");
        doc["unique_id"] = String(clientID + "_current_value");
        doc["state_topic"] = currentTopic;
        doc["unit_of_measurement"] = "m³";
        // The device clears this topic after applying a correction, so the payload may be empty
        doc["value_template"] = "{{ value | float(none) }}";
        doc["state_class"] = "total_increasing";
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

// Format a volume in 1/100 m3 as "12345.67"
String formatVolume(uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu.%02lu", (unsigned long)(value / 100), (unsigned long)(value % 100));
    return String(buf);
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
        if (wifiResetRequestTime != 0 && millis() - wifiResetRequestTime < WIFI_RESET_CONFIRM_WINDOW)
            actionBtn2 = "press again: reset >";
        else
            actionBtn2 = "        reset wifi >";
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

        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%06ld.%02ld  save", number / 100, number % 100);
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
        line1 = "value: " + formatVolume(gasVolume) + " m3";
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
            wm.resetSettings();
            saveAndRestart();
        }
        wifiResetRequestTime = millis();
        updateDisplay();
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
    strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
    if (parsePort(custom_mqtt_port.getValue()) != 0)
        strlcpy(mqtt_port, custom_mqtt_port.getValue(), sizeof(mqtt_port));
    strlcpy(mqtt_user, custom_mqtt_user.getValue(), sizeof(mqtt_user));
    // Empty password field means "keep the stored password"
    if (strlen(custom_mqtt_password.getValue()) > 0)
        strlcpy(mqtt_password, custom_mqtt_password.getValue(), sizeof(mqtt_password));
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
    webServer.send_P(200, "text/html", WEB_DASHBOARD);
}

void handleStatusRequest()
{
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
        otaRejected = !isTrustedRequest();
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
    if (otaRejected || !isTrustedRequest())
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
    webServer.begin();
    Serial.println("HTTP dashboard available on http://" + WiFi.localIP().toString());
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
