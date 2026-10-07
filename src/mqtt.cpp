#include "mqtt.h"
#include "app.h"
#include "config.h"
#include "meter.h"
#include "settings.h"
#include "util.h"
#include <ArduinoJson.h>
#include <espMqttClient.h>

namespace
{
    espMqttClient client(espMqttClientTypes::UseInternalTask::YES);

    // espMqttClient keeps pointers to these, so they must stay valid while connected
    char host[sizeof(Settings::mqttServer)];
    char user[sizeof(Settings::mqttUser)];
    char password[sizeof(Settings::mqttPassword)];
    char clientId[sizeof(Settings::clientId)];
    char availabilityTopic[sizeof(Settings::clientId) + 16];
    char correctionTopic[sizeof(Settings::clientId) + sizeof(Settings::topicCorrection) + 1];

    String statusText = "never";
    int lastErrorCode = 0;
    uint32_t lastAttempt = 0;
    bool attemptedOnce = false;
    uint32_t lastPublish = 0;
    bool reconfigurePending = false;

    // Events from the MQTT task, handled in loop() (callbacks run in the MQTT task)
    portMUX_TYPE eventLock = portMUX_INITIALIZER_UNLOCKED;
    bool connectEvent = false;
    bool disconnectEvent = false;
    espMqttClientTypes::DisconnectReason disconnectReason;
    bool correctionPending = false;
    bool correctionRetained = false;
    char correctionPayload[33];
    size_t correctionLength = 0;

    String topic(const char *suffix)
    {
        return String(settings.clientId) + "/" + suffix;
    }

    bool publish(const String &t, const char *payload, bool retain)
    {
        return client.publish(t.c_str(), 0, retain, payload) != 0;
    }

    // Copies the settings into the buffers used by the client; only while disconnected
    bool configure()
    {
        uint16_t port = parsePort(settings.mqttPort);
        if (!settings.mqttServer[0] || port == 0)
            return false;
        strlcpy(host, settings.mqttServer, sizeof(host));
        strlcpy(user, settings.mqttUser, sizeof(user));
        strlcpy(password, settings.mqttPassword, sizeof(password));
        strlcpy(clientId, settings.clientId, sizeof(clientId));
        snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", settings.clientId);
        snprintf(correctionTopic, sizeof(correctionTopic), "%s/%s", settings.clientId, settings.topicCorrection);
        client.setServer(host, port);
        client.setCredentials(user[0] ? user : nullptr, user[0] && password[0] ? password : nullptr);
        client.setClientId(clientId);
        client.setWill(availabilityTopic, 1, true, "offline");
        client.setCleanSession(true);
        client.setKeepAlive(60);
        return true;
    }

    void onConnect(bool)
    {
        portENTER_CRITICAL(&eventLock);
        connectEvent = true;
        portEXIT_CRITICAL(&eventLock);
    }

    void onDisconnect(espMqttClientTypes::DisconnectReason reason)
    {
        portENTER_CRITICAL(&eventLock);
        disconnectEvent = true;
        disconnectReason = reason;
        portEXIT_CRITICAL(&eventLock);
    }

    void onMessage(const espMqttClientTypes::MessageProperties &props, const char *t, const uint8_t *payload,
                   size_t len, size_t index, size_t total)
    {
        if (strcmp(t, correctionTopic) != 0 || index != 0 || len != total)
            return;
        // Empty payload: our own cleanup of a retained correction (see handleCorrection)
        if (total == 0)
            return;
        portENTER_CRITICAL(&eventLock);
        correctionLength = total < sizeof(correctionPayload) ? total : sizeof(correctionPayload);
        memcpy(correctionPayload, payload, correctionLength);
        correctionRetained = props.retain;
        correctionPending = true;
        portEXIT_CRITICAL(&eventLock);
    }

    void publishDiscovery()
    {
        String stateTopic = topic(settings.topicGas) + "/state";
        String availability = topic("availability");

        JsonDocument device;
        device["name"] = settings.clientId;
        device["sw_version"] = FIRMWARE_VERSION;
        device["identifiers"].to<JsonArray>().add(settings.clientId);
        device["model"] = "Gaszaehler";
        device["manufacturer"] = "DIY";

        // Sensor: total (cumulative) gas volume
        JsonDocument sensor;
        sensor["name"] = String(settings.clientId) + " Gas Volume";
        sensor["unique_id"] = String(settings.clientId) + "_gas_volume";
        sensor["state_topic"] = stateTopic;
        sensor["unit_of_measurement"] = "m³";
        sensor["state_class"] = "total_increasing";
        sensor["device_class"] = "gas";
        sensor["suggested_display_precision"] = 2;
        sensor["icon"] = "mdi:fire";
        sensor["availability_topic"] = availability;
        sensor["device"] = device;

        // Number: lets Home Assistant set the meter reading via the correction topic
        JsonDocument number;
        number["name"] = String(settings.clientId) + " Set Meter Reading";
        number["unique_id"] = String(settings.clientId) + "_meter_set";
        number["command_topic"] = topic(settings.topicCorrection);
        number["state_topic"] = stateTopic;
        number["unit_of_measurement"] = "m³";
        number["device_class"] = "gas";
        number["min"] = 0;
        number["max"] = MAX_METER_VALUE / 100.0;
        number["step"] = 0.01;
        number["mode"] = "box";
        number["entity_category"] = "config";
        number["icon"] = "mdi:counter";
        number["availability_topic"] = availability;
        number["device"] = device;

        String payload;
        serializeJson(sensor, payload);
        publish(String("homeassistant/sensor/") + settings.clientId + "_gas_volume/config", payload.c_str(), true);
        payload.clear();
        serializeJson(number, payload);
        publish(String("homeassistant/number/") + settings.clientId + "_meter_set/config", payload.c_str(), true);
        // Remove the sensor entity of firmware <= 0.1.1 that the number entity replaces
        publish(String("homeassistant/sensor/") + settings.clientId + "_current_value/config", "", true);
        Serial.println("MQTT: Home Assistant discovery published");
    }

    void clearDiscovery(const char *id)
    {
        publish(String("homeassistant/sensor/") + id + "_gas_volume/config", "", true);
        publish(String("homeassistant/sensor/") + id + "_current_value/config", "", true);
        publish(String("homeassistant/number/") + id + "_meter_set/config", "", true);
        publish(String(id) + "/availability", "", true);
        Serial.printf("MQTT: cleared Home Assistant discovery of old client ID %s\n", id);
    }

    void handleCorrection(const char *payload, size_t len, bool retained)
    {
        // The broker sets the retain flag only when replaying a stored message after (re)subscribing.
        // Applying it would reset the meter to an old value on every reconnect and drop all pulses since.
        if (retained)
        {
            Serial.println("MQTT: ignoring retained correction value (replayed by the broker)");
        }
        else
        {
            uint32_t value;
            if (len < sizeof(correctionPayload) && parseMeterValue(payload, len, value))
            {
                Serial.printf("MQTT: meter reading set to %.*s m3\n", (int)len, payload);
                meter::setTotal(value);
                app::meterChanged();
            }
            else
            {
                Serial.println("MQTT: ignoring invalid correction value");
            }
        }
        // Remove a retained copy from the broker, so it is not replayed again
        publish(String(correctionTopic), "", true);
    }
}

namespace mqtt
{
    void begin()
    {
        client.onConnect(onConnect);
        client.onDisconnect(onDisconnect);
        client.onMessage(onMessage);
    }

    void loop(bool wifiConnected)
    {
        // events from the MQTT task
        portENTER_CRITICAL(&eventLock);
        bool connectedNow = connectEvent, disconnectedNow = disconnectEvent, correction = correctionPending;
        auto reason = disconnectReason;
        char payload[sizeof(correctionPayload)];
        size_t len = correctionLength;
        bool retained = correctionRetained;
        memcpy(payload, correctionPayload, len);
        connectEvent = disconnectEvent = correctionPending = false;
        portEXIT_CRITICAL(&eventLock);

        if (disconnectedNow)
        {
            lastErrorCode = static_cast<int>(reason);
            statusText = espMqttClientTypes::disconnectReasonToString(reason);
            Serial.printf("MQTT: disconnected (%s)\n", statusText.c_str());
            app::requestDisplayUpdate();
        }
        if (connectedNow)
        {
            statusText = "connected";
            lastErrorCode = 0;
            Serial.printf("MQTT: connected to %s\n", host);
            publish(String(availabilityTopic), "online", true);
            client.subscribe(correctionTopic, 1);
            publishDiscovery();
            publishState();
            app::requestDisplayUpdate();
        }
        if (correction)
            handleCorrection(payload, len, retained);

        // (re)connect, the actual network work happens in the MQTT task
        if (client.disconnected() && wifiConnected &&
            (reconfigurePending || !attemptedOnce || millis() - lastAttempt >= MQTT_RECONNECT_INTERVAL_MS))
        {
            reconfigurePending = false;
            attemptedOnce = true;
            lastAttempt = millis();
            if (configure())
            {
                Serial.printf("MQTT: connecting to %s:%s\n", host, settings.mqttPort);
                statusText = "connecting";
                client.connect();
            }
            else
            {
                statusText = "not configured";
                lastErrorCode = -1;
            }
        }

        if (millis() - lastPublish >= PUBLISH_INTERVAL_MS)
            publishState();
    }

    void publishState()
    {
        lastPublish = millis();
        if (!client.connected())
            return;
        char value[16];
        formatVolume(value, sizeof(value), meter::total());
        String base = topic(settings.topicGas);
        // retained, so Home Assistant has the value immediately after its own restart
        publish(base + "/state", value, true);
        // same value without retain, kept for setups of firmware <= 0.0.1
        publish(base, value, false);
    }

    void reconfigure(const char *oldClientId)
    {
        if (client.connected())
        {
            if (strcmp(oldClientId, settings.clientId) != 0)
                clearDiscovery(oldClientId);
            else
                publish(String(availabilityTopic), "offline", true);
            client.disconnect(); // graceful: queued messages are sent first
        }
        reconfigurePending = true;
    }

    void shutdown(uint32_t timeoutMs)
    {
        if (!client.connected())
            return;
        publish(String(availabilityTopic), "offline", true);
        client.disconnect();
        uint32_t start = millis();
        while (!client.disconnected() && millis() - start < timeoutMs)
            delay(10);
    }

    bool connected() { return client.connected(); }
    const char *status() { return statusText.c_str(); }
    int lastError() { return lastErrorCode; }
    uint32_t lastAttemptUptime() { return lastAttempt / 1000; }
}
