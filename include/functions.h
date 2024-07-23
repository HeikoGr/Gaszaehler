// functions.h
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <Arduino.h>

// declarations
String formatWithHundredsSeparator(uint32_t value);
void publishGasVolume();
void saveDataToSPIFFS();
void updateDisplay();
void captureAndSendScreenshotRLE();
void handleButton1(unsigned long currentMillis);
void MQTTcallbackReceive(char *topic, byte *payload, unsigned int length);
boolean reconnect_mqtt();
void handleButton2(unsigned long currentMillis);
void handleButtons();
void WMsaveParamsCallback();

#endif // FUNCTIONS_H