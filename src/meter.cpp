#include "meter.h"
#include "config.h"
#include <Arduino.h>

namespace
{
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    // shared between the sampling task and the main loop, guarded by lock
    uint32_t pulseCount = 0;
    uint32_t offsetValue = 0;
    uint32_t lastPulseMillis = 0;
    uint32_t lastPulseInterval = 0;
    bool pulseSeen = false;
    bool pulseEvent = false;

    void sampleTask(void *)
    {
        bool closed = false;
        TickType_t lastWake = xTaskGetTickCount();
        for (;;)
        {
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(REED_SAMPLE_INTERVAL_MS));
            int value = analogRead(REED_PIN);
            if (closed && value <= REED_THRESHOLD_LOW)
            {
                closed = false;
            }
            else if (!closed && value > REED_THRESHOLD_HIGH)
            {
                closed = true;
                uint32_t now = millis();
                portENTER_CRITICAL(&lock);
                pulseCount++;
                lastPulseInterval = pulseSeen ? now - lastPulseMillis : 0;
                lastPulseMillis = now;
                pulseSeen = true;
                pulseEvent = true;
                portEXIT_CRITICAL(&lock);
            }
        }
    }
}

namespace meter
{
    void begin(uint32_t pulses, uint32_t offset)
    {
        portENTER_CRITICAL(&lock);
        pulseCount = pulses;
        offsetValue = offset;
        portEXIT_CRITICAL(&lock);
        pinMode(REED_PIN, INPUT);
        // Higher priority than loop() (1) on the same core, so it preempts long-running loop work
        xTaskCreatePinnedToCore(sampleTask, "meter", 3072, nullptr, 3, nullptr, ARDUINO_RUNNING_CORE);
    }

    uint32_t total()
    {
        portENTER_CRITICAL(&lock);
        uint32_t value = offsetValue + pulseCount;
        portEXIT_CRITICAL(&lock);
        return value;
    }

    uint32_t pulses()
    {
        portENTER_CRITICAL(&lock);
        uint32_t value = pulseCount;
        portEXIT_CRITICAL(&lock);
        return value;
    }

    void snapshot(uint32_t &pulses, uint32_t &offset)
    {
        portENTER_CRITICAL(&lock);
        pulses = pulseCount;
        offset = offsetValue;
        portEXIT_CRITICAL(&lock);
    }

    void setTotal(uint32_t value)
    {
        portENTER_CRITICAL(&lock);
        offsetValue = value;
        pulseCount = 0;
        portEXIT_CRITICAL(&lock);
    }

    bool takePulseEvent()
    {
        portENTER_CRITICAL(&lock);
        bool event = pulseEvent;
        pulseEvent = false;
        portEXIT_CRITICAL(&lock);
        return event;
    }

    uint32_t flowLitersPerHour()
    {
        portENTER_CRITICAL(&lock);
        bool seen = pulseSeen;
        uint32_t last = lastPulseMillis, interval = lastPulseInterval;
        portEXIT_CRITICAL(&lock);
        if (!seen || interval == 0)
            return 0;
        uint32_t sinceLast = millis() - last;
        if (sinceLast > FLOW_TIMEOUT_MS)
            return 0;
        // If the current gap is already longer than the last interval, the flow has slowed down
        if (sinceLast > interval)
            interval = sinceLast;
        return static_cast<uint32_t>(10ULL * 3600000ULL / interval); // 10 l per pulse
    }

    uint32_t secondsSinceLastPulse()
    {
        portENTER_CRITICAL(&lock);
        bool seen = pulseSeen;
        uint32_t last = lastPulseMillis;
        portEXIT_CRITICAL(&lock);
        return seen ? (millis() - last) / 1000 : UINT32_MAX;
    }
}
