#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <WiFiManager.h>
#include <PubSubClient.h>

void setupWiFi();
void setupMQTT();
bool reconnectMQTT();
void publishGasVolume(uint32_t gasVolume);

#endif // WIFI_MQTT_H