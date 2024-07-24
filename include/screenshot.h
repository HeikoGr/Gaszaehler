#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <Arduino.h>
#include <TFT_eSPI.h>

void captureAndSendScreenshotRLE(TFT_eSPI& tft);

#endif // SCREENSHOT_H