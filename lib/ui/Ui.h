// Screen layouts of the 240x135 TFT. Pure functions of UiState, no hardware access:
// main.cpp fills UiState from the device state, tools/screenshots from fixtures.
#pragma once
#include <stdint.h>
#include "Canvas.h"

constexpr int UI_WIDTH = 240;
constexpr int UI_HEIGHT = 135;

enum class Screen
{
    Gas,
    Wifi,
    Mqtt,
    Info,
    Edit,
    Portal,
    Boot
};

constexpr int UI_PAGE_COUNT = 4;        // Gas, Wifi, Mqtt, Info are cycled with the top button
constexpr int UI_EDIT_SAVE = 8;         // edit cursor positions 0..7 are digits, then save, cancel
constexpr int UI_EDIT_CANCEL = 9;
constexpr int UI_EDIT_POSITIONS = 10;
constexpr uint32_t UI_NO_PULSE = 0xFFFFFFFF;

struct UiState
{
    Screen screen = Screen::Gas;
    bool wifiConnected = false;
    bool mqttConnected = false;

    uint32_t volume = 0;                       // 1/100 m3
    uint32_t flowLitersPerHour = 0;            // current flow estimate
    uint32_t secondsSinceLastPulse = UI_NO_PULSE;

    const char *ssid = "";
    const char *ip = "";
    int rssi = 0;
    bool wifiResetPending = false;             // waiting for the confirming second click

    const char *mqttServer = "";
    const char *mqttPort = "";
    const char *clientId = "";
    const char *mqttStatus = "";

    const char *version = "";
    uint32_t uptimeSeconds = 0;
    uint32_t pulseCount = 0;

    uint32_t editValue = 0;                    // 1/100 m3
    int editCursor = 0;                        // 0..UI_EDIT_POSITIONS-1

    const char *apName = "";
    const char *apIp = "";
};

void renderUi(Canvas &canvas, const UiState &state);
