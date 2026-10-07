#include "portal.h"
#include "app.h"
#include "config.h"
#include "settings.h"
#include "util.h"
#include <WiFi.h>
#include <WiFiManager.h>

namespace
{
    WiFiManager wm;
    // The values of the text fields are set from the stored settings in begin()
    WiFiManagerParameter paramServer("server", "mqtt server", "", sizeof(Settings::mqttServer) - 1);
    WiFiManagerParameter paramPort("port", "mqtt port", "", sizeof(Settings::mqttPort) - 1);
    WiFiManagerParameter paramUser("username", "mqtt username", "", sizeof(Settings::mqttUser) - 1);
    WiFiManagerParameter paramPassword("password", "mqtt password (leave empty to keep)", "",
                                       sizeof(Settings::mqttPassword) - 1, "type='password'");
    WiFiManagerParameter paramWebPassword("webpass", "web UI password, user 'admin' (optional, leave empty to keep)", "",
                                          sizeof(Settings::webPassword) - 1, "type='password'");
    uint32_t lastReconnect = 0;

    void onSaveParams()
    {
        char oldClientId[sizeof(Settings::clientId)];
        strlcpy(oldClientId, settings.clientId, sizeof(oldClientId));
        strlcpy(settings.mqttServer, paramServer.getValue(), sizeof(settings.mqttServer));
        if (parsePort(paramPort.getValue()) != 0)
            strlcpy(settings.mqttPort, paramPort.getValue(), sizeof(settings.mqttPort));
        strlcpy(settings.mqttUser, paramUser.getValue(), sizeof(settings.mqttUser));
        // Empty password fields mean "keep the stored password"
        if (paramPassword.getValue()[0])
            strlcpy(settings.mqttPassword, paramPassword.getValue(), sizeof(settings.mqttPassword));
        if (paramWebPassword.getValue()[0])
            strlcpy(settings.webPassword, paramWebPassword.getValue(), sizeof(settings.webPassword));
        Serial.printf("Portal: MQTT %s:%s (user: %s)\n", settings.mqttServer, settings.mqttPort, settings.mqttUser);
        app::settingsChanged(oldClientId);
    }
}

namespace portal
{
    void begin()
    {
        WiFi.mode(WIFI_STA);
        paramServer.setValue(settings.mqttServer, sizeof(Settings::mqttServer) - 1);
        paramPort.setValue(settings.mqttPort, sizeof(Settings::mqttPort) - 1);
        paramUser.setValue(settings.mqttUser, sizeof(Settings::mqttUser) - 1);
        wm.addParameter(&paramServer);
        wm.addParameter(&paramPort);
        wm.addParameter(&paramUser);
        wm.addParameter(&paramPassword);
        wm.addParameter(&paramWebPassword);
        wm.setConfigPortalBlocking(false);
        wm.setSaveParamsCallback(onSaveParams);
        wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_S);

        if (wm.autoConnect(AP_NAME))
            Serial.println("WiFi connected");
        else
            Serial.println("Config portal running");
        lastReconnect = millis();
    }

    void loop()
    {
        wm.process();
        if (!active() && !wifiConnected() && millis() - lastReconnect >= WIFI_RECONNECT_INTERVAL_MS)
        {
            WiFi.reconnect();
            lastReconnect = millis();
        }
    }

    bool active() { return wm.getConfigPortalActive(); }
    bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
    void resetWifi() { wm.resetSettings(); }
}
