#include "screenshot.h"

void captureAndSendScreenshotRLE(TFT_eSPI& tft) {
    const uint16_t width = tft.width();
    const uint16_t height = tft.height();
    const uint32_t totalPixels = width * height;
    Serial.println("Start of RLE Compressed Screenshot");
    Serial.printf("Original size: %u bytes\n", totalPixels * 2);
    uint32_t compressedSize = 0;
    uint16_t currentColor = 0;
    uint8_t count = 0;

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint16_t pixelColor = tft.readPixel(x, y);
            if (pixelColor == currentColor && count < 255) {
                count++;
            } else {
                if (count > 0) {
                    Serial.printf("%02X%04X", count, currentColor);
                    compressedSize += 3;
                }
                currentColor = pixelColor;
                count = 1;
            }
        }
    }

    // Output the last color block
    if (count > 0) {
        Serial.printf("%02X%04X", count, currentColor);
        compressedSize += 3;
    }

    Serial.printf("\nCompressed size: %u bytes\n", compressedSize);
    Serial.println("End of RLE Compressed Screenshot");
}