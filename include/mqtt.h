// MQTT client (espMqttClient, runs its network I/O in its own task, never blocks loop())
// and Home Assistant discovery
#pragma once
#include <stdint.h>

namespace mqtt
{
    void begin();
    // Reconnects, handles events from the MQTT task, publishes periodically
    void loop(bool wifiConnected);

    void publishState();
    // Settings changed: cleans up the old client ID in Home Assistant if needed and reconnects
    void reconfigure(const char *oldClientId);
    // Publishes "offline" and disconnects gracefully (waits up to timeoutMs)
    void shutdown(uint32_t timeoutMs);

    bool connected();
    const char *status();
    int lastError();
    uint32_t lastAttemptUptime(); // seconds since boot
}
