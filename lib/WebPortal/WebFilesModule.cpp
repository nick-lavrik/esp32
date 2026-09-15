#include "WebFilesModule.hpp"

#include <ESPAsyncWebServer.h>

#include "WebJson.hpp"
#include "WebPortal.hpp"

bool WebFilesModule::isSafePath(const String& path) {
  if (path.length() == 0 || path.length() > kMaxPath) return false;
  if (path[0] != '/') return false;
  // ".." відсікаємо цілим сегментом, а не підрядком: файл "a..b" законний.
  if (path == ".." || path.startsWith("../") || path.endsWith("/..") ||
      path.indexOf("/../") >= 0) {
    return false;
  }
  // Порожній сегмент ("//") LittleFS сприймає по-різному залежно від виклику -
  // не даємо йому такого шансу.
  if (path.indexOf("//") >= 0) return false;

  for (size_t i = 0; i < path.length(); ++i) {
    const uint8_t c = (uint8_t)path[i];
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

String WebFilesModule::_listJob(const String& path) {
  File dir = _fs.open(path, "r");
  if (!dir) return webjson::fail("No such directory");
  if (!dir.isDirectory()) {
    dir.close();
    return webjson::fail("Not a directory");
  }

  String entries;
  size_t count = 0;
  bool truncated = false;
  for (File item = dir.openNextFile(); item; item = dir.openNextFile()) {
    if (count >= kMaxEntries) {
      truncated = true;
      item.close();
      break;
    }
    if (count > 0) entries += ',';
    entries += "{\"name\":";
    entries += webjson::quote(item.name());
    entries += ",\"dir\":";
    entries += webjson::boolean(item.isDirectory());
    entries += ",\"size\":";
    entries += (uint32_t)item.size();
    entries += '}';
    ++count;
    item.close();
  }
  dir.close();

  String json = "{\"ok\":true,\"path\":";
  json += webjson::quote(path);
  json += ",\"label\":";
  json += webjson::quote(_label);
  json += ",\"truncated\":";
  json += webjson::boolean(truncated);
  // Місткість їде разом зі списком, а не окремим /api/fs/info: її дивляться
  // рівно тоді, коли дивляться каталог, а зайвий запит на це - зайва задача в
  // черзі з чотирьох слотів.
  size_t used = 0, total = 0;
  if (_usage && _usage(used, total)) {
    json += ",\"used\":";
    json += (uint32_t)used;
    json += ",\"total\":";
    json += (uint32_t)total;
  }
  json += ",\"entries\":[";
  json += entries;
  json += "]}";
  return json;
}

String WebFilesModule::_writeJob(const String& path, const String& content) {
  // Каталог під тим самим іменем перетворив би запис на мовчазну невдачу:
  // open(dir, "w") на LittleFS повертає невалідний File, і "saved" було б
  // неправдою.
  File probe = _fs.open(path, "r");
  if (probe && probe.isDirectory()) {
    probe.close();
    return webjson::fail("This is a directory");
  }
  const bool existed = (bool)probe;
  if (probe) probe.close();

  File file = _fs.open(path, "w");
  if (!file) return webjson::fail("Cannot open for writing (missing directory?)");

  const size_t written = file.write((const uint8_t*)content.c_str(), content.length());
  file.close();

  // Не віра, а перевірка: флеш буває заповнений, і тоді write() пише менше,
  // ніж просили, нічого про це не кажучи.
  if (written != content.length()) {
    _logger.error("FS write failed: %s (%u of %u bytes)", path.c_str(), (unsigned)written,
                  (unsigned)content.length());
    return webjson::fail("Write failed (filesystem full?)");
  }

  _logger.info("FS %s: %s (%u bytes)", existed ? "updated" : "created", path.c_str(),
               (unsigned)written);
  return webjson::ok(existed ? "File updated" : "File created");
}

String WebFilesModule::_mkdirJob(const String& path) {
  if (_fs.exists(path)) return webjson::fail("Path already exists");
  if (!_fs.mkdir(path)) return webjson::fail("Cannot create directory (missing parent?)");

  _logger.info("FS mkdir: %s", path.c_str());
  return webjson::ok("Directory created");
}

String WebFilesModule::_removeJob(const String& path) {
  if (path == "/") return webjson::fail("Refusing to remove the root");

  File item = _fs.open(path, "r");
  if (!item) return webjson::fail("No such file or directory");
  const bool isDir = item.isDirectory();
  item.close();

  if (isDir) {
    // rmdir знімає лише порожній каталог. Рекурсивного видалення тут немає
    // свідомо: один хибний клік по "/" знищив би і вебку, і профілі мережі.
    if (!_fs.rmdir(path)) return webjson::fail("Cannot remove (directory not empty?)");
    _logger.warn("FS rmdir: %s", path.c_str());
    return webjson::ok("Directory removed");
  }

  if (!_fs.remove(path)) return webjson::fail("Cannot remove file");
  _logger.warn("FS removed: %s", path.c_str());
  return webjson::ok("File removed");
}

String WebFilesModule::_renameJob(const String& from, const String& to) {
  if (!_fs.exists(from)) return webjson::fail("No such file or directory");
  // LittleFS::rename мовчки переписав би ціль - а тут це чужий файл, який
  // ніхто не просив чіпати.
  if (_fs.exists(to)) return webjson::fail("Destination already exists");
  if (!_fs.rename(from, to)) return webjson::fail("Rename failed (missing directory?)");

  _logger.info("FS renamed: %s -> %s", from.c_str(), to.c_str());
  return webjson::ok("Renamed");
}

void WebFilesModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  // Спільна для всіх роутів перевірка параметра-шляху. Швидка відмова 400 - у
  // роуті, щоб непридатний шлях не займав слот черги (їх усього чотири).
  auto pathParam = [](AsyncWebServerRequest* request, bool post, const char* nameOfParam,
                      String& out) {
    if (!request->hasParam(nameOfParam, post)) {
      request->send(400, "application/json",
                    webjson::error("Missing path parameter"));
      return false;
    }
    out = request->getParam(nameOfParam, post)->value();
    out.trim();
    if (!isSafePath(out)) {
      request->send(400, "application/json",
                    webjson::error("Path must be absolute and must not contain '..'"));
      return false;
    }
    return true;
  };

  auto submit = [&portal](AsyncWebServerRequest* request, WebJobQueue::Handler handler) {
    const uint32_t jobId = portal.jobs().submit(std::move(handler));
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  };

  // ---- список каталогу ----
  //
  // GET, який відповідає номером задачі: обхід каталогу - це відкриття кожного
  // запису, тобто flash I/O на десятки файлів, а в таску AsyncTCP такому не
  // місце (див. WebJobQueue.hpp).
  server.on("/api/fs/list", HTTP_GET, [this, pathParam, submit](AsyncWebServerRequest* request) {
    // Відсутній параметр тут не помилка: корінь - типовий каталог.
    String path = "/";
    if (request->hasParam("path") && !pathParam(request, false, "path", path)) return;

    submit(request, [this, path]() { return _listJob(path); });
  });

  // ---- файл цілком ----
  //
  // Єдиний роут розділу повз чергу. Причина: AsyncWebServer віддає файл
  // потоком, порціями по кілька кілобайт, і саме так уже працює вся статика
  // порталу (LittleFsStaticSource). Читання не стирає флеш, тобто не блокує на
  // десятки мілісекунд, як запис; а покласти в слот черги файл на 270 КБ не
  // вийшло б у принципі - він там лежав би рядком у heap'і.
  //
  // Він же - ВЕСЬ перегляд розділу: картинка через <img src="...">, текст і
  // бінарник через fetch() тієї ж адреси, а розпізнавання типу й xxd-дамп
  // робить сторінка. Тому тут немає ні стель показу, ні hex-конверсії: файл
  // на 276 КБ тече повз heap пристрою, а в браузері він цілий - тобто саме
  // те, чого не міг дати роут із JSON, який вертав перші кілька кілобайт.
  server.on("/api/fs/raw", HTTP_GET, [this, pathParam](AsyncWebServerRequest* request) {
    String path;
    if (!pathParam(request, false, "path", path)) return;

    File probe = _fs.open(path, "r");
    const bool ok = probe && !probe.isDirectory();
    if (probe) probe.close();
    if (!ok) {
      request->send(404, "application/json", webjson::error("No such file"));
      return;
    }

    // download=1 - зберегти файл, без нього - показати. Типове значення саме
    // "показати", бо так роут годиться для <img>; а от кнопка Download мусить
    // лишатись кнопкою: без Content-Disposition клік по index.html відкрив би
    // вебку замість вивантаження.
    const bool download = request->hasParam("download") &&
                          request->getParam("download")->value() != "0";
    if (download) {
      request->send(_fs, path, "application/octet-stream", true);
      return;
    }
    // Порожній contentType: AsyncFileResponse виводить тип із розширення сам
    // (image/jpeg, image/png, image/svg+xml...) - своєї mime-таблиці тут не
    // тримаємо, рівно як і в LittleFsStaticSource.
    request->send(_fs, path);
  });

  // ---- створення й зміна ----
  //
  // Одна дія на обидва випадки, як і в NVS: файлова система не розрізняє
  // "створити" і "переписати", і окремий роут довелося б підпирати власною
  // перевіркою наявності.
  server.on("/api/fs/file", HTTP_POST, [this, pathParam, submit](AsyncWebServerRequest* request) {
    String path;
    if (!pathParam(request, true, "path", path)) return;

    if (!request->hasParam("content", true)) {
      request->send(400, "application/json", webjson::error("Missing 'content' parameter"));
      return;
    }
    const String content = request->getParam("content", true)->value();
    if (content.length() > kMaxWrite) {
      request->send(400, "application/json",
                    webjson::error("Content is too large for the editor"));
      return;
    }

    submit(request, [this, path, content]() { return _writeJob(path, content); });
  });

  // ---- створення каталогу ----
  server.on("/api/fs/mkdir", HTTP_POST, [this, pathParam, submit](AsyncWebServerRequest* request) {
    String path;
    if (!pathParam(request, true, "path", path)) return;

    submit(request, [this, path]() { return _mkdirJob(path); });
  });

  // ---- перейменування й переміщення ----
  server.on("/api/fs/rename", HTTP_POST, [this, pathParam, submit](AsyncWebServerRequest* request) {
    String from, to;
    if (!pathParam(request, true, "from", from)) return;
    if (!pathParam(request, true, "to", to)) return;
    if (from == to) {
      request->send(400, "application/json", webjson::error("Source and destination are equal"));
      return;
    }

    submit(request, [this, from, to]() { return _renameJob(from, to); });
  });

  // ---- видалення ----
  server.on("/api/fs/remove", HTTP_DELETE, [this, pathParam, submit](AsyncWebServerRequest* request) {
    String path;
    if (!pathParam(request, false, "path", path)) return;

    submit(request, [this, path]() { return _removeJob(path); });
  });
}
