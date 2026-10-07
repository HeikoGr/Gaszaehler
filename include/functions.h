// functions.h
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <Arduino.h>
#include <Button2.h>

// declarations
String formatVolume(uint32_t value);
void publishGasVolume();
void saveDataToSPIFFS();
void updateDisplay();
void incrementDigit();
void moveCursor();
void handleButton1Click(Button2 &btn);
void handleButton2Click(Button2 &btn);
void MQTTcallbackReceive(char *topic, byte *payload, unsigned int length);
boolean reconnect_mqtt();
void WMsaveParamsCallback();
void setupWebInterface();
void handleRootRequest();
void handleStatusRequest();
void handleConsumptionUpdate();
void handleMqttConfigUpdate();
void handleFirmwareUpload();

#endif // FUNCTIONS_H