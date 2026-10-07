// Pulse counting in a dedicated FreeRTOS task, independent of the main loop:
// blocking Wi-Fi, MQTT, web or flash operations cannot cause missed pulses.
// One pulse = 0.01 m3. Values are in 1/100 m3.
#pragma once
#include <stdint.h>

namespace meter
{
    void begin(uint32_t pulses, uint32_t offset);

    uint32_t total();  // offset + pulses
    uint32_t pulses(); // pulses since the last correction
    void snapshot(uint32_t &pulses, uint32_t &offset);

    // Sets the meter reading (offset = value, pulses = 0)
    void setTotal(uint32_t value);

    // True once for every new pulse (for display refresh)
    bool takePulseEvent();

    uint32_t flowLitersPerHour();
    uint32_t secondsSinceLastPulse(); // UINT32_MAX if no pulse since boot
}
