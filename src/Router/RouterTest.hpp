#pragma once

// Перевірка клієнта роутера ASUS (lib/AsusWRT):
//   dump-asuswrt  - логін на роутер (ROUTER_HOST) + get_clientlist, список клієнтів;
//   dump-asuswrt2 - той самий розбір, але JSON з /asus-get_clientlist.json (LittleFS),
//                   коли роутер недоступний: чи парсер ще розуміє формат.

#include <SerialCommander.hpp>

void registerRouterCommands(SerialCommander& commander);
