#include <Arduino.h> // entfernen für Arduino IDE
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <sstream>
#include <iomanip>
#include "lodepng.h"

// Pin-Definitionen
#define REED_PIN 32 // ADC1 Pin
#define BUTTON_1 35
#define BUTTON_2 0
#define DEBOUNCE_DELAY 300
#define HYSTERESIS_LOW 500.0
#define HYSTERESIS_HIGH 4000.0

String chipID;
String clientID;

const char *mqtt_topic_gas = "gas_meter/volume";
const char *mqtt_topic_currentVal = "gas_meter/currentVal";

// Variablen
volatile uint32_t pulseCount = 0;
uint32_t offset = 0;
char mqtt_server[40] = "192.168.0.1";
char mqtt_port[6] = "1883";
uint32_t gasVolume = 0;
int displayMode = 0;
volatile bool lastState = false;

// Vorherige Werte
uint32_t prevPulseCount = 0;
uint32_t prevOffset = 0;
char prevMqttServer[40];
char prevMqttPort[6];
bool prevWifiStatus = false;
bool prevMqttStatus = false;

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
const unsigned long publishInterval = 60000;         // 60 Sekunden
const unsigned long InterruptInterval = 50;          // 50 Millisekunden
const unsigned long buttonCheckInterval = 100;       // 100 Millisekunden
const unsigned long saveInterval = 600000;           // 10 Minuten
const unsigned long WiFireconnectIntervall = 300000; // 5 Minuten
const unsigned long MQTTreconnectIntervall = 10000;  // 10 Sekunden

// online converter: http://www.rinkydinkelectronics.com/_t_doimageconverter565.php
const unsigned short wifiIcon[] PROGMEM = {
    0x7BEF, 0x7BEF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0010 (16) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7BEF, 0x7BEF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0020 (32) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0030 (48) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0040 (64) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, // 0x0050 (80) pixels
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0060 (96) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 0x0070 (112) pixels
    0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 0x0080 (128) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0090 (144) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x00A0 (160) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x00B0 (176) pixels
    0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x00C0 (192) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 0x00D0 (208) pixels
    0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, // 0x00E0 (224) pixels
    0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x00F0 (240) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0100 (256) pixels
    0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, // 0x0110 (272) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0120 (288) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0130 (304) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0140 (320) pixels
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0150 (336) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, // 0x0160 (352) pixels
    0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, // 0x0170 (368) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0180 (384) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0190 (400) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01A0 (416) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01B0 (432) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, // 0x01C0 (448) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01D0 (464) pixels
    0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01E0 (480) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01F0 (496) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0200 (512) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0210 (528) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0220 (544) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0230 (560) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0240 (576) pixels
};

const unsigned short mqttIcon[] PROGMEM = {
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0010 (16) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, // 0x0020 (32) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0030 (48) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0040 (64) pixels
    0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0050 (80) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0060 (96) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0070 (112) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 0x0080 (128) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0090 (144) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x00A0 (160) pixels
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 0x00B0 (176) pixels
    0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x00C0 (192) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, // 0x00D0 (208) pixels
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, // 0x00E0 (224) pixels
    0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x00F0 (240) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, // 0x0100 (256) pixels
    0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, // 0x0110 (272) pixels
    0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0120 (288) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, // 0x0130 (304) pixels
    0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0140 (320) pixels
    0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0150 (336) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, // 0x0160 (352) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0170 (368) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0180 (384) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0190 (400) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01A0 (416) pixels
    0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x01B0 (432) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, // 0x01C0 (448) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01D0 (464) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x01E0 (480) pixels
    0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x01F0 (496) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0200 (512) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, // 0x0210 (528) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0220 (544) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0230 (560) pixels
    0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, // 0x0240 (576) pixels
};

// Display
TFT_eSPI tft = TFT_eSPI();

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

  if (!SPIFFS.begin(true))
  {
    Serial.println("Fehler beim Mounten von SPIFFS");
    return;
  }

  fs::File file = SPIFFS.open("/data.json", FILE_WRITE);
  if (!file)
  {
    Serial.println("Fehler beim Öffnen der Datei zum Schreiben");
    SPIFFS.end();
    return;
  }

  JsonDocument doc;
  doc["count"] = pulseCount;
  doc["offset"] = offset;
  doc["mqtt_server"] = mqtt_server;
  doc["mqtt_port"] = mqtt_port;

  serializeJson(doc, file);
  file.close();

  prevPulseCount = pulseCount;
  prevOffset = offset;
  strcpy(prevMqttServer, mqtt_server);
  strcpy(prevMqttPort, mqtt_port);

  Serial.println("Daten gespeichert");
  Serial.print(" > Zählerstand: ");
  Serial.println(pulseCount);
  Serial.print(" > Offset: ");
  Serial.println(offset);
  Serial.print(" > MQTT Server: ");
  Serial.println(mqtt_server);
  Serial.print(" > MQTT Port: ");
  Serial.println(mqtt_port);

  SPIFFS.end();
}

void saveParamCallback()
{
  Serial.println("[CALLBACK] saveParamCallback fired");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());

  saveDataToSPIFFS();
}

// Funktion zum Laden des Zählerstands aus SPIFFS
void loadDataFromSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("Fehler beim Mounten von SPIFFS");
    return;
  }

  fs::File file = SPIFFS.open("/data.json", FILE_READ);
  if (!file)
  {
    Serial.println("Fehler beim Öffnen der Datei zum Lesen");
    SPIFFS.end();
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.println("Fehler beim Deserialisieren der JSON-Daten");
    SPIFFS.end();
    return;
  }

  pulseCount = doc["count"].as<uint32_t>();
  offset = doc["offset"].as<uint32_t>();
  strlcpy(mqtt_server, doc["mqtt_server"] | "", sizeof(mqtt_server));
  strlcpy(mqtt_port, doc["mqtt_port"] | "", sizeof(mqtt_server));

  Serial.println("Daten geladen:");
  Serial.print(" < Zählerstand: ");
  Serial.println(pulseCount);
  Serial.print(" < Offset: ");
  Serial.println(offset);
  Serial.print(" < MQTT Server: ");
  Serial.println(mqtt_server);
  Serial.print(" < MQTT Port: ");
  Serial.println(mqtt_port);

  SPIFFS.end();
}

// Funktion zur Aktualisierung des Displays
void updateDisplay()
{

  tft.setCursor(0, 0);
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 240, 26, TFT_DARKGREY);  // Statusleiste
  tft.fillRect(0, 110, 240, 1, TFT_DARKGREY); // Akzentlinie

  switch (displayMode)
  {
  case 0:
    gasVolume = pulseCount + offset;
    tft.setCursor(0, 2);
    tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
    tft.setTextSize(3);
    tft.print("Gaszaehler");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 35);
    tft.printf("Gas: %s m3", formatWithHundredsSeparator(gasVolume).c_str());
    tft.setCursor(0, 115);
    tft.print("    publish & save >");
    break;
  case 1:
    // tft.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "Verbunden" : "Getrennt");
    tft.setCursor(0, 3);
    tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
    tft.setTextSize(3);
    tft.print("WiFi");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 35);
    tft.printf("SSID: %s", WiFi.SSID().c_str());
    tft.setCursor(0, 65);
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
    tft.setCursor(0, 115);
    tft.print(" reset wifi & mqtt >");
    break;
  case 2:
    tft.setCursor(0, 3);
    tft.setTextColor(TFT_BLACK, TFT_DARKGREY);
    tft.setTextSize(3);
    tft.print("MQTT");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 35);
    tft.printf("IP: %s", mqtt_server);
    break;
  }
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
  Serial.printf("Gasvolumen veröffentlicht: %s m3\n", formatWithHundredsSeparator(gasVolume));
}

// Funktion zur Wiederverbindung mit dem MQTT-Broker
boolean reconnect_mqtt()
{
  Serial.print("Versuche MQTT-Verbindung ... \n");

  if (client.connect(clientID.c_str()))
  {
    Serial.printf("MQTT verbunden mit %s\n", mqtt_server);
    String mqttTopic = clientID + "/" + mqtt_topic_currentVal;
    client.subscribe(mqttTopic.c_str());
    publishGasVolume();
  }
  else
  {
    Serial.println("Verbindung zu MQTT-Server fehlgeschlagen.");
  }
  return client.connected();
}

// Hilfsfunktion zum Konvertieren von RGB565 zu RGB888
void convertRGB565toRGB888(uint16_t rgb565, uint8_t &r, uint8_t &g, uint8_t &b)
{
  r = ((rgb565 >> 11) & 0x1F) * 255 / 31;
  g = ((rgb565 >> 5) & 0x3F) * 255 / 63;
  b = (rgb565 & 0x1F) * 255 / 31;
}

void sendScreenshotAsRGB888()
{
  uint16_t w = tft.width();
  uint16_t h = tft.height();
  uint16_t *buffer = (uint16_t *)malloc(w * h * sizeof(uint16_t));

  if (buffer)
  {
    tft.readRect(0, 0, w, h, buffer);

    for (int i = 0; i < w * h; i++)
    {
      uint8_t r, g, b;
      convertRGB565toRGB888(buffer[i], r, g, b);
      Serial.printf("%02X%02X%02X ", r, g, b);

      // Sende alle 20 Pixel (60 Hexzeichen) als eine Nachricht
      if ((i + 1) % 20 == 0 || i == w * h - 1)
      {
        Serial.println();
      }
    }

    free(buffer);
  }
  else
  {
    Serial.println("Fehler: Nicht genug Speicher für Screenshot");
  }
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

  //sendScreenshotAsRGB888();

  if (displayMode == 0 || displayMode == 1)
  {

    // MQTT-Verbindung prüfen
    if (WiFi.status() == WL_CONNECTED && !client.connected())
    {
      if (reconnect_mqtt())
      {
        lastMQTTreconnectTime = currentMillis;
      }
    }

    saveDataToSPIFFS();
    publishGasVolume();
    updateDisplay();
  }

  if (displayMode == 1)
  {
    wm.resetSettings();
    delay(20);
    ESP.restart();
  }

  lastButton2PressTime = currentMillis;
}
void drawStatus()
{

  // Display 135X240,  Paramater der tft Funktionen: x, y ,w ,h
  // Überprüfe den aktuellen WiFi-Status
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  uint16_t wifiColor = wifiConnected ? TFT_GREEN : TFT_RED;
  tft.fillRect(189, 1, 24, 24, wifiColor);
  tft.pushImage(189, 1, 24, 24, wifiIcon, TFT_WHITE);
  prevWifiStatus = wifiConnected; // Aktualisiere vorherigen Status

  bool mqttConnected = client.connected();
  // Wenn sich der MQTT-Status geändert hat, zeichne das Icon neu
  uint16_t mqttColor = mqttConnected ? TFT_GREEN : TFT_RED;
  tft.fillRect(215, 1, 24, 24, mqttColor);
  tft.pushImage(215, 1, 24, 24, mqttIcon, TFT_WHITE);
  prevMqttStatus = mqttConnected; // Aktualisiere vorherigen Status
}

void handleButtons()
{
  unsigned long currentMillis = millis();

  handleButton1(currentMillis);
  handleButton2(currentMillis);

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

  // SPIFFS initialisieren
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS-Initialisierung fehlgeschlagen!");
    return;
  }

  loadDataFromSPIFFS();

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

  // MQTT-Client einrichten
  client.setServer(mqtt_server, static_cast<uint16_t>(std::stoi(mqtt_port)));
  client.setCallback(callback);

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

  updateDisplay();

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
      lastMQTTreconnectTime = currentMillis;
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