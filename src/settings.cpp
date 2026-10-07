#include "settings.h"
#include "config.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <esp_partition.h>
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

    // Reads the data file of firmware <= 0.2.0 from SPIFFS (empty if there is none), returns false if
    // the partition holds no SPIFFS at all
    bool readLegacySpiffs(String &data)
    {
        if (!SPIFFS.begin(false))
            return false;
        const char *path = SPIFFS.exists(DATA_FILE) ? DATA_FILE : (SPIFFS.exists(TMP_FILE) ? TMP_FILE : nullptr);
        if (path)
        {
            File file = SPIFFS.open(path, FILE_READ);
            data = file.readString();
            file.close();
        }
        SPIFFS.end();
        return true;
    }

    // True if the filesystem partition was never written (all 0xFF), e.g. on a freshly flashed device
    bool partitionBlank()
    {
        const esp_partition_t *part =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
        if (!part)
            return false;
        uint8_t buf[256];
        for (size_t pos = 0; pos < 8192 && pos < part->size; pos += sizeof(buf))
        {
            if (esp_partition_read(part, pos, buf, sizeof(buf)) != ESP_OK)
                return false;
            for (uint8_t b : buf)
                if (b != 0xFF)
                    return false;
        }
        return true;
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

        // Format only an empty partition or one holding SPIFFS data of firmware <= 0.2.0 (which is
        // migrated); a LittleFS that fails to mount is left untouched instead of silently erasing it
        String legacy;
        if (!readLegacySpiffs(legacy) && !partitionBlank())
        {
            Serial.println("Storage: mounting LittleFS failed, partition left untouched (settings are not saved)");
            return false;
        }
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

        uint32_t p = doc["count"].is<uint32_t>() ? doc["count"].as<uint32_t>() : pulses;
        uint32_t o = doc["offset"].is<uint32_t>() ? doc["offset"].as<uint32_t>() : offset;
        // a corrupt value would otherwise make offset + pulses wrap around in meter::total()
        if (static_cast<uint64_t>(p) + o <= MAX_METER_VALUE)
        {
            pulses = p;
            offset = o;
        }
        else
        {
            Serial.printf("Storage: meter reading out of range (count=%lu offset=%lu), ignored\n",
                          (unsigned long)p, (unsigned long)o);
        }
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
