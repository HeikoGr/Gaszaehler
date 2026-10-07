#include "Canvas.h"

namespace
{
    // Decodes one UTF-8 code point and advances the pointer (invalid bytes yield '?')
    uint32_t nextCodePoint(const char *&p)
    {
        uint8_t c = static_cast<uint8_t>(*p++);
        if (c < 0x80)
            return c;
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
        if (extra < 0)
            return '?';
        uint32_t cp = c & (0x3F >> extra);
        for (int i = 0; i < extra; i++)
        {
            uint8_t cc = static_cast<uint8_t>(*p);
            if ((cc & 0xC0) != 0x80)
                return '?';
            cp = (cp << 6) | (cc & 0x3F);
            p++;
        }
        return cp;
    }

    const Glyph *findGlyph(const Font &font, uint32_t cp)
    {
        int lo = 0, hi = font.glyphCount - 1;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            uint16_t code = font.glyphs[mid].code;
            if (code == cp)
                return &font.glyphs[mid];
            if (code < cp)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return cp == '?' ? nullptr : findGlyph(font, '?');
    }

    // Number of 4x4 subsamples (0..16) of pixel (px, py) inside the rounded rectangle
    int roundRectSamples(int px, int py, int x, int y, int w, int h, int r)
    {
        if (w <= 0 || h <= 0 || px < x || py < y || px >= x + w || py >= y + h)
            return 0;
        if (r * 2 > w)
            r = w / 2;
        if (r * 2 > h)
            r = h / 2;
        // corner centre this pixel belongs to, if any (coordinates doubled to stay integer)
        int cx2, cy2;
        if (px < x + r)
            cx2 = 2 * (x + r);
        else if (px >= x + w - r)
            cx2 = 2 * (x + w - r);
        else
            return 16;
        if (py < y + r)
            cy2 = 2 * (y + r);
        else if (py >= y + h - r)
            cy2 = 2 * (y + h - r);
        else
            return 16;
        // subsample positions at px + (2i+1)/8, compared in units of 1/8 pixel
        int inside = 0;
        const int r8 = r * 8;
        for (int sy = 0; sy < 4; sy++)
        {
            int dy = (py * 8 + 2 * sy + 1) - cy2 * 4;
            for (int sx = 0; sx < 4; sx++)
            {
                int dx = (px * 8 + 2 * sx + 1) - cx2 * 4;
                if (dx * dx + dy * dy <= r8 * r8)
                    inside++;
            }
        }
        return inside;
    }
}

void Canvas::fill(uint16_t color)
{
    for (int i = 0; i < w * h; i++)
        buf[i] = color;
}

void Canvas::fillRect(int x, int y, int rw, int rh, uint16_t color)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            buf[yy * w + xx] = color;
}

void Canvas::blend(int x, int y, uint16_t color, uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= w || y >= h || alpha == 0)
        return;
    uint16_t &dst = buf[y * w + x];
    if (alpha == 255)
    {
        dst = color;
        return;
    }
    int r = ((color >> 11) * alpha + (dst >> 11) * (255 - alpha) + 127) / 255;
    int g = (((color >> 5) & 0x3F) * alpha + ((dst >> 5) & 0x3F) * (255 - alpha) + 127) / 255;
    int b = ((color & 0x1F) * alpha + (dst & 0x1F) * (255 - alpha) + 127) / 255;
    dst = static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void Canvas::roundRectCoverage(int x, int y, int rw, int rh, int radius, uint16_t color, bool outline)
{
    for (int py = y; py < y + rh; py++)
    {
        for (int px = x; px < x + rw; px++)
        {
            int samples = roundRectSamples(px, py, x, y, rw, rh, radius);
            if (outline)
                samples -= roundRectSamples(px, py, x + 1, y + 1, rw - 2, rh - 2, radius > 1 ? radius - 1 : 0);
            if (samples > 0)
                blend(px, py, color, static_cast<uint8_t>(samples * 255 / 16));
        }
    }
}

void Canvas::fillRoundRect(int x, int y, int rw, int rh, int radius, uint16_t color)
{
    roundRectCoverage(x, y, rw, rh, radius, color, false);
}

void Canvas::drawRoundRect(int x, int y, int rw, int rh, int radius, uint16_t color)
{
    roundRectCoverage(x, y, rw, rh, radius, color, true);
}

void Canvas::fillCircle(int cx, int cy, int radius, uint16_t color)
{
    roundRectCoverage(cx - radius, cy - radius, 2 * radius + 1, 2 * radius + 1, radius, color, false);
}

int Canvas::textWidth(const char *text, const Font &font)
{
    int width = 0;
    for (const char *p = text; *p;)
    {
        const Glyph *g = findGlyph(font, nextCodePoint(p));
        if (g)
            width += g->advance;
    }
    return width;
}

int Canvas::drawText(int x, int y, const char *text, const Font &font, uint16_t color, Align align)
{
    int width = textWidth(text, font);
    if (align == Align::Center)
        x -= width / 2;
    else if (align == Align::Right)
        x -= width;

    for (const char *p = text; *p;)
    {
        const Glyph *g = findGlyph(font, nextCodePoint(p));
        if (!g)
            continue;
        const uint8_t *data = font.bitmap + g->offset;
        for (int i = 0; i < g->w * g->h; i++)
        {
            uint8_t a4 = (i & 1) ? (data[i >> 1] & 0x0F) : (data[i >> 1] >> 4);
            if (a4)
                blend(x + g->dx + i % g->w, y + g->dy + i / g->w, color, a4 * 17);
        }
        x += g->advance;
    }
    return width;
}
