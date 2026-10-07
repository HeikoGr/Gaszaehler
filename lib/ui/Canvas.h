// Minimal RGB565 framebuffer with anti-aliased text and shapes.
// Has no Arduino dependencies, so the same drawing code runs on the ESP32 and on a PC
// (tools/screenshots renders the README screenshots with it).
#pragma once
#include <stdint.h>
#include "Font.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

enum class Align
{
    Left,
    Center,
    Right
};

class Canvas
{
public:
    Canvas(uint16_t *buffer, int width, int height) : buf(buffer), w(width), h(height) {}

    int width() const { return w; }
    int height() const { return h; }
    const uint16_t *data() const { return buf; }

    void fill(uint16_t color);
    void fillRect(int x, int y, int rw, int rh, uint16_t color);
    void fillRoundRect(int x, int y, int rw, int rh, int radius, uint16_t color);
    void drawRoundRect(int x, int y, int rw, int rh, int radius, uint16_t color);
    void fillCircle(int cx, int cy, int radius, uint16_t color);
    void hline(int x, int y, int len, uint16_t color) { fillRect(x, y, len, 1, color); }

    // Draws UTF-8 text with its baseline at y, returns the text width
    int drawText(int x, int y, const char *text, const Font &font, uint16_t color, Align align = Align::Left);
    static int textWidth(const char *text, const Font &font);

private:
    uint16_t *buf;
    int w, h;

    void blend(int x, int y, uint16_t color, uint8_t alpha); // alpha 0..255
    void roundRectCoverage(int x, int y, int rw, int rh, int radius, uint16_t color, bool outline);
};
