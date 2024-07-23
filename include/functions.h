// functions.h
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <Arduino.h>

// declarations
String formatWithHundredsSeparator(uint32_t value);
void publishGasVolume();
void saveDataToSPIFFS();
void updateDisplay();
void captureAndSendScreenshotRLE(TFT_eSPI &tft);
void incrementDigit();
void moveCursor();
void handleButton1Click(Button2 &btn);
void handleButton2Click(Button2 &btn);
void handleButton2LongPress(Button2 &btn);
void MQTTcallbackReceive(char *topic, byte *payload, unsigned int length);
boolean reconnect_mqtt();
void handleButtons();
void WMsaveParamsCallback();

#endif // FUNCTIONS_H