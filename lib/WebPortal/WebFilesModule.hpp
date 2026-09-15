#pragma once

// Розділ "files": перегляд і редагування вмісту LittleFS прямо з браузера.
//
// Навіщо. У флеші пристрою лежить те, без чого він не працює як треба:
// сторінка порталу (/www), профілі мережі (/network/*.nmconnection), фонові
// зображення. Досі єдиним способом їх змінити був 'pio run -t uploadfs' - а це
// кабель, стирання розділу цілком і перезавантаження. Тут - той самий вміст,
// але поштучно.
//
// Джерело файлів - fs::FS&, а не жорстко LittleFS: на платах із карткою
// (BOARD_HAS_SD) той самий модуль піднімається другим екземпляром з іншим
// посиланням. Параметра "?fs=..." свідомо немає: два джерела - це два розділи
// з власними роутами, а не гілка всередині кожного обробника.
//
// Шляхи. Приймається лише абсолютний шлях без ".." (див. isSafePath) -
// перевірка стоїть у роуті, тобто ДО черги, і застосовується на пристрої, а не
// в UI: роут відкритий будь-кому, хто вже в мережі пристрою.
//
// Що можна і чого не можна. Створити й переписати можна лише ТЕКСТОВИЙ файл,
// який уміщається в kMaxWrite: значення їде звичайним полем форми, через слот
// WebJobQueue, тобто цілком лежить у heap'і. Вивантаження великого файла
// (фонові JPEG - по 270 КБ) вимагає писати чанки прямо в таску сервера, і це
// окрема робота - див. docs/web_portal_roadmap.md, розділ про upload.
//
// Прочитати ж можна будь-що і ЦІЛКОМ - але не звідси: перегляд робить браузер
// із /api/fs/raw. Розпізнати текст від бінарника, розкласти xxd-дамп і
// намалювати картинку він уміє сам, а потік із flash його не коштує пристрою
// нічого понад той самий File, яким уже роздається статика. Роут, що вертав
// початок файла в JSON (hex, дві стелі, буфер у слоті черги), був другим
// механізмом для тієї ж задачі - і прибраний.
//
// Виконання. Запис у flash - тільки через WebJobQueue, тобто в loop(). Через
// чергу йде і читання (список каталогу - це десятки відкриттів файлів), з тієї
// ж причини, що в WebNvsModule. Виняток один - /api/fs/raw: він віддає файл
// потоком тим самим механізмом, яким AsyncWebServer уже роздає статику з
// LittleFS (див. LittleFsStaticSource), тож окремої поведінки тут не
// винаходимо.
//
// Обережно: /www редагується цим самим CRUD, тобто власну вебку можна знести
// звідси ж. Страхує вшита в прошивку копія сторінки (ProgmemStaticSource) -
// портал переживе порожній /www і дозволить залити її назад.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

#include <TLogger.hpp>

#include <functional>

#include "IWebModule.hpp"

// Скільки байтів приймаємо в тілі запису. Не стеля файлової системи, а стеля
// одного слота WebJobQueue: значення лежить у heap'і цілком - і в String
// запиту, і в замиканні задачі. 4 КБ покривають реальні випадки цього розділу
// (профіль .nmconnection, невеликий JSON, конфіг), а сторінка порталу
// заливається 'uploadfs'.
#ifndef WEB_FS_MAX_WRITE
#define WEB_FS_MAX_WRITE 4096
#endif

// Скільки записів віддавати в одному списку каталогу.
#ifndef WEB_FS_MAX_ENTRIES
#define WEB_FS_MAX_ENTRIES 128
#endif

// Межа довжини шляху. LittleFS в ESP-IDF обмежує ім'я об'єкта 64 байтами
// (CONFIG_LITTLEFS_OBJ_NAME_LEN), повний шлях - довший, але не безмежно.
#ifndef WEB_FS_MAX_PATH
#define WEB_FS_MAX_PATH 127
#endif

class WebFilesModule : public IWebModule {
public:
  static constexpr size_t kMaxWrite = WEB_FS_MAX_WRITE;
  static constexpr size_t kMaxEntries = WEB_FS_MAX_ENTRIES;
  static constexpr size_t kMaxPath = WEB_FS_MAX_PATH;

  // Зайнято / всього, у байтах. Окремим замиканням, бо fs::FS цього не вміє:
  // usedBytes()/totalBytes() є в LittleFSFS і SDFS, але не в спільній базі, а
  // тримати тут гілку під кожну реалізацію - рівно те дублювання, якого
  // уникає посилання на fs::FS. Порожнє замикання = не показувати місткість.
  using UsageFn = std::function<bool(size_t& used, size_t& total)>;

  // label - як джерело називається в UI ("LittleFS", "SD"); ім'я розділу для
  // /api/status лишається "files".
  explicit WebFilesModule(fs::FS& fs, const char* label = "LittleFS", UsageFn usage = nullptr)
      : _fs(fs), _label(label), _usage(std::move(usage)) {}

  const char* name() const override { return "files"; }
  void registerRoutes(AsyncWebServer& server, WebPortal& portal) override;

  // Абсолютний шлях без ".." і без керівних символів. Статична й публічна, бо
  // це єдине місце, де описано, який шлях вважається прийнятним, - роути
  // звіряються з ним ДО того, як задача потрапить у чергу.
  static bool isSafePath(const String& path);

private:
  // Усі виконуються з WebJobQueue, тобто в контексті loop().
  String _listJob(const String& path);
  String _writeJob(const String& path, const String& content);
  String _mkdirJob(const String& path);
  String _removeJob(const String& path);
  String _renameJob(const String& from, const String& to);

  fs::FS& _fs;
  const char* _label;
  UsageFn _usage;

  const TLogger _logger{"web"};
};
