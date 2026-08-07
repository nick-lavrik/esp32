#pragma once

#include <Arduino.h>

#include <vector>

#include "RouterClientInfo.hpp"

// Заміна ланцюжка jq-фільтрів:
//   .get_clientlist | del(.ClientAPILevel) | del(.maclist) | ...
//   select(.online == "1")
//
// Приклад використання:
//   std::vector<RouterClientInfo> clients;
//   if (!RouterClientListParser::parse(json, clients)) {
//     Logger::error("parse failed or out of memory, got %u", clients.size());
//   }
class RouterClientListParser {
public:
  // Парсить сире тіло appGet.cgi у вектор online-клієнтів (isOnline == "1").
  // Некоректні/неочікувані ключі (ClientAPILevel, maclist, поля не-об'єкти) пропускаються
  // мовчки — так само як jq del()/select() пропускали б їх.
  //
  // Пам'ять під clients виділяється динамічно під час парсингу (без попереднього
  // резервування). Якщо вільного heap стає критично мало під час додавання
  // чергового клієнта — парсинг зупиняється на вже розпарсених елементах
  // і повертає false (без часткового/пошкодженого запису).
  //
  // Повертає true, лише якщо JSON коректний І весь список розібрано без браку пам'яті.
  static bool parse(const String& rawJson, std::vector<RouterClientInfo>& outClients);

private:
  // Мінімальний запас вільного heap (байти), нижче якого парсинг зупиняється
  // ще до спроби виділення — захист від abort() на платах без PSRAM
  // (esp32-st7789 / ttgo-t1 ~270KB heap, esp8266 ~40-45KB).
  static const size_t kMinFreeHeapBytes = 8192;

  static bool hasEnoughFreeHeap();
};
