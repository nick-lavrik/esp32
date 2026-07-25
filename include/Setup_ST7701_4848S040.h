// Setup_ST7701_4848S040.h
// Конфіг LovyanGFX для плати ESP32-S3 "4848S040" (Guition/Sunton, 480x480, ST7701)
//
// bodmer/TFT_eSPI НЕ підтримує RGB/DPI-паралельний інтерфейс (DE/VSYNC/HSYNC/PCLK),
// тому тут використовується LovyanGFX із сумісним шаром LGFX_TFT_eSPI.hpp,
// який надає клас під іменем TFT_eSPI з тим самим API — щоб прикладний код
// (Display.h / Display.cpp) не відрізнявся між env.
//
// Підключається через build_flags у platformio.ini:
//   -DLGFX_USE_V1
//   -include include/Setup_ST7701_4848S040.h
//
// УВАГА: номери пінів (pin_dN, pin_pclk, pin_henable, pin_vsync, pin_hsync) —
// ТИПОВІ для плати Guition ESP32-S3-4848S040. Обов'язково звірте з офіційною
// схемою/розпіновкою саме вашої ревізії плати перед прошивкою.

#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
// lgfx::Light_PWM вже доступний через <LovyanGFX.hpp> — окремого
// заголовку esp32s3/Light_PWM.hpp не існує (підсвітка спільна для всіх ESP32).
// LGFX_TFT_eSPI.hpp підключаємо НИЖЧЕ, після визначення класу LGFX —
// бо він робить "using TFT_eSPI = LGFX;", і LGFX на той момент вже має бути відомим типом.

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7701_guition_esp32_4848S040 _panel; // готовий клас саме для цієї плати:
                                                       // сам відправляє ST7701 init-послідовність
    lgfx::Bus_RGB        _bus;
    lgfx::Light_PWM      _light;

public:
    LGFX() {
        // ---------- Шина RGB (DPI) ----------
        auto bus_cfg = _bus.config();
        bus_cfg.panel = &_panel;

        bus_cfg.pin_pclk  = 21;
        bus_cfg.pin_vsync = 17;
        bus_cfg.pin_hsync = 16;
        bus_cfg.pin_henable = 18;   // DE
        bus_cfg.freq_write = 12000000;
        bus_cfg.pclk_active_neg = false;

        bus_cfg.hsync_polarity     = 1;
        bus_cfg.hsync_front_porch  = 10;
        bus_cfg.hsync_pulse_width  = 8;
        bus_cfg.hsync_back_porch   = 50;
        bus_cfg.vsync_polarity     = 1;
        bus_cfg.vsync_front_porch  = 10;
        bus_cfg.vsync_pulse_width  = 8;
        bus_cfg.vsync_back_porch   = 20;

        // B0..B4
        bus_cfg.pin_d0  = 4;
        bus_cfg.pin_d1  = 5;
        bus_cfg.pin_d2  = 6;
        bus_cfg.pin_d3  = 7;
        bus_cfg.pin_d4  = 15;
        // G0..G5
        bus_cfg.pin_d5  = 8;
        bus_cfg.pin_d6  = 20;
        bus_cfg.pin_d7  = 3;
        bus_cfg.pin_d8  = 46;
        bus_cfg.pin_d9  = 9;
        bus_cfg.pin_d10 = 10;
        // R0..R4
        bus_cfg.pin_d11 = 11;
        bus_cfg.pin_d12 = 12;
        bus_cfg.pin_d13 = 13;
        bus_cfg.pin_d14 = 14;
        bus_cfg.pin_d15 = 0;

        _bus.config(bus_cfg);
        _panel.setBus(&_bus);

        // ---------- Параметри панелі ----------
        auto panel_cfg = _panel.config();
        panel_cfg.memory_width  = 480;
        panel_cfg.memory_height = 480;
        panel_cfg.panel_width   = 480;
        panel_cfg.panel_height  = 480;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        _panel.config(panel_cfg);

        auto detail_cfg = _panel.config_detail();
        detail_cfg.pin_cs   = 39; // 3-wire SPI (ініціалізація ST7701), типово для цієї плати
        detail_cfg.pin_sclk = 48;
        detail_cfg.pin_mosi = 47;
        detail_cfg.use_psram = 1;
        _panel.config_detail(detail_cfg);

        // ---------- Підсвітка ----------
        auto light_cfg = _light.config();
        // light_cfg.pin_bl = 6;    // GPIO підсвітки — звірте зі схемою
        light_cfg.pin_bl = 38;    // GPIO підсвітки — звірте зі схемою
        light_cfg.invert = false;
        // light_cfg.invert = true;
        // light_cfg.freq   = 44100;
        // light_cfg.freq   = 150; // 150Hz - працює, але яскравість не дуже висока
        // light_cfg.freq   = 1500; // setBrightness(min 75, max 255)
        // light_cfg.freq   = 1000; // setBrightness(min 50, max 255)
        // light_cfg.freq   = 500; // setBrightness(min 25, max 255)
        light_cfg.freq   = 300; // setBrightness(min 10, max 255)
        // light_cfg.freq   = 250; // setBrightness(min 10, max 255)
        light_cfg.pwm_channel = 0;
        _light.config(light_cfg);
        _panel.setLight(&_light);

        setPanel(&_panel);
    }
};

// Підключаємо ТІЛЬКИ після визначення class LGFX вище — цей заголовок
// робить "using TFT_eSPI = LGFX;", тож LGFX вже має бути повністю відомим типом.
#include <LGFX_TFT_eSPI.hpp>

// Якщо кольори переплутані (червоний/синій міняються місцями) або картинка
// виглядає як негатив — розкоментуйте одне з нижченаведеного після init():
//   tft.setColorDepth(16);
//   tft.invertDisplay(true);

// Сам глобальний об'єкт "tft" тепер створюється в src/TftInstance_4848S040.cpp,
// а не тут — щоб цей файл можна було включати лише туди, де він дійсно
// потрібен, а не в кожен файл проєкту через build_flags -include.