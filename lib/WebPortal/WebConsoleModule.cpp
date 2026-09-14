#include "WebConsoleModule.hpp"

#include <ESPAsyncWebServer.h>
#include <Journal.hpp>

#include "WebJson.hpp"
#include "WebPortal.hpp"

// Точка, з якої почати мовити новому клієнтові. Last-Event-ID браузер надсилає
// сам після обриву; якщо його немає (перше відкриття вкладки) - хвіст кільця,
// щоб сторінка одразу показала останні події.
//
// ЖОДНОГО ВИКЛИКУ МЕТОДІВ _events ЗВІДСИ. AsyncEventSource::_addClient() бере
// std::mutex списку клієнтів і кличе цей колбек, НЕ відпустивши його
// (AsyncEventSource.cpp:344). Мьютекс не рекурсивний, тому будь-який count(),
// avgPacketsWaiting() чи _events.send() тут вішає таск AsyncTCP на самого себе:
// пристрій пінгується, але порт 80 не приймає нових з'єднань, поки відкритий
// бодай один SSE-клієнт. Перевірено відніманням - голий AsyncEventSource без
// цього колбека не блокує нічого.
//
// client->send() безпечний (у клієнта свій замок), але надсилати все одно
// краще з loop(): у таску AsyncTCP нічого зайвого робити не варто.
void WebConsoleModule::_seekForClient(AsyncEventSourceClient* client) {
  Journal& journal = Journal::instance();
  const uint32_t head = journal.head();
  const uint32_t tail = journal.tail();

  uint32_t from = client->lastId() > 0 ? client->lastId() + 1 : 0;

  // Порядок затискань має значення, і на цьому я вже попався: спершу стеля.
  // Last-Event-ID цілком може вказувати в МАЙБУТНЄ - браузер тримає його між
  // перезавантаженнями сторінки, а наш seq після ребута пристрою починається з
  // нуля. Без цієї перевірки "head - from" на uint32_t давав переповнення, і
  // клієнт отримував резюм із випадкового місця кільця. Такий id означає "не
  // знаю, де я" - віддаємо хвіст, як першому підключенню.
  if (from > head) from = 0;
  if (from < tail) from = tail;
  if (head - from > kMaxLinesPerRequest) from = head - kMaxLinesPerRequest;

  // Курсор спільний на всіх - персонального стану ми не тримаємо. Відмотуємо
  // лише НАЗАД: новий клієнт бачить усе своє, а решта може побачити кілька
  // рядків удруге. Дублі прийнятніші за дірки, і сторінка їх переживає -
  // клієнтський курсор просто пересинхронізується.
  if (from < _cursor) _cursor = from;
}

void WebConsoleModule::loop() {
  // Перша перевірка - час, і лише потім усе інше: count() бере мьютекс списку
  // клієнтів, а нас тут викликають тисячі разів на секунду.
  const uint32_t now = millis();
  if (now - _lastSseMs < kSseIntervalMs) return;
  _lastSseMs = now;

  Journal& journal = Journal::instance();

  if (_events.count() == 0) {
    _cursor = journal.head();  // нікому мовити - не накопичуємо борг
    return;
  }
  // Клієнт не встигає розгрібати: не додаємо йому роботи, хай черга спорожніє.
  if (_events.avgPacketsWaiting() >= kSseMaxQueued) return;

  const uint32_t head = journal.head();
  const uint32_t tail = journal.tail();
  if (_cursor < tail) _cursor = tail;  // відстали - розрив видно з стрибка id

  char line[JournalEntry::kTextSize + 16];
  JournalEntry entry;
  for (size_t sent = 0; _cursor < head && sent < kSseFramesPerLoop; ++_cursor) {
    if (!journal.copyEntry(_cursor, entry)) continue;
    if (journalFormatLine(entry, line, sizeof(line)) == 0) continue;
    _events.send(line, nullptr, _cursor);
    ++sent;
  }
}

void WebConsoleModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  (void)portal;  // консоль більше не користується WebJobQueue - команди йдуть у CommandQueue
  // ---- стрічка логу через SSE ----
  _events.onConnect([this](AsyncEventSourceClient* client) { _seekForClient(client); });
  server.addHandler(&_events);

  // ---- стрічка логу ----
  server.on("/api/console/log", HTTP_GET, [this](AsyncWebServerRequest* request) {
    Journal& journal = Journal::instance();
    const uint32_t head = journal.head();
    const uint32_t tail = journal.tail();

    // Без since - віддаємо хвіст, а не весь буфер: так вкладка, яку щойно
    // відкрили, одразу бачить останні події, не витягуючи всю історію.
    uint32_t since = tail;
    if (request->hasParam("since")) {
      since = strtoul(request->getParam("since")->value().c_str(), nullptr, 10);
    } else if (head > kMaxLinesPerRequest) {
      since = head - kMaxLinesPerRequest;
    }

    // Клієнт відстав більше, ніж уміщує кільце: підтягуємо його до
    // найстарішого наявного запису. Розрив видно з того, що "from" >
    // надісланого "since".
    if (since < tail) since = tail;
    if (since > head) since = head;

    uint32_t last = since + kMaxLinesPerRequest;
    if (last > head) last = head;

    String json = "{\"from\":";
    json += since;
    json += ",\"next\":";
    json += last;
    json += ",\"head\":";
    json += head;
    json += ",\"lines\":[";

    char line[JournalEntry::kTextSize + 16];
    JournalEntry entry;
    bool first = true;
    for (uint32_t seq = since; seq < last; ++seq) {
      // Запис міг зникнути просто зараз - помпа й продюсери працюють паралельно
      // з цим обробником. Пропускаємо: клієнт побачить розрив по "from"/"next".
      if (!journal.copyEntry(seq, entry)) continue;
      if (journalFormatLine(entry, line, sizeof(line)) == 0) continue;
      if (!first) json += ',';
      json += webjson::quote(line);
      first = false;
    }
    json += "]}";

    request->send(200, "application/json", json);
  });

  // ---- список зареєстрованих команд ----
  server.on("/api/console/commands", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String json = "[";
    for (size_t i = 0; i < _commander.commandCount(); ++i) {
      if (i > 0) json += ',';
      json += "{\"name\":";
      json += webjson::quote(_commander.commandName(i));
      json += ",\"description\":";
      json += webjson::quote(_commander.commandDescription(i));
      json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // ---- запуск команди ----
  server.on("/api/console/exec", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!request->hasParam("cmd", true)) {
      request->send(400, "application/json", webjson::error("Missing 'cmd' parameter"));
      return;
    }

    const String cmd = request->getParam("cmd", true)->value();
    if (cmd.length() == 0) {
      request->send(400, "application/json", webjson::error("Empty command"));
      return;
    }

    // Відлуння введеного рядка - щоб у консолі було видно, ЩО саме виконали,
    // як у терміналі. Іде звичайним логом: власного буфера в модуля більше
    // немає, тож рядок бачать усі приймачі, і serial теж. Це навіть корисно: у
    // моніторі видно, що команду запустили з браузера.
    //
    // Логуємо ДО submit(): при повній черзі в стрічці має лишитись слід, що
    // команду вводили, а не тиша.
    _logger.info("> %s", cmd.c_str());

    if (!_submit || !_submit(cmd.c_str())) {
      request->send(503, "application/json", webjson::error("Command queue is full, try again"));
      return;
    }

    request->send(202, "application/json", String("{\"queued\":") + webjson::quote(cmd) + "}");
  });
}
