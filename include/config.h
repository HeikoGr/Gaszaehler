// Compile-time configuration: pins, timings, limits
#pragma once
#include <stdint.h>

constexpr const char *FIRMWARE_VERSION = "V 0.4.0";
// "V 0.3.1 (abc1234)", "*" marks a build with uncommitted changes
const char *firmwareVersionLong();
constexpr const char *PROJECT_URL = "https://github.com/HeikoGr/Gaszaehler";

// Pins (LilyGO TTGO T-Display)
constexpr int REED_PIN = 32; // ADC1 pin, reed contact to 3.3 V with 1 kOhm pull-down
constexpr int BUTTON_1 = 35; // upper button: next page / next digit
constexpr int BUTTON_2 = 0;  // lower button: action

// Reed contact sampling (ADC 0..4095) with hysteresis against noise
constexpr int REED_THRESHOLD_LOW = 500;
constexpr int REED_THRESHOLD_HIGH = 4000;
constexpr uint32_t REED_SAMPLE_INTERVAL_MS = 50;

// Intervals
constexpr uint32_t PUBLISH_INTERVAL_MS = 60 * 1000;
constexpr uint32_t SAVE_INTERVAL_MS = 10 * 60 * 1000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 20 * 1000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 30 * 1000;
constexpr uint32_t DISPLAY_REFRESH_INTERVAL_MS = 30 * 1000;
constexpr uint32_t WIFI_RESET_CONFIRM_WINDOW_MS = 3000;
constexpr uint32_t FLOW_TIMEOUT_MS = 15 * 60 * 1000; // no pulse for 15 min = no flow
constexpr uint16_t CONFIG_PORTAL_TIMEOUT_S = 300;

// Upper limit for meter values (in 1/100 m3) so that pulses + offset never overflow
constexpr uint32_t MAX_METER_VALUE = 99999999; // 999999.99 m3

// Names
constexpr const char *AP_NAME = "GaszaehlerAP";
constexpr const char *AP_IP = "192.168.4.1";
constexpr const char *WEB_USER = "admin";
