#pragma once

#include <Arduino.h>

#include "RouterClientListIterator.hpp"

// Фасад: login + fetch + parse + format в один виклик, для SerialCommander-команди.
// Лише аварійні повідомлення йдуть через Logger — результат повертається через buffer.
//
// Приклад реєстрації в SerialCommander (main.cpp):
//   serialCommander.registerCommand("clients", "Список активних клієнтів роутера",
//       [](const String& args) {
//         char buf[2048];
//         if (fetchRouterClientListTable(ROUTER_HOST, ROUTER_LOGIN_AUTHORIZATION, buf,
//                                         sizeof(buf))) {
//           Serial.print(buf);
//         }
//       });
//
// host, loginAuthorization — з build flags (secrets.ini), параметри функції.
// Повертає false при мережевій/парсинг/переповнення помилці (деталі — через Logger).
bool fetchRouterClientListTable(const String& host, const String& loginAuthorization, char* buffer,
                                 size_t bufferSize);

// Альтернатива fetchRouterClientListTable() — замість готової форматованої таблиці
// повертає ітератор (hasNext()/next()) над розпарсеним списком, для випадків коли
// клієнта треба обробити по одному (напр. вивід у декілька Serial.println(),
// фільтрація перед виводом на екран тощо).
//
// Приклад використання:
//   RouterClientListIterator it;
//   if (!fetchRouterClientListIterator(ROUTER_HOST, ROUTER_LOGIN_AUTHORIZATION, it)) return;
//   while (it.hasNext()) {
//     const RouterClientInfo& c = it.next();
//     Serial.printf("%s (%s)\n", c.name.c_str(), c.ip.c_str());
//   }
//
// Повертає false при мережевій/парсинг помилці (деталі — через Logger); outIterator
// у цьому разі лишається порожнім (hasNext() == false).
bool fetchRouterClientListIterator(const String& host, const String& loginAuthorization,
                                    RouterClientListIterator& outIterator);

