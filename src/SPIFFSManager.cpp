#include "SPIFFSManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

const char *SPIFFSManager::DATA_FILE = "/data.json";
const char *SPIFFSManager::TMP_FILE = "/data.json.tmp";

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

bool SPIFFSManager::saveData(uint32_t pulseCount, uint32_t offset, char *mqtt_server, char *mqtt_port, char *mqtt_user, char *mqtt_password, char *mqtt_clientid, char *mqtt_topic_gas, char *mqtt_topic_current, char *web_password)
{
    // Write to a temporary file first, so a power loss during writing never destroys the last good state
    File file = SPIFFS.open(TMP_FILE, FILE_WRITE);
    if (!file)
    {
        Serial.println("Error opening file for writing");
        return false;
    }

    JsonDocument doc;
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
    doc["web_password"] = web_password;

    if (serializeJson(doc, file) == 0)
    {
        Serial.println("Error writing data");
        file.close();
        SPIFFS.remove(TMP_FILE);
        return false;
    }
    file.close();

    // SPIFFS cannot rename onto an existing file; loadData() falls back to TMP_FILE
    // if the device loses power between remove() and rename()
    SPIFFS.remove(DATA_FILE);
    if (!SPIFFS.rename(TMP_FILE, DATA_FILE))
    {
        Serial.println("Error renaming data file");
        return false;
    }

    Serial.println("Data successfully written:");
    Serial.printf(" < Meter reading: %u\n", pulseCount);
    Serial.printf(" < Offset: %u\n", offset);
    Serial.printf(" < MQTT Server: %s\n", mqtt_server);
    Serial.printf(" < MQTT Port: %s\n", mqtt_port);
    Serial.printf(" < MQTT Username: %s\n", mqtt_user);
    Serial.printf(" < MQTT Password: %s\n", strlen(mqtt_password) ? "********" : "(none)");

    return true;
}

bool SPIFFSManager::loadData(uint32_t &pulseCount, uint32_t &offset, char *mqtt_server, char *mqtt_port, char *mqtt_user, char *mqtt_password, char *mqtt_clientid, char *mqtt_topic_gas, char *mqtt_topic_current, char *web_password)
{
    const char *path = DATA_FILE;
    if (!SPIFFS.exists(DATA_FILE) && SPIFFS.exists(TMP_FILE))
    {
        Serial.println("Data file missing, recovering from temporary file");
        path = TMP_FILE;
    }
    File file = SPIFFS.open(path, FILE_READ);
    if (!file)
    {
        Serial.println("Error opening file for reading");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("Error deserializing JSON data");
        return false;
    }

    // Only overwrite fields when present and non-empty; otherwise keep existing defaults
    if (doc["count"].is<uint32_t>())
        pulseCount = doc["count"].as<uint32_t>();
    if (doc["offset"].is<uint32_t>())
        offset = doc["offset"].as<uint32_t>();

    auto copyIfSet = [](JsonVariantConst v, char *dest, size_t len) {
        const char *val = v.isNull() ? nullptr : v.as<const char *>();
        if (val && strlen(val) > 0)
        {
            strlcpy(dest, val, len);
        }
    };

    // buffer sizes must match the globals in main.cpp
    copyIfSet(doc["mqtt_server"], mqtt_server, 40);
    copyIfSet(doc["mqtt_port"], mqtt_port, 6);
    copyIfSet(doc["mqtt_user"], mqtt_user, 40);
    copyIfSet(doc["mqtt_password"], mqtt_password, 40);
    copyIfSet(doc["mqtt_clientid"], mqtt_clientid, 64);
    copyIfSet(doc["mqtt_topic_gas"], mqtt_topic_gas, 64);
    copyIfSet(doc["mqtt_topic_current"], mqtt_topic_current, 64);
    copyIfSet(doc["web_password"], web_password, 40);
    
    Serial.printf(" < Meter reading: %u\n", pulseCount);
    Serial.printf(" < Offset: %u\n", offset);
    Serial.printf(" < MQTT Server: %s\n", mqtt_server);
    Serial.printf(" < MQTT Port: %s\n", mqtt_port);
    Serial.printf(" < MQTT Username: %s\n", mqtt_user);
    Serial.printf(" < MQTT Password: %s\n", strlen(mqtt_password) ? "********" : "(none)");

    return true;
}
