// main.cpp
//
// NodeMCU ESP8266 + 0.96" I2C OLED (SSD1306, 128x64)
// Окремий застосунок (НЕ спільний src/main.cpp): src/ жорстко зав'язаний на
// ESP32-only API (<WiFi.h>, ESP.getChipModel(), TFT_eSPI-спрайт), тому тут -
// власна реалізація з тим самим набором фіч: WiFi/NTP/Ping/MQTT/Gmail/
// EventDispatcher/TaskController.
//
// ===== CHIP INFO =====
// PlatformIO: esp8266 (nodemcuv2)
// Framework:  Arduino-ESP8266
// Core version: 3.x

#include <Arduino.h>
#include "Display.hpp"
#include "OledColors.hpp"
#include <EventDispatcher.hpp>
#include <TaskController.hpp>
#include <MqttClient.hpp>
#include <GmailSender.hpp>
#include <SerialCommander.hpp>
#include "wifi.h"
#include "ntp.h"
#include "ping.h"
#include "setup.h"

MqttConfig makeMqttConfig() {
    MqttConfig config;
    config.host = MQTT_HOST;
    config.port = MQTT_PORT;
    config.clientId = MQTT_CLIENT_ID;
    config.useAuth = true;
    config.username = MQTT_USERNAME;
    config.password = MQTT_PASSWORD;
    config.lwtTopic = MQTT_LWT_TOPIC;
    config.lwtOfflineMessage = MQTT_LWT_MSG_OFFLINE;
    config.lwtOnlineMessage = MQTT_LWT_MSG_ONLINE;
    return config;
}

Display display;
EventDispatcher dispatcher;
TaskController scheduler;
MqttClient mqtt(makeMqttConfig());
SerialCommander commandHandler;

#if HAS_GMAIL_SENDER
GmailSender mailer(GMAIL_EMAIL, GMAIL_PASSWORD, "ESP8266 NodeMCU");
#endif

void setupEventDispatcher() {
    dispatcher.addListener("wifi.connected", [](IEvent& e) {
        Serial.println("[Event] wifi.connected");
    });

    dispatcher.addListener("mqtt.message", [](IEvent& e) {
        Serial.println("[Event] mqtt.message");
    });

    Serial.println("EventDispatcher setup done");
}

void setupMqttClient() {
    mqtt.begin();

    mqtt.addStringListener("esp8266/cmd/#", [](const char* topic, const char* payload) {
        Serial.printf("[MQTT] %s = %s\n", topic, payload);
        dispatcher.dispatch("mqtt.message");
    });

    Serial.println("MqttClient setup done");
}

// Публікація базової телеметрії (uptime/heap) на MQTT кожні 10 секунд
void setupTelemetryTask() {
    scheduler.addCronTask(10UL * 1000UL, []() {
        if (!mqtt.isConnected()) return;
        mqtt.publishNumber("esp8266/uptime", millis() / 1000);
        mqtt.publishNumber("esp8266/heap", ESP.getFreeHeap());
    });
}

// Email надсилається ЛИШЕ вручну через серійну команду "mail" - ніколи автоматично.
void setupSerialCommander() {
    commandHandler.registerCommand("wifiscan", "Сканування Wi-Fi мереж", [](const String& args) {
        WiFi_scan();
    });

    commandHandler.registerCommand("mail", "Надіслати тестовий email: mail your@email.com", [](const String& args) {
        #if HAS_GMAIL_SENDER
        if (args.length() == 0) {
            Serial.println("Використання: mail recipient@example.com");
            return;
        }
        Serial.println("Надсилання email...");
        bool ok = mailer.sendEmail(args.c_str(), PIO_PIOENV, "Тестовий лист з ESP8266 NodeMCU");
        Serial.println(ok ? "Email надіслано." : "Помилка надсилання email.");
        #else
        Serial.println("GmailSender недоступний (ESP_Mail_Client.h не знайдено).");
        #endif
    });

    Serial.println("SerialCommander setup done");
}

void drawSystemInfo() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t uptimeSec = millis() / 1000;

    display.setTextSize(1);
    display.setTextColor(TFT_WHITE);

    display.setCursor(0, 0);
    display.printf("Up: %02d:%02d:%02d", uptimeSec / 3600, (uptimeSec / 60) % 60, uptimeSec % 60);

    display.setCursor(0, 8);
    display.printf("Heap: %d KB  fps:%d", freeHeap / 1024, display.loopFrameRate());

    char* pingStr = dumpPingStatsStr();
    if (pingStr) {
        display.setCursor(0, 16);
        display.print(pingStr);
    }

    display.setCursor(0, 24);
    display.printf("MQTT: %s", mqtt.isConnected() ? "connected" : "offline");
}

void setup() {
    setupSerial();
    setupDisplay();
    setupSerialCommander();
    setupEventDispatcher();

    setupWiFi();
    dispatcher.dispatch("wifi.connected");

    setupNtpService();
    setupMqttClient();
    setupTelemetryTask();

    display.flush();
    Serial.println("\n> Ready. Введіть 'list' для перегляду команд.\n");
}

void loop() {
    commandHandler.update();
    mqtt.loop();
    doPing();
    scheduler.loop();

    display.clear();
    drawSystemInfo();
    drawTime();
    display.flush();

    delay(10);
}
