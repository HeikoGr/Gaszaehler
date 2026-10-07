// Web dashboard, JSON API and OTA update on port 80
#pragma once

namespace web
{
    void begin();
    // Serves requests; the server only runs while the config portal is inactive (both use port 80)
    void loop(bool portalActive, bool wifiConnected);
}
