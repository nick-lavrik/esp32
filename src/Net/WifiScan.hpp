#pragma once

#include <string>
#include <vector>

// Скан ефіру у форматі 'nmcli device wifi list'.
//
// knownSsids - SSID зі списку профілів NetworkSupervisor: вони позначаються в
// колонці IN-USE. Раніше тут стояла позначка "збіглося з WIFI_SSID", але після
// переїзду на NetworkSupervisor build-flag більше не описує стан пристрою, і
// така позначка була б просто неправдою.
//
// IN-USE: '*' - поточне з'єднання, '+' - є збережений профіль.
void wifiScan(const std::vector<std::string>& knownSsids = {});
