#include "screenshot.h"

void captureAndSendScreenshotRLE(const uint16_t *frame, uint16_t width, uint16_t height)
{
    const uint32_t totalPixels = static_cast<uint32_t>(width) * height;
    Serial.println("Start of RLE Compressed Screenshot");
    Serial.printf("Original size: %u bytes\n", totalPixels * 2);
    uint32_t compressedSize = 0;
    uint16_t currentColor = frame[0];
    uint8_t count = 0;

    for (uint32_t i = 0; i < totalPixels; i++)
    {
        if (frame[i] == currentColor && count < 255)
        {
            count++;
        }
        else
        {
            Serial.printf("%02X%04X", count, currentColor);
            compressedSize += 3;
            currentColor = frame[i];
            count = 1;
        }
    }
    Serial.printf("%02X%04X", count, currentColor);
    compressedSize += 3;

    Serial.printf("\nCompressed size: %u bytes\n", compressedSize);
    Serial.println("End of RLE Compressed Screenshot");
}
