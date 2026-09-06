#pragma once

#include <string>
#include <vector>

#include "WifiConnection.hpp"

// Формат keyfile NetworkManager (.nmconnection) <-> WifiConnection.
//
// Свідомо той самий INI, що й у /etc/NetworkManager/system-connections/ на
// Linux: файл, знятий з ноутбука, читається пристроєм без переробки, і навпаки.
// Довідник по секціях - docs/network_manager_guide.md.
//
//   [connection]
//   id=HomeWiFi
//   type=wifi
//   autoconnect=true
//   autoconnect-priority=10
//   autoconnect-retries=-1
//
//   [wifi]
//   mode=infrastructure
//   ssid=HomeWiFi
//
//   [wifi-security]
//   key-mgmt=wpa-psk
//   psk=secret
//
//   [ipv4]
//   method=manual
//   address1=192.168.1.50/24,192.168.1.1
//   gateway=192.168.1.1
//   dns=8.8.8.8;1.1.1.1;
//
//   [ipv6]
//   method=ignore
//
// Що НЕ підтримується (мовчки ігнорується при читанні, не пишеться при
// записі): uuid, interface-name, permissions, 802-1x/WPA-Enterprise, secrets
// flags, ipv6 крім method, будь-які типи крім type=wifi.
//
// Значення ssid/psk підтримують байтову форму NetworkManager
// (ssid=65;115;117;115;32; — десяткові байти через ';'). Вона потрібна там, де
// звичайний рядок не переживе зворотного читання: пробіли на краях, ';',
// не-ASCII. Запис сам обирає форму, читання розпізнає обидві.
//
// Ім'я файлу - косметичне, як і в NetworkManager: мережу визначає wifi.ssid
// усередині. Це навмисно: SSID може бути до 32 символів, а LittleFS на
// ESP8266 обмежує довжину компонента шляху.
namespace NmConnectionIni {

// Розбирає текст .nmconnection. Повертає false, якщо секції [wifi] з
// непорожнім ssid немає (тобто це не Wi-Fi профіль або файл побитий).
// connectionId у out НЕ чіпається - його призначає NetworkSupervisor.
bool parse(const std::string& text, WifiConnection& out, std::string* error = nullptr);

// Серіалізує профіль у текст .nmconnection.
std::string serialize(const WifiConnection& conn);

// Безпечне ім'я файлу для профілю: SSID з заміненими роздільниками плюс
// ".nmconnection", обрізане до maxLen символів разом з розширенням.
std::string fileNameFor(const WifiConnection& conn, size_t maxLen = 31);

// "255.255.255.0" <-> 24. Потрібні і при читанні (address1=.../24), і в
// командному шарі (ipv4.addresses).
std::string cidrToMask(int bits);
int maskToCidr(const std::string& mask);

}  // namespace NmConnectionIni
