#include "display.h"
#include "app.h"
#include "config.h"
#include "meter.h"
#include "mqtt.h"
#include "portal.h"
#include "settings.h"
#include "Ui.h"
#include <Button2.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>

namespace
{
    // LilyGO TTGO T-Display: ST7789 135x240 on HSPI, backlight on GPIO 4
    class TDisplay : public lgfx::LGFX_Device
    {
        lgfx::Panel_ST7789 panel;
        lgfx::Bus_SPI bus;
        lgfx::Light_PWM light;

    public:
        TDisplay()
        {
            auto b = bus.config();
            b.spi_host = HSPI_HOST;
            b.spi_mode = 0;
            b.freq_write = 40000000;
            b.freq_read = 14000000;
            b.spi_3wire = true;
            b.pin_sclk = 18;
            b.pin_mosi = 19;
            b.pin_miso = -1;
            b.pin_dc = 16;
            bus.config(b);
            panel.setBus(&bus);

            auto p = panel.config();
            p.pin_cs = 5;
            p.pin_rst = 23;
            p.panel_width = 135;
            p.panel_height = 240;
            p.offset_x = 52;
            p.offset_y = 40;
            p.invert = true;
            p.readable = false;
            panel.config(p);

            auto l = light.config();
            l.pin_bl = 4;
            l.pwm_channel = 7;
            l.freq = 44100;
            light.config(l);
            panel.setLight(&light);

            setPanel(&panel);
        }
    };

    enum Page
    {
        PAGE_GAS,
        PAGE_WIFI,
        PAGE_MQTT,
        PAGE_INFO,
        PAGE_EDIT
    };

    TDisplay lcd;
    uint16_t *frameBuffer = nullptr; // full frame in RAM, pushed at once -> no flicker
    Button2 button1, button2;

    int page = PAGE_GAS;
    uint32_t editValue = 0;
    int editCursor = 0;
    uint32_t wifiResetRequest = 0; // time of the first click, 0 = none
    bool updateRequested = true;
    uint32_t lastUpdate = 0;
    bool prevWifi = false, prevMqtt = false, prevPortal = false;

    bool wifiResetPending()
    {
        return wifiResetRequest != 0 && millis() - wifiResetRequest < WIFI_RESET_CONFIRM_WINDOW_MS;
    }

    void push()
    {
        lcd.pushImage(0, 0, UI_WIDTH, UI_HEIGHT, reinterpret_cast<const lgfx::rgb565_t *>(frameBuffer));
    }

    void render()
    {
        lastUpdate = millis();
        updateRequested = false;
        if (!frameBuffer)
            return;

        static const Screen screens[] = {Screen::Gas, Screen::Wifi, Screen::Mqtt, Screen::Info, Screen::Edit};
        String ssid = WiFi.SSID();
        String ip = WiFi.localIP().toString();
        uint32_t pulses, offset;
        meter::snapshot(pulses, offset);

        UiState s;
        s.screen = portal::active() ? Screen::Portal : screens[page];
        s.wifiConnected = portal::wifiConnected();
        s.mqttConnected = mqtt::connected();
        s.volume = pulses + offset;
        s.flowLitersPerHour = meter::flowLitersPerHour();
        uint32_t since = meter::secondsSinceLastPulse();
        s.secondsSinceLastPulse = since == UINT32_MAX ? UI_NO_PULSE : since;
        s.ssid = ssid.c_str();
        s.ip = ip.c_str();
        s.rssi = WiFi.RSSI();
        s.wifiResetPending = wifiResetPending();
        s.mqttServer = settings.mqttServer;
        s.mqttPort = settings.mqttPort;
        s.clientId = settings.clientId;
        s.mqttStatus = mqtt::status();
        s.version = FIRMWARE_VERSION;
        s.uptimeSeconds = millis() / 1000;
        s.pulseCount = pulses;
        s.editValue = editValue;
        s.editCursor = editCursor;
        s.apName = AP_NAME;
        s.apIp = AP_IP;

        Canvas canvas(frameBuffer, UI_WIDTH, UI_HEIGHT);
        renderUi(canvas, s);
        push();
    }

    void startEdit()
    {
        editValue = meter::total();
        if (editValue > MAX_METER_VALUE)
            editValue = MAX_METER_VALUE;
        editCursor = 0;
        page = PAGE_EDIT;
    }

    void incrementDigit()
    {
        uint32_t multiplier = 1;
        for (int i = 0; i < 7 - editCursor; i++)
            multiplier *= 10;
        uint32_t digit = (editValue / multiplier) % 10;
        editValue += ((digit + 1) % 10 - digit) * multiplier; // unsigned wrap-around is intended
    }

    // Upper button: next page, or next digit in edit mode
    void onButton1(Button2 &)
    {
        if (page == PAGE_EDIT)
            editCursor = (editCursor + 1) % UI_EDIT_POSITIONS;
        else
            page = (page + 1) % UI_PAGE_COUNT;
        wifiResetRequest = 0;
        updateRequested = true;
    }

    // Lower button: action of the current page
    void onButton2(Button2 &)
    {
        switch (page)
        {
        case PAGE_WIFI:
            // A second click within the confirmation window resets Wi-Fi
            if (wifiResetPending())
                app::resetWifi();
            wifiResetRequest = millis();
            break;
        case PAGE_INFO:
            startEdit();
            break;
        case PAGE_EDIT:
            if (editCursor == UI_EDIT_SAVE)
            {
                meter::setTotal(editValue);
                page = PAGE_GAS;
                app::meterChanged();
            }
            else if (editCursor == UI_EDIT_CANCEL)
            {
                page = PAGE_INFO;
            }
            else
            {
                incrementDigit();
            }
            break;
        default: // gas and MQTT page: save and publish now
            app::save();
            mqtt::publishState();
            break;
        }
        updateRequested = true;
    }
}

namespace display
{
    void begin()
    {
        lcd.init();
        lcd.setRotation(1);
        lcd.fillScreen(TFT_BLACK);
        frameBuffer = static_cast<uint16_t *>(malloc(UI_WIDTH * UI_HEIGHT * sizeof(uint16_t)));
        if (frameBuffer)
        {
            UiState boot;
            boot.screen = Screen::Boot;
            boot.version = FIRMWARE_VERSION;
            Canvas canvas(frameBuffer, UI_WIDTH, UI_HEIGHT);
            renderUi(canvas, boot);
            push();
        }
        else
        {
            Serial.println("Display: not enough memory for the framebuffer");
        }

        button1.begin(BUTTON_1, INPUT_PULLUP, true);
        button2.begin(BUTTON_2, INPUT_PULLUP, true);
        button1.setTapHandler(onButton1);
        button2.setClickHandler(onButton2);
    }

    void loop()
    {
        button1.loop();
        button2.loop();

        bool wifi = portal::wifiConnected(), mqttConnected = mqtt::connected(), portalActive = portal::active();
        if (wifi != prevWifi || mqttConnected != prevMqtt || portalActive != prevPortal)
        {
            prevWifi = wifi;
            prevMqtt = mqttConnected;
            prevPortal = portalActive;
            updateRequested = true;
        }
        // Wi-Fi reset confirmation expired: restore the normal button hint
        if (wifiResetRequest != 0 && !wifiResetPending())
        {
            wifiResetRequest = 0;
            updateRequested = true;
        }
        if (meter::takePulseEvent())
            updateRequested = true;

        if (updateRequested || millis() - lastUpdate >= DISPLAY_REFRESH_INTERVAL_MS)
            render();
    }

    void requestUpdate() { updateRequested = true; }
}
