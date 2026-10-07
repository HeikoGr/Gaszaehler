// Actions that involve several modules, implemented in main.cpp
#pragma once

struct Settings;

namespace app
{
    // The meter reading was corrected: save, publish, refresh display
    void meterChanged();
    // Settings were changed (web UI or config portal): save and reconnect MQTT
    void settingsChanged(const Settings &old);
    // Saves settings and meter state if something changed
    void save();
    // Saves, announces offline via MQTT and restarts
    void saveAndRestart();
    // Forgets Wi-Fi credentials and the web password, then restarts into the config portal
    void resetWifi();
    void requestDisplayUpdate();
}
