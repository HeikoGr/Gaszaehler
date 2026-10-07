#include "Ui.h"
#include <stdio.h>
#include <string.h>

#include "fonts/font_small.h"
#include "fonts/font_body.h"
#include "fonts/font_title.h"
#include "fonts/font_digits.h"
#include "fonts/font_big.h"

namespace
{
    // Palette matches the web dashboard
    constexpr uint16_t BG = rgb565(0, 0, 0);
    constexpr uint16_t SURFACE = rgb565(17, 24, 39);
    constexpr uint16_t LINE = rgb565(40, 52, 72);
    constexpr uint16_t TEXT = rgb565(241, 245, 249);
    constexpr uint16_t MUTED = rgb565(148, 163, 184);
    constexpr uint16_t ACCENT = rgb565(250, 204, 21);
    constexpr uint16_t GOOD = rgb565(34, 197, 94);
    constexpr uint16_t BAD = rgb565(239, 68, 68);
    constexpr uint16_t DECIMALS = rgb565(248, 113, 113); // red like the decimal drums of a gas meter

    constexpr int MARGIN = 8;
    constexpr int RIGHT = UI_WIDTH - 6; // button hints end here, next to the physical buttons
    constexpr int HEADER_LINE = 24;
    constexpr int FOOTER_LINE = 112;
    constexpr int COLUMN_2 = 160;

    // Copies text into out, shortened with an ellipsis so that it fits into maxWidth
    const char *fitText(char *out, size_t outSize, const char *text, const Font &font, int maxWidth)
    {
        snprintf(out, outSize, "%s", text);
        if (Canvas::textWidth(out, font) <= maxWidth)
            return out;
        size_t len = strlen(out);
        while (len > 0)
        {
            // drop one UTF-8 character
            do
                len--;
            while (len > 0 && (static_cast<uint8_t>(out[len]) & 0xC0) == 0x80);
            snprintf(out + len, outSize - len, "…");
            if (Canvas::textWidth(out, font) <= maxWidth)
                break;
        }
        return out;
    }

    void formatDuration(char *out, size_t size, uint32_t seconds)
    {
        uint32_t minutes = seconds / 60, hours = minutes / 60, days = hours / 24;
        if (days > 0)
            snprintf(out, size, "%lud %luh %lum", (unsigned long)days, (unsigned long)(hours % 24), (unsigned long)(minutes % 60));
        else if (hours > 0)
            snprintf(out, size, "%luh %lum", (unsigned long)hours, (unsigned long)(minutes % 60));
        else
            snprintf(out, size, "%lum", (unsigned long)minutes);
    }

    void drawHeader(Canvas &c, const char *title, int page, const char *topHint)
    {
        int x = MARGIN + c.drawText(MARGIN, 17, title, font_title, TEXT);
        if (page >= 0)
        {
            x += 10;
            for (int i = 0; i < UI_PAGE_COUNT; i++, x += 9)
                c.fillCircle(x, 11, 2, i == page ? ACCENT : LINE);
        }
        if (topHint)
            c.drawText(RIGHT, 16, topHint, font_small, MUTED, Align::Right);
        c.hline(0, HEADER_LINE, UI_WIDTH, LINE);
    }

    void drawStatusChips(Canvas &c, const UiState &s)
    {
        int x = MARGIN + 3;
        c.fillCircle(x, 124, 3, s.wifiConnected ? GOOD : BAD);
        x += 7 + c.drawText(x + 7, 128, "WiFi", font_small, MUTED) + 12;
        c.fillCircle(x, 124, 3, s.mqttConnected ? GOOD : BAD);
        c.drawText(x + 7, 128, "MQTT", font_small, MUTED);
    }

    void drawFooter(Canvas &c, const UiState &s, const char *bottomHint, uint16_t hintColor = ACCENT)
    {
        c.hline(0, FOOTER_LINE, UI_WIDTH, LINE);
        drawStatusChips(c, s);
        if (bottomHint)
            c.drawText(RIGHT, 128, bottomHint, font_small, hintColor, Align::Right);
    }

    // Label in small grey text, value below it
    void drawField(Canvas &c, int x, int labelBaseline, const char *label, const char *value,
                   uint16_t valueColor = TEXT, int maxWidth = UI_WIDTH - 2 * MARGIN)
    {
        char buf[64];
        c.drawText(x, labelBaseline, label, font_small, MUTED);
        c.drawText(x, labelBaseline + 20, fitText(buf, sizeof(buf), value, font_body, maxWidth), font_body, valueColor);
    }

    void drawGas(Canvas &c, const UiState &s)
    {
        drawHeader(c, "Gas meter", 0, "next ›");

        char integer[12], decimals[6];
        snprintf(integer, sizeof(integer), "%lu", (unsigned long)(s.volume / 100));
        snprintf(decimals, sizeof(decimals), ".%02lu", (unsigned long)(s.volume % 100));
        c.drawText(MARGIN, 42, "Meter reading", font_small, MUTED);
        int x = MARGIN;
        x += c.drawText(x, 82, integer, font_big, TEXT);
        x += c.drawText(x, 82, decimals, font_digits, DECIMALS);
        c.drawText(x + 5, 82, "m³", font_body, MUTED);

        char line[64];
        if (s.secondsSinceLastPulse == UI_NO_PULSE)
        {
            snprintf(line, sizeof(line), "No pulse since start");
        }
        else
        {
            char ago[16];
            if (s.secondsSinceLastPulse < 60)
                snprintf(ago, sizeof(ago), "<1 min");
            else
                formatDuration(ago, sizeof(ago), s.secondsSinceLastPulse);
            snprintf(line, sizeof(line), "%lu.%02lu m³/h  ·  last pulse %s ago",
                     (unsigned long)(s.flowLitersPerHour / 1000), (unsigned long)(s.flowLitersPerHour % 1000 / 10), ago);
        }
        c.drawText(MARGIN, 103, line, font_small, MUTED);

        drawFooter(c, s, "save ›");
    }

    void drawWifi(Canvas &c, const UiState &s)
    {
        drawHeader(c, "WiFi", 1, "next ›");
        if (s.wifiConnected)
        {
            char rssi[16];
            snprintf(rssi, sizeof(rssi), "%d dBm", s.rssi);
            drawField(c, MARGIN, 42, "Network", s.ssid, TEXT, COLUMN_2 - MARGIN - 6);
            drawField(c, COLUMN_2, 42, "Signal", rssi, s.rssi > -75 ? TEXT : ACCENT);
            drawField(c, MARGIN, 81, "IP address", s.ip);
        }
        else
        {
            drawField(c, MARGIN, 42, "Network", s.ssid[0] ? s.ssid : "not configured", BAD);
            c.drawText(MARGIN, 90, "Reconnecting…", font_small, MUTED);
        }
        if (s.wifiResetPending)
            drawFooter(c, s, "press again to reset ›", BAD);
        else
            drawFooter(c, s, "reset WiFi ›", MUTED);
    }

    void drawMqtt(Canvas &c, const UiState &s)
    {
        drawHeader(c, "MQTT", 2, "next ›");
        char broker[64];
        snprintf(broker, sizeof(broker), "%s:%s", s.mqttServer[0] ? s.mqttServer : "-", s.mqttPort);
        drawField(c, MARGIN, 42, "Broker", broker, s.mqttConnected ? TEXT : BAD);
        if (!s.mqttConnected && s.mqttStatus[0])
        {
            char status[40];
            c.drawText(RIGHT, 42, fitText(status, sizeof(status), s.mqttStatus, font_small, 150), font_small, BAD, Align::Right);
        }
        drawField(c, MARGIN, 81, "Client ID", s.clientId);
        drawFooter(c, s, "save ›");
    }

    void drawInfo(Canvas &c, const UiState &s)
    {
        drawHeader(c, "Device", 3, "next ›");
        char uptime[24], pulses[16];
        formatDuration(uptime, sizeof(uptime), s.uptimeSeconds);
        snprintf(pulses, sizeof(pulses), "%lu", (unsigned long)s.pulseCount);
        drawField(c, MARGIN, 42, "Firmware", s.version);
        drawField(c, MARGIN, 81, "Uptime", uptime, TEXT, COLUMN_2 - MARGIN - 6);
        drawField(c, COLUMN_2, 81, "Pulses", pulses);
        drawFooter(c, s, "edit value ›");
    }

    void drawEdit(Canvas &c, const UiState &s)
    {
        drawHeader(c, "Set meter value", -1, "next ›");

        constexpr int CELL_W = 20, CELL_H = 32, GAP = 3, DOT_W = 10, TOP = 34;
        char digits[9];
        snprintf(digits, sizeof(digits), "%08lu", (unsigned long)(s.editValue % 100000000UL));
        int x = MARGIN;
        for (int i = 0; i < 8; i++)
        {
            if (i == 6)
            {
                c.drawText(x + DOT_W / 2 - GAP, TOP + 27, ".", font_digits, MUTED, Align::Center);
                x += DOT_W;
            }
            bool active = (i == s.editCursor);
            c.fillRoundRect(x, TOP, CELL_W, CELL_H, 4, active ? ACCENT : SURFACE);
            char digit[2] = {digits[i], 0};
            c.drawText(x + CELL_W / 2, TOP + 27, digit, font_digits, active ? BG : TEXT, Align::Center);
            x += CELL_W + GAP;
        }
        c.drawText(x + 2, TOP + 27, "m³", font_body, MUTED);

        constexpr int BTN_Y = 76, BTN_H = 26, BTN_W = 96;
        struct
        {
            const char *label;
            int pos;
            uint16_t color;
        } buttons[] = {{"Save", UI_EDIT_SAVE, GOOD}, {"Cancel", UI_EDIT_CANCEL, BAD}};
        x = MARGIN;
        for (auto &b : buttons)
        {
            bool active = (s.editCursor == b.pos);
            if (active)
                c.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, BTN_H / 2, b.color);
            else
                c.drawRoundRect(x, BTN_Y, BTN_W, BTN_H, BTN_H / 2, LINE);
            c.drawText(x + BTN_W / 2, BTN_Y + 18, b.label, font_body, active ? BG : MUTED, Align::Center);
            x += BTN_W + 10;
        }

        const char *hint = s.editCursor == UI_EDIT_SAVE ? "save ›" : s.editCursor == UI_EDIT_CANCEL ? "cancel ›" : "+1 ›";
        drawFooter(c, s, hint, s.editCursor == UI_EDIT_SAVE ? GOOD : s.editCursor == UI_EDIT_CANCEL ? BAD : ACCENT);
    }

    void drawPortal(Canvas &c, const UiState &s)
    {
        drawHeader(c, "WiFi setup", -1, nullptr);
        drawField(c, MARGIN, 42, "1. Connect to WiFi", s.apName, ACCENT);
        drawField(c, MARGIN, 81, "2. Open in browser", s.apIp, ACCENT);
        c.hline(0, FOOTER_LINE, UI_WIDTH, LINE);
        c.drawText(MARGIN, 128, "Setup closes after 5 min without activity", font_small, MUTED);
    }

    void drawBoot(Canvas &c, const UiState &s)
    {
        c.drawText(UI_WIDTH / 2, 62, "Gas meter", font_title, TEXT, Align::Center);
        c.drawText(UI_WIDTH / 2, 84, "Connecting to WiFi…", font_small, MUTED, Align::Center);
        c.drawText(UI_WIDTH / 2, 126, s.version, font_small, LINE, Align::Center);
    }
}

void renderUi(Canvas &canvas, const UiState &state)
{
    canvas.fill(BG);
    switch (state.screen)
    {
    case Screen::Gas: drawGas(canvas, state); break;
    case Screen::Wifi: drawWifi(canvas, state); break;
    case Screen::Mqtt: drawMqtt(canvas, state); break;
    case Screen::Info: drawInfo(canvas, state); break;
    case Screen::Edit: drawEdit(canvas, state); break;
    case Screen::Portal: drawPortal(canvas, state); break;
    case Screen::Boot: drawBoot(canvas, state); break;
    }
}
