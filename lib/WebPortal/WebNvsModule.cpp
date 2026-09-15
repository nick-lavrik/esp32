#include "WebNvsModule.hpp"

#include <ConfigStorage.hpp>
#include <ESPAsyncWebServer.h>

#include <vector>

#include "WebJson.hpp"
#include "WebPortal.hpp"

namespace {

// Назви типів, які приймає форма. Свідомо коротший перелік, ніж у NVS:
// створювати з вебки можна лише те, що ConfigStorage вміє записати одним
// викликом і потім прочитати назад тим самим типом.
constexpr const char* kTypeString = "string";
constexpr const char* kTypeInt = "i32";
constexpr const char* kTypeBool = "bool";

// Тип NVS, у якому опиниться значення після запису. Потрібен, щоб помітити
// зміну типу наявного ключа: nvs_set_* на ключі іншого типу повертає
// ESP_ERR_NVS_TYPE_MISMATCH, а Preferences цю помилку лише логує - запис тихо
// не відбувся б.
nvs_type_t targetType(const String& type) {
  if (type == kTypeString) return NVS_TYPE_STR;
  if (type == kTypeInt) return NVS_TYPE_I32;
  if (type == kTypeBool) return NVS_TYPE_U8;
  return NVS_TYPE_ANY;
}

bool parseBool(const String& value) {
  return value == "1" || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("on");
}

}  // namespace

bool WebNvsModule::_isOwn(const String& ns) const {
  return ns.length() == 0 || ns == _storage.namespaceName();
}

String WebNvsModule::_listJob(const String& ns) {
  const bool own = _isOwn(ns);
  const char* target = own ? nullptr : ns.c_str();

  auto entries = _storage.listEntries(target);
  const bool truncated = entries.size() > kMaxEntries;
  if (truncated) entries.resize(kMaxEntries);

  String json = "{\"ok\":true,\"namespace\":";
  json += webjson::quote(own ? String(_storage.namespaceName()) : ns);
  // Писати можна лише у свій namespace - сторінка бере це звідси, а не
  // вирішує сама: правило одне й живе на пристрої, який його й застосовує.
  json += ",\"writable\":";
  json += webjson::boolean(own);
  json += ",\"truncated\":";
  json += webjson::boolean(truncated);

  json += ",\"namespaces\":[";
  auto names = _storage.listNamespaces();
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) json += ',';
    json += webjson::quote(names[i]);
  }
  json += "]";

  json += ",\"entries\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    const ConfigStorage::Entry& e = entries[i];
    if (i > 0) json += ',';
    json += "{\"key\":";
    json += webjson::quote(e.key);
    json += ",\"type\":";
    json += webjson::quote(e.typeName);
    json += ",\"value\":";
    json += webjson::quote(_storage.getAsString(e.key.c_str(), e.type, target));
    // Редагованість вирішує пристрій, а не сторінка: він єдиний знає, що
    // вміє записати назад. Інакше UI довелося б тримати власну копію цього
    // правила й синхронізувати її руками.
    json += ",\"editable\":";
    json += webjson::boolean(own && e.type != NVS_TYPE_BLOB);
    json += "}";
  }
  json += "]}";
  return json;
}

String WebNvsModule::_blobJob(const String& key, const String& ns) {
  const char* target = _isOwn(ns) ? nullptr : ns.c_str();

  const size_t stored = _storage.blobLength(key.c_str(), target);
  if (stored == 0) return webjson::fail("No such key, or it is not a blob");

  // Блоб читається ЦІЛИМ - часткового читання в NVS немає (див. kMaxBlobRead).
  if (stored > kMaxBlobRead) {
    return webjson::fail("Blob is too large to read");
  }
  std::vector<uint8_t> bytes(stored);
  const size_t read = _storage.readBlob(key.c_str(), bytes.data(), stored, target);
  if (read == 0) return webjson::fail("Cannot read blob");

  // А от показуємо не більше kMaxBlob: обрізаємо вже прочитане.
  const size_t got = read > kMaxBlob ? kMaxBlob : read;

  // Розкладку (адреси, колонки, ASCII-панель) малює сторінка: тут іде лише
  // рядок hex. Формувати готовий xxd на пристрої означало б слати втричі
  // більше байтів заради тексту, який браузер складе сам.
  static const char kHex[] = "0123456789abcdef";
  String hex;
  hex.reserve(got * 2);
  for (size_t i = 0; i < got; ++i) {
    hex += kHex[bytes[i] >> 4];
    hex += kHex[bytes[i] & 0x0f];
  }

  String json = "{\"ok\":true,\"key\":";
  json += webjson::quote(key);
  json += ",\"size\":";
  json += (uint32_t)stored;
  json += ",\"truncated\":";
  json += webjson::boolean(got < read);
  json += ",\"hex\":";
  json += webjson::quote(hex);
  json += "}";
  return json;
}

String WebNvsModule::_saveJob(const String& key, const String& type, const String& value) {
  const nvs_type_t wanted = targetType(type);
  if (wanted == NVS_TYPE_ANY) return webjson::fail("Unsupported value type");

  const nvs_type_t existing = _storage.getType(key.c_str());
  if (existing != NVS_TYPE_ANY && existing != wanted) {
    // Змінити тип наявного ключа можна лише через видалення: NVS звіряє тип
    // при записі. Робимо це явно, бо інакше запис просто не відбувся б, а
    // сторінка показала б "saved".
    _storage.remove(key.c_str());
  }

  if (type == kTypeString) {
    _storage.setString(key.c_str(), value);
  } else if (type == kTypeInt) {
    _storage.setInt(key.c_str(), (int32_t)strtol(value.c_str(), nullptr, 10));
  } else {
    _storage.setBool(key.c_str(), parseBool(value));
  }

  // Перевірка, а не віра: NVS-розділ буває заповнений, і тоді set* мовчить.
  if (_storage.getType(key.c_str()) != wanted) {
    _logger.error("NVS write failed: %s", key.c_str());
    return webjson::fail("Write failed (NVS partition full?)");
  }

  _logger.info("NVS %s: %s = %s", existing == NVS_TYPE_ANY ? "created" : "updated", key.c_str(),
               value.c_str());
  return webjson::ok(existing == NVS_TYPE_ANY ? "Key created" : "Key updated");
}

String WebNvsModule::_deleteJob(const String& key) {
  if (!_storage.remove(key.c_str())) return webjson::fail("No such key");

  _logger.info("NVS removed: %s", key.c_str());
  return webjson::ok("Key removed");
}

void WebNvsModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  // ---- перелік записів ----
  //
  // GET, який відповідає номером задачі, - не примха: listEntries() крутить
  // ітератор по NVS, а getAsString() читає кожне значення, тобто це flash I/O
  // на десятки ключів. У таску AsyncTCP таке не місце (див. WebJobQueue.hpp).
  server.on("/api/nvs/list", HTTP_GET, [this, &portal](AsyncWebServerRequest* request) {
    const String ns = request->hasParam("ns") ? request->getParam("ns")->value() : String();
    if (ns.length() > 0 && !ConfigStorage::isKeyValid(ns.c_str())) {
      request->send(400, "application/json", webjson::error("Bad namespace name"));
      return;
    }

    const uint32_t jobId = portal.jobs().submit([this, ns]() { return _listJob(ns); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- сирий вміст блоба ----
  //
  // Окремий роут, а не поле в /api/nvs/list: блоби в списку бувають по кілька
  // сотень байтів кожен, і віддавати їх усі заради того, що дивляться по
  // одному, - зайва робота і для flash, і для heap.
  server.on("/api/nvs/blob", HTTP_GET, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("key")) {
      request->send(400, "application/json", webjson::error("Missing 'key' parameter"));
      return;
    }

    const String key = request->getParam("key")->value();
    const String ns = request->hasParam("ns") ? request->getParam("ns")->value() : String();
    if (!ConfigStorage::isKeyValid(key.c_str())) {
      request->send(400, "application/json",
                    webjson::error("Key must be 1 to 15 characters long"));
      return;
    }

    const uint32_t jobId = portal.jobs().submit([this, key, ns]() { return _blobJob(key, ns); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- створення й зміна ----
  //
  // Одна дія на обидва випадки: NVS не розрізняє insert і update, і окремий
  // роут "тільки створити" довелося б підпирати власною перевіркою наявності
  // - зайвий стан там, де сховище й так поводиться як put.
  server.on("/api/nvs/entry", HTTP_POST, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("key", true) || !request->hasParam("type", true) ||
        !request->hasParam("value", true)) {
      request->send(400, "application/json",
                    webjson::error("Missing 'key', 'type' or 'value' parameter"));
      return;
    }

    String key = request->getParam("key", true)->value();
    const String type = request->getParam("type", true)->value();
    const String value = request->getParam("value", true)->value();
    key.trim();

    // Чужий namespace - тільки на читання. Перевірка тут, а не лише в UI:
    // роут відкритий будь-кому, хто вже в мережі пристрою.
    if (request->hasParam("ns", true) && !_isOwn(request->getParam("ns", true)->value())) {
      request->send(403, "application/json", webjson::error("This namespace is read-only"));
      return;
    }

    if (!ConfigStorage::isKeyValid(key.c_str())) {
      request->send(400, "application/json",
                    webjson::error("Key must be 1 to 15 characters long"));
      return;
    }
    if (value.length() > kMaxValue) {
      request->send(400, "application/json", webjson::error("Value is too long"));
      return;
    }
    if (targetType(type) == NVS_TYPE_ANY) {
      request->send(400, "application/json", webjson::error("Unsupported value type"));
      return;
    }

    const uint32_t jobId =
        portal.jobs().submit([this, key, type, value]() { return _saveJob(key, type, value); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });

  // ---- видалення ----
  server.on("/api/nvs/entry", HTTP_DELETE, [this, &portal](AsyncWebServerRequest* request) {
    if (!request->hasParam("key")) {
      request->send(400, "application/json", webjson::error("Missing 'key' parameter"));
      return;
    }

    const String key = request->getParam("key")->value();
    if (!ConfigStorage::isKeyValid(key.c_str())) {
      request->send(400, "application/json",
                    webjson::error("Key must be 1 to 15 characters long"));
      return;
    }
    if (request->hasParam("ns") && !_isOwn(request->getParam("ns")->value())) {
      request->send(403, "application/json", webjson::error("This namespace is read-only"));
      return;
    }

    const uint32_t jobId = portal.jobs().submit([this, key]() { return _deleteJob(key); });
    if (jobId == 0) {
      request->send(503, "application/json", webjson::error("Job queue is full, try again"));
      return;
    }
    request->send(202, "application/json", String("{\"jobId\":") + jobId + "}");
  });
}
