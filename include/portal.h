// Wi-Fi connection and WiFiManager config portal (captive portal "GaszaehlerAP")
#pragma once

namespace portal
{
    void begin();
    void loop();
    bool active();
    bool wifiConnected();
    // Erases the stored Wi-Fi credentials (takes effect after restart)
    void resetWifi();
}
