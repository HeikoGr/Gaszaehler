// Gas meter: counts reed contact pulses of a diaphragm gas meter and publishes them via MQTT.
// Modules: meter (pulse counting task), settings (LittleFS), portal (Wi-Fi/WiFiManager),
// mqtt (espMqttClient, Home Assistant discovery), web (dashboard/API/OTA), display (TFT + buttons)
#include <Arduino.h>
#include "app.h"
#include "config.h"
#include "display.h"
#include "meter.h"
#include "mqtt.h"
#include "portal.h"
#include "settings.h"
#include "web.h"

namespace
{
    // State as last written to flash, to save only when something changed
    Settings savedSettings;
    uint32_t savedPulses = 0;
    uint32_t savedOffset = 0;
    uint32_t lastSave = 0;
}

namespace app
{
    void save()
    {
        lastSave = millis();
        uint32_t pulses, offset;
        meter::snapshot(pulses, offset);
        if (pulses == savedPulses && offset == savedOffset && settings == savedSettings)
            return;
        if (storage::save(settings, pulses, offset))
        {
            savedSettings = settings;
            savedPulses = pulses;
            savedOffset = offset;
        }
    }

    void meterChanged()
    {
        save();
        mqtt::publishState();
        display::requestUpdate();
    }

    void settingsChanged(const Settings &old)
    {
        save();
        mqtt::reconfigure(old);
        display::requestUpdate();
    }

    void saveAndRestart()
    {
        save();
        mqtt::shutdown(1000);
        delay(100);
        ESP.restart();
    }

    void resetWifi()
    {
        // Physical access is the recovery path for a forgotten web password
        settings.webPassword[0] = '\0';
        portal::resetWifi();
        saveAndRestart();
    }

    void requestDisplayUpdate() { display::requestUpdate(); }
}

void setup()
{
    Serial.begin(115200);
    Serial.printf("Starting gas meter %s\n", FIRMWARE_VERSION);

    // Display first, so the user sees something while Wi-Fi connects
    display::begin();

    uint32_t pulses = 0, offset = 0;
    if (storage::begin())
        storage::load(settings, pulses, offset);
    if (!settings.clientId[0])
    {
        // Individual default client ID from the chip ID
        snprintf(settings.clientId, sizeof(settings.clientId), "Gaszaehler_%lX", (unsigned long)(uint32_t)ESP.getEfuseMac());
    }
    savedSettings = settings;
    savedPulses = pulses;
    savedOffset = offset;

    meter::begin(pulses, offset);
    mqtt::begin();
    web::begin();
    portal::begin();
    Serial.println("Setup completed");
}

void loop()
{
    portal::loop();
    bool wifi = portal::wifiConnected();
    web::loop(portal::active(), wifi);
    mqtt::loop(wifi);
    display::loop();

    if (millis() - lastSave >= SAVE_INTERVAL_MS)
        app::save();
}
