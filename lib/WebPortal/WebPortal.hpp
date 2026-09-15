#pragma once

// Веб-портал пристрою: точка збірки HttpServer, статики, автентифікації,
// черги задач і розділів (IWebModule).
//
// Головна властивість: портал НЕ залежить від наявності підключення до WiFi.
// AsyncWebServer слухає на всіх інтерфейсах lwIP, тому його достатньо підняти
// один раз при старті - і він однаково відповідає і в домашній мережі, і на
// точці доступу, яку NetworkSupervisor підіймає сам, коли жодної збереженої
// мережі не видно (AP-fallback, apSsid = "ESP-<env>"). Гасити й піднімати
// сервер на кожну зміну стану мережі не потрібно й шкідливо: саме в момент
// падіння мережі портал і потрібен.
//
// Статика йде ланцюжком CompositeStaticSource:
//   LittleFS "/www" (100)  ->  вшита в прошивку копія (ProgmemStaticSource, -100)
//
// Звичайне джерело - саме вшита копія: сторінка лежить у assets/www/, тобто
// ПОЗА образом LittleFS, і оновлюється разом із прошивкою, без 'uploadfs'.
// LittleFS стоїть перед нею не тому, що там сторінка, а щоб її можна було
// ПЕРЕКРИТИ кастомною - файлом, покладеним у /www хоч через саму вкладку
// Files. Видалення цього файла повертає вшиту сторінку, тому знести вебку з
// /www не страшно: пристрій лишається настроюваним.
//
// Автентифікація - HTTP Basic, логін і пароль у ConfigStorage (ключі
// "web_user" / "web_pass"). Поки пароль не заданий, портал відкритий: інакше
// перша ж прошивка залишила б пристрій без доступу до власного налаштування.
//
// Використання (main.cpp):
//   WebPortal portal(httpServer, configStorage);
//   portal.addModule(&wifiModule);
//   portal.addModule(&consoleModule);
//   portal.begin();
//   ...
//   void loop() { portal.loop(); }

#include <Arduino.h>
#include <CompositeStaticSource.hpp>
#include <HttpServer.hpp>
#include <LittleFsStaticSource.hpp>
#include <ProgmemStaticSource.hpp>
#include <TLogger.hpp>

#include <vector>

#include "IWebModule.hpp"
#include "WebJobQueue.hpp"

class ConfigStorage;

// Каталог статики у LittleFS. Корінь зайнятий іншим вмістом проєкту
// (фонові JPEG, /network/*.nmconnection), тому вебка живе окремо.
#ifndef WEB_PORTAL_FS_ROOT
#define WEB_PORTAL_FS_ROOT "/www"
#endif

class WebPortal {
public:
  // Ключі ConfigStorage (обмеження NVS - 15 символів).
  static constexpr const char* kCfgUser = "web_user";
  static constexpr const char* kCfgPassword = "web_pass";

  WebPortal(HttpServer& httpServer, ConfigStorage& storage);

  WebPortal(const WebPortal&) = delete;
  WebPortal& operator=(const WebPortal&) = delete;

  // Додає розділ. Викликати ДО begin() - саме там реєструються роути.
  void addModule(IWebModule* module);

  // Додаткове джерело статики (напр. SdStaticSource на платах з карткою).
  // priority: більше = перевіряється раніше; LittleFS має 100, вшита
  // сторінка -100.
  void addStaticSource(IStaticSource* source, int priority = 0);

  // Читає креденшели з NVS, збирає статику, реєструє роути модулів
  // і піднімає HttpServer.
  bool begin();

  // Дренаж черги задач + loop() кожного розділу. Викликати з loop().
  void loop();

  WebJobQueue& jobs() { return _jobs; }

  // Зберігає нові креденшели в NVS. Порожній пароль - вимкнути авторизацію.
  // Застосовується лише після рестарту сервера (middleware додається в
  // HttpServer::begin()), про що й повідомляє відповідь роуту.
  void setCredentials(const String& user, const String& password);

  bool isRunning() const { return _httpServer.isRunning(); }

private:
  // /api/status - спільний для всіх розділів мінімум: чи живий портал,
  // скільки задач у черзі, які розділи зареєстровані.
  void _registerCoreRoutes();

  HttpServer& _httpServer;
  ConfigStorage& _storage;

  CompositeStaticSource _staticSources;
  LittleFsStaticSource _fsSource;
  ProgmemStaticSource _builtinSource;

  WebJobQueue _jobs;
  std::vector<IWebModule*> _modules;

  const TLogger _logger{"web"};
};
