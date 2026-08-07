#pragma once

#include <Arduino.h>

// Один запис зі списку клієнтів роутера (аналог одного об'єкта
// у get_clientlist після jq-фільтрації isOnline == "1").
//
// Приклад використання:
//   RouterClientInfo client;
//   client.name = "MyPhone";
//   client.ip = "192.168.28.42";
struct RouterClientInfo {
  String name;
  String ip;
  String mac;
  String type;
  String vendor;
  String timer;
};
