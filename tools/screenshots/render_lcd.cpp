// Renders every TFT screen with fixed example data on the PC, using the same
// lib/ui code as the firmware. Writes raw RGB565 frames; lcd_screenshots.py converts them to PNG.
#include <cstdio>
#include <string>
#include "Ui.h"

static uint16_t frame[UI_WIDTH * UI_HEIGHT];

static UiState baseState()
{
    UiState s;
    s.wifiConnected = true;
    s.mqttConnected = true;
    s.volume = 14198824;
    s.flowLitersPerHour = 420;
    s.secondsSinceLastPulse = 95;
    s.ssid = "MyHomeNetwork";
    s.ip = "192.168.178.162";
    s.rssi = -61;
    s.mqttServer = "192.168.178.10";
    s.mqttPort = "1883";
    s.clientId = "Gaszaehler_7C9EBD12";
    s.mqttStatus = "connected";
    s.version = "V 0.3.1 (75aa32c)";
    s.uptimeSeconds = 3 * 86400 + 4 * 3600 + 17 * 60;
    s.pulseCount = 1834;
    s.editValue = 14198824;
    s.apName = "GaszaehlerAP";
    s.apIp = "192.168.4.1";
    return s;
}

static bool write(const std::string &dir, const char *name, const UiState &s)
{
    Canvas canvas(frame, UI_WIDTH, UI_HEIGHT);
    renderUi(canvas, s);
    std::string path = dir + "/" + name + ".rgb565";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    fwrite(frame, sizeof(frame), 1, f);
    fclose(f);
    printf("%s\n", name);
    return true;
}

int main(int argc, char **argv)
{
    std::string dir = argc > 1 ? argv[1] : ".";
    UiState s = baseState();
    bool ok = true;

    s.screen = Screen::Gas;
    ok &= write(dir, "lcd-gas", s);
    s.screen = Screen::Wifi;
    ok &= write(dir, "lcd-wifi", s);
    s.screen = Screen::Mqtt;
    ok &= write(dir, "lcd-mqtt", s);
    s.screen = Screen::Info;
    ok &= write(dir, "lcd-info", s);
    s.screen = Screen::Edit;
    s.editCursor = 3;
    ok &= write(dir, "lcd-edit", s);
    s.editCursor = UI_EDIT_SAVE;
    ok &= write(dir, "lcd-edit-save", s);
    s.screen = Screen::Portal;
    s.wifiConnected = false;
    s.mqttConnected = false;
    ok &= write(dir, "lcd-portal", s);

    // error states
    UiState e = baseState();
    e.screen = Screen::Mqtt;
    e.mqttConnected = false;
    e.mqttStatus = "bad credentials (state=4)";
    ok &= write(dir, "lcd-mqtt-error", e);
    e = baseState();
    e.screen = Screen::Wifi;
    e.wifiResetPending = true;
    ok &= write(dir, "lcd-wifi-reset", e);
    return ok ? 0 : 1;
}
