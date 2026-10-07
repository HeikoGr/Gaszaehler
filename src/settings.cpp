#include "settings.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <string.h>

Settings settings;

namespace
{
    const char *DATA_FILE = "/data.json";
    const char *TMP_FILE = "/data.json.tmp";

    bool writeFile(fs::FS &fs, const char *payload, size_t len)
    {
        File file = fs.open(TMP_FILE, FILE_WRITE);
        if (!file)
        {
            Serial.println("Storage: cannot open file for writing");
            return false;
        }
        bool ok = file.write(reinterpret_cast<const uint8_t *>(payload), len) == len;
        file.close();
        // LittleFS rename atomically replaces the old file
        if (!ok || !fs.rename(TMP_FILE, DATA_FILE))
        {
            Serial.println("Storage: writing data failed");
            fs.remove(TMP_FILE);
            return false;
        }
        return true;
    }

    // Reads the data file of firmware <= 0.2.0 from SPIFFS, returns an empty string if there is none
    String readLegacySpiffs()
    {
        String data;
        if (!SPIFFS.begin(false))
            return data;
        const char *path = SPIFFS.exists(DATA_FILE) ? DATA_FILE : (SPIFFS.exists(TMP_FILE) ? TMP_FILE : nullptr);
        if (path)
        {
            File file = SPIFFS.open(path, FILE_READ);
            data = file.readString();
            file.close();
        }
        SPIFFS.end();
        return data;
    }

    void copyIfPresent(JsonVariantConst v, char *dest, size_t len, bool allowEmpty)
    {
        if (!v.is<const char *>())
            return;
        const char *val = v.as<const char *>();
        if (allowEmpty || strlen(val) > 0)
            strlcpy(dest, val, len);
    }
}

bool operator==(const Settings &a, const Settings &b)
{
    return strcmp(a.mqttServer, b.mqttServer) == 0 && strcmp(a.mqttPort, b.mqttPort) == 0 &&
           strcmp(a.mqttUser, b.mqttUser) == 0 && strcmp(a.mqttPassword, b.mqttPassword) == 0 &&
           strcmp(a.clientId, b.clientId) == 0 && strcmp(a.topicGas, b.topicGas) == 0 &&
           strcmp(a.webPassword, b.webPassword) == 0;
}

namespace storage
{
    bool begin()
    {
        if (LittleFS.begin(false))
            return true;

        // No LittleFS yet: rescue the SPIFFS data, then format the partition as LittleFS
        String legacy = readLegacySpiffs();
        Serial.printf("Storage: no LittleFS found, formatting%s\n", legacy.length() ? " and migrating SPIFFS data" : "");
        if (!LittleFS.begin(true))
        {
            Serial.println("Storage: mounting LittleFS failed");
            return false;
        }
        if (legacy.length() && writeFile(LittleFS, legacy.c_str(), legacy.length()))
            Serial.println("Storage: SPIFFS data migrated to LittleFS");
        return true;
    }

    bool load(Settings &s, uint32_t &pulses, uint32_t &offset)
    {
        const char *path = LittleFS.exists(DATA_FILE) ? DATA_FILE : (LittleFS.exists(TMP_FILE) ? TMP_FILE : nullptr);
        if (!path)
        {
            Serial.println("Storage: no saved data");
            return false;
        }
        File file = LittleFS.open(path, FILE_READ);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error)
        {
            Serial.printf("Storage: invalid data file (%s)\n", error.c_str());
            return false;
        }

        if (doc["count"].is<uint32_t>())
            pulses = doc["count"].as<uint32_t>();
        if (doc["offset"].is<uint32_t>())
            offset = doc["offset"].as<uint32_t>();
        // keys are kept compatible with firmware <= 0.2.0
        copyIfPresent(doc["mqtt_server"], s.mqttServer, sizeof(s.mqttServer), true);
        copyIfPresent(doc["mqtt_port"], s.mqttPort, sizeof(s.mqttPort), false);
        copyIfPresent(doc["mqtt_user"], s.mqttUser, sizeof(s.mqttUser), true);
        copyIfPresent(doc["mqtt_password"], s.mqttPassword, sizeof(s.mqttPassword), true);
        copyIfPresent(doc["mqtt_clientid"], s.clientId, sizeof(s.clientId), false);
        copyIfPresent(doc["mqtt_topic_gas"], s.topicGas, sizeof(s.topicGas), false);
        // "mqtt_topic_current" of firmware <= 0.3.1 is ignored: the set topic is now <topic>/set
        copyIfPresent(doc["web_password"], s.webPassword, sizeof(s.webPassword), true);

        Serial.printf("Storage: loaded pulses=%lu offset=%lu server=%s:%s user=%s password=%s\n",
                      (unsigned long)pulses, (unsigned long)offset, s.mqttServer, s.mqttPort, s.mqttUser,
                      s.mqttPassword[0] ? "********" : "(none)");
        return true;
    }

    bool save(const Settings &s, uint32_t pulses, uint32_t offset)
    {
        JsonDocument doc;
        doc["count"] = pulses;
        doc["offset"] = offset;
        doc["mqtt_server"] = s.mqttServer;
        doc["mqtt_port"] = s.mqttPort;
        doc["mqtt_user"] = s.mqttUser;
        doc["mqtt_password"] = s.mqttPassword;
        doc["mqtt_clientid"] = s.clientId;
        doc["mqtt_topic_gas"] = s.topicGas;
        doc["web_password"] = s.webPassword;
        String payload;
        serializeJson(doc, payload);
        if (!writeFile(LittleFS, payload.c_str(), payload.length()))
            return false;
        Serial.printf("Storage: saved pulses=%lu offset=%lu\n", (unsigned long)pulses, (unsigned long)offset);
        return true;
    }
}
