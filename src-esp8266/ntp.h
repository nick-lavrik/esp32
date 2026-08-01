// ntp.h
#pragma once

#include <time.h>
#include "Display.hpp"
#include "OledColors.hpp"

const char* ntpServer1 = "1.pool.ntp.org";
const char* ntpServer2 = "ua.pool.ntp.org";
const char* ntpServer3 = "pool.ntp.org";
const long  gmtOffset_sec = 2 * 3600;
const int   daylightOffset_sec = 3600;

void setupNtpService() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
}

extern Display display;

void drawTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        display.setTextSize(1);
        display.setTextColor(TFT_RED);
        display.setCursor(0, 0);
        display.print("Time sync failed");
        Serial.println("Time sync failed!");
        return;
    }

    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

    display.setTextSize(1);
    display.setTextColor(TFT_LIGHTGREY);
    display.setCursor(max(0, display.width() - display.textWidth(timeStr)), 0);
    display.print(timeStr);
}
