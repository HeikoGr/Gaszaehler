#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <Arduino.h>

// Dumps a RGB565 framebuffer RLE-compressed to the serial console
// (decode with contrib/rle_to_rgb565_to_png.html). For README screenshots use
// tools/screenshots instead, it renders the screens on the PC.
void captureAndSendScreenshotRLE(const uint16_t *frame, uint16_t width, uint16_t height);

#endif // SCREENSHOT_H
