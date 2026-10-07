// TFT display (LovyanGFX) and the two buttons. The screens themselves are drawn by lib/ui.
#pragma once

namespace display
{
    void begin(); // shows the boot screen
    void loop();  // buttons, periodic refresh
    void requestUpdate();
}
