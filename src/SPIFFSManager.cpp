#include "SPIFFSManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

const char *SPIFFSManager::DATA_FILE = "/data.json";

SPIFFSManager::SPIFFSManager() {}

SPIFFSManager::~SPIFFSManager()
{
    end();
}

bool SPIFFSManager::begin()
{
    return mountSPIFFS();
}

void SPIFFSManager::end()
{
    SPIFFS.end();
}

bool SPIFFSManager::mountSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("Error mounting SPIFFS");
        return false;
    }
    return true;
}

bool SPIFFSManager::saveData(uint32_t pulseCount, uint32_t offset, char *mqtt_server, char *mqtt_port, char *mqtt_user, char *mqtt_password, char *mqtt_clientid, char *mqtt_topic_gas, char *mqtt_topic_current)
{
    // Open in write mode and truncate to avoid stale JSON fragments
    File file = SPIFFS.open(DATA_FILE, "w"); // truncate + write
    if (!file)
    {
        Serial.println("Error opening file for writing");
        return false;
    }

    DynamicJsonDocument doc(1024);
    doc["count"] = pulseCount;
    doc["offset"] = offset;
    doc["mqtt_server"] = mqtt_server;
    doc["mqtt_port"] = mqtt_port;
    doc["mqtt_user"] = mqtt_user;
    doc["mqtt_password"] = mqtt_password;
    // optional fields
    doc["mqtt_clientid"] = mqtt_clientid;
    doc["mqtt_topic_gas"] = mqtt_topic_gas;
    doc["mqtt_topic_current"] = mqtt_topic_current;

    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Error writing data");
        file.close();
        return false;
    }

    Serial.println("Data successfully written:");
    Serial.printf(" < Meter reading: %i\n", pulseCount);
    Serial.printf(" < Offset: %i\n", offset);
    Serial.printf(" < MQTT Server: %s\n", mqtt_server);
    Serial.printf(" < MQTT Port: %s\n", mqtt_port);
    Serial.printf(" < MQTT Username: %s\n", mqtt_user);
    Serial.printf(" < MQTT Password: %s\n", mqtt_password);

    file.close();
    return true;
}

bool SPIFFSManager::loadData(uint32_t &pulseCount, uint32_t &offset, char *mqtt_server, char *mqtt_port, char *mqtt_user, char *mqtt_password, char *mqtt_clientid, char *mqtt_topic_gas, char *mqtt_topic_current)
{
    File file = SPIFFS.open(DATA_FILE, FILE_READ);
    if (!file)
    {
        Serial.println("Error opening file for reading");
        return false;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("Error deserializing JSON data");
        return false;
    }

    // Only overwrite fields when present and non-empty; otherwise keep existing defaults
    if (doc.containsKey("count"))
        pulseCount = doc["count"].as<uint32_t>();
    if (doc.containsKey("offset"))
        offset = doc["offset"].as<uint32_t>();

    auto copyIfSet = [](JsonVariantConst v, char *dest, size_t len) {
        const char *val = v.isNull() ? nullptr : v.as<const char *>();
        if (val && strlen(val) > 0)
        {
            strlcpy(dest, val, len);
        }
    };

    copyIfSet(doc["mqtt_server"], mqtt_server, 40);
    copyIfSet(doc["mqtt_port"], mqtt_port, 6);
    copyIfSet(doc["mqtt_user"], mqtt_user, 40);
    copyIfSet(doc["mqtt_password"], mqtt_password, 40);
    copyIfSet(doc["mqtt_clientid"], mqtt_clientid, 64);
    copyIfSet(doc["mqtt_topic_gas"], mqtt_topic_gas, 64);
    copyIfSet(doc["mqtt_topic_current"], mqtt_topic_current, 64);
    
    Serial.printf(" < Meter reading: %i\n", pulseCount);
    Serial.printf(" < Offset: %i\n", offset);
    Serial.printf(" < MQTT Server: %s\n", mqtt_server);
    Serial.printf(" < MQTT Port: %s\n", mqtt_port);
    Serial.printf(" < MQTT Username: %s\n", mqtt_user);
    Serial.printf(" < MQTT Password: %s\n", mqtt_password);

    return true;
}

void SPIFFSManager::listFiles()
{
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        Serial.print("FILE: ");
        Serial.println(file.name());
        file = root.openNextFile();
    }
}