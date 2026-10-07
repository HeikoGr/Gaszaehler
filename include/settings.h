// Persistent settings and meter state (LittleFS, /data.json)
#pragma once
#include <stdint.h>

struct Settings
{
    char mqttServer[40] = "";
    char mqttPort[6] = "1883";
    char mqttUser[40] = "";
    char mqttPassword[40] = "";
    char clientId[64] = "";
    char topicGas[64] = "measurement/gas";
    char topicCorrection[64] = "measurement/current";
    char webPassword[40] = ""; // empty = web UI without login
};

bool operator==(const Settings &a, const Settings &b);
inline bool operator!=(const Settings &a, const Settings &b) { return !(a == b); }

// The settings currently in use (modified by web UI and config portal)
extern Settings settings;

namespace storage
{
    // Mounts LittleFS. Data of firmware <= 0.2.0 (SPIFFS on the same partition) is migrated once.
    bool begin();
    bool load(Settings &s, uint32_t &pulses, uint32_t &offset);
    // Atomic: writes a temporary file and renames it, so a power loss never leaves a broken file
    bool save(const Settings &s, uint32_t pulses, uint32_t offset);
}
