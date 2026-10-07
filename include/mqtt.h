// MQTT client (espMqttClient, runs its network I/O in its own task, never blocks loop())
// and Home Assistant discovery
#pragma once
#include <stdint.h>

struct Settings;

namespace mqtt
{
    void begin();
    // Reconnects, handles events from the MQTT task, publishes periodically
    void loop(bool wifiConnected);

    void publishState();
    // Settings changed: removes retained messages of the old client ID/topic and reconnects
    void reconfigure(const Settings &old);
    // Publishes "offline" and disconnects gracefully (waits up to timeoutMs)
    void shutdown(uint32_t timeoutMs);

    bool connected();
    const char *status();
    int lastError();
    uint32_t lastAttemptUptime(); // seconds since boot
}
