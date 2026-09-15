#pragma once

// Розділ "commands": перелік зареєстрованих команд, їх запуск і shortcuts.
//
// Чому окремо від "console". Консоль - це СТРІЧКА: вона показує журнал і має
// поле вводу, як термінал. Тут же інша задача - знайти команду серед кількох
// десятків, побачити її опис і запустити з аргументами, не памʼятаючи
// синтаксису. Це різні екрани, і зшивати їх в один не варто.
//
// Зате механізм запуску спільний, і він живе САМЕ ТУТ: POST /api/commands/exec
// - єдиний веб-вхід для "виконати рядок". Консоль (вкладка Console) шле свій
// ввід туди ж; власного /api/console/exec більше немає. Дві копії одного
// маршруту розійшлись би на першій же зміні (валідація, ліміт довжини,
// відлуння в лог) - рівно той випадок, про який попереджає CLAUDE.md.
//
// Виконання. Команда НЕ виконується в таску сервера: вона кладеться в
// CommandQueue, той самий вхід, що для serial, MQTT і cron, а виконує її
// loop(). 'sdbench' блокує на десятки секунд, і в таску AsyncTCP йому не
// місце. Черга скінченна, тому відмова явна - 503, а не тиха втрата.
// Окремої "відповіді команди" немає: вивід зʼявиться в стрічці логу, яку
// читає вкладка Console.
//
// Shortcuts - вільний список "назва + рядок команди", який редагує
// користувач. Лежать у NVS одним ключем 'web_shortcuts'
// (ConfigStorage::setStringArray), елемент - "<назва>\t<команда>": табуляція
// замість JSON, щоб не тягнути парсер на пристрій заради двох полів. Запис у
// flash - через WebJobQueue, як і будь-яка інша мутація.
//
// Читання списку shortcuts іде з ГОТОВОГО знімка JSON, а не з NVS: сам вектор
// чіпає лише loop(), а таск сервера бачить рядок під мьютексом. Та сама
// схема, що в WebWifiModule, і з тієї ж причини - вектор може бути
// перевиділений під час читання.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <functional>
#include <vector>

#include <SerialCommander.hpp>
#include <TLogger.hpp>

#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#include "IWebModule.hpp"

// Скільки shortcuts тримати. Стеля не з вимог UI, а з NVS: усе лежить одним
// блобом, який читається й пишеться цілком.
#ifndef WEB_COMMANDS_MAX_SHORTCUTS
#define WEB_COMMANDS_MAX_SHORTCUTS 16
#endif

// Межа рядка команди. Та сама, що в CommandQueue::kLineSize і
// SerialCommander::maxLineLength_ - довший рядок черга однаково відкине,
// краще сказати про це одразу й зрозуміло.
#ifndef WEB_COMMANDS_MAX_LINE
#define WEB_COMMANDS_MAX_LINE 128
#endif

class ConfigStorage;

class WebCommandsModule : public IWebModule {
public:
  static constexpr size_t kMaxShortcuts = WEB_COMMANDS_MAX_SHORTCUTS;
  static constexpr size_t kMaxLine = WEB_COMMANDS_MAX_LINE;
  static constexpr size_t kMaxShortcutName = 32;

  // Ключ NVS (обмеження - 15 символів).
  static constexpr const char* kCfgShortcuts = "web_shortcuts";

  // submit - CommandQueue::submit(); повертає false, якщо черга повна.
  using Submit = std::function<bool(const char* line)>;

  WebCommandsModule(SerialCommander& commander, Submit submit, ConfigStorage& storage);
  ~WebCommandsModule() override;

  const char* name() const override { return "commands"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

private:
  struct Shortcut {
    String name;
    String command;
  };

  // Усі три - лише з loop()/черги задач: читають NVS або чіпають вектор.
  void _loadShortcuts();
  String _saveJob(const String& name, const String& command, int index);
  String _deleteJob(size_t index);

  // Перезбирає _shortcutsJson під мьютексом. Викликати після кожної зміни
  // вектора.
  void _rebuildJson();

  class Lock {
  public:
#if defined(ESP32)
    explicit Lock(SemaphoreHandle_t m) : _m(m) {
      if (_m) xSemaphoreTake(_m, portMAX_DELAY);
    }
    ~Lock() {
      if (_m) xSemaphoreGive(_m);
    }

  private:
    SemaphoreHandle_t _m;
#else
    explicit Lock(int) {}
#endif
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };

  SerialCommander& _commander;
  Submit _submit;
  ConfigStorage& _storage;

  std::vector<Shortcut> _shortcuts;  // тільки loop()
  String _shortcutsJson = "[]";      // знімок для таска сервера

#if defined(ESP32)
  SemaphoreHandle_t _mutex = nullptr;
#else
  int _mutex = 0;
#endif

  const TLogger _logger{"web"};
};
