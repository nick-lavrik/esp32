#pragma once

#include <WifiConnection.hpp>

// Прошитий перелік WiFi-мереж плати - "заводський" запис, а не робоча
// конфігурація: netSupervisor.seedConnections() засіває ним порожній список при першому
// старті (див. setupNetworkSupervisor() у main.cpp), далі профілі живуть у
// NVS і редагуються командою 'net'.
//
// Чому таблиця, а не пара WIFI_SSID/WIFI_PASSWORD з secrets.ini: мереж у
// реальності більше однієї (роутер, гостьова, хотспот з телефона), і вони
// відрізняються пріоритетом. Опис однієї макросом, а решти вручну через 'net'
// означав би, що після чистої прошивки плата знає половину того, що знає
// розробник.
//
// Чому в src/, а не поруч із seedConnections() у lib/NetworkSupervisor: тут
// креденшели конкретного пристрою, бібліотека лишається переносною (той самий
// поділ, що в src/Ecoflow/EcoflowDeviceRegistry).
//
// УВАГА: "Asus " - з ПРОБІЛОМ на кінці, це справжнє ім'я мережі, а не одрук.
// Пробіл виживає в ефірі й у роутері, але тихо гине скрізь, де рядок проходить
// через trim: саме через нього в netcli::splitArgs() є режим лапок ('net
// connection add ssid "Asus "'), інакше профіль з консолі не ввести. Прибрати
// пробіл тут - зламати підключення, а не "причесати таблицю".
//
// Паролі беруться ВИКЛЮЧНО з макросів: secrets.ini у .gitignore, а цей файл -
// ні. SSID тут відкриті свідомо: вони й так в ефірі.

#ifndef WIFI5_PASSWORD
#define WIFI5_PASSWORD ""
#endif

inline constexpr WifiNetworkInfo kWifiNetworks[] = {
    {.ssid = WIFI_SSID_SEED, .password = WIFI_PASSWORD_SEED, .priority = 0},
    {.ssid = "Asus ", .password = WIFI5_PASSWORD, .priority = 100},
    {.ssid = "5G.nick.lavrik", .password = WIFI5_PASSWORD, .priority = 100},
    // Друга мережа - рядок тут плюс ключ у secrets.ini і -D у platformio.ini:
    // {"Asus_5G", WIFI_PASSWORD_5G, 90},
    // {"Pixel", WIFI_PASSWORD_HOTSPOT, 50},
};
