#include "JsonApi.hpp"

#if HAS_MQTT_CLIENT && !ESP8266

#include <Arduino.h>
#include <ArduinoJson.h>

#include <JsonApiEntry.hpp>
#include <MqttReplyTarget.hpp>
#include <WebJson.hpp>
#include <cstring>
#include <memory>
#include <vector>

#include "App/AppGlobals.hpp"

#if HAS_ECOFLOW_CLIENT
#include "Ecoflow/EcoflowJournalView.hpp"
#endif

// Анонімний namespace: структури аргументів (FsListArgs, NvsArgs, ...) мали б
// інакше зовнішнє зв'язування, і однакове ім'я в іншому TU дало б тиху
// ODR-колізію.
namespace {

// Реєстр імен зареєстрованих JSON API команд - джерело для discovery.commands
// (src/Mqtt/Discovery.cpp, поза HAS_WEB_PORTAL). Накопичувач лишається видимим НЕЗАЛЕЖНО від
// HAS_WEB_PORTAL, хоча заповнює його лише registerJsonApiEntry() нижче: сам
// publishDiscovery() під HAS_WEB_PORTAL не стоїть (той самий принцип, що й
// для board/revision - коментар нижче), тож масив має бути визначений тут, а
// не всередині гейтованого блоку. Гейт !ESP8266 - той самий, що й навколо
// publishDiscovery(): на esp8266 HAS_WEB_PORTAL=0 і discovery взагалі не
// компілюється, тож ні писати, ні читати цей масив нема кому - без цього
// гейта registerJsonApiCommandName() лишався б "defined but not used" саме на
// esp8266 (перевірено збіркою).
static constexpr size_t kMaxJsonApiCommands =
    12;  // 11 наявних (system-info/
         // wifi-status/wifi-connections/
         // ecoflow-status/ecoflow-journal/
         // mqtt-status/commands-list/fs-list/fs-read/nvs-list/nvs-blob) + запас
static const char* kJsonApiCommandNames[kMaxJsonApiCommands] = {};
static size_t kJsonApiCommandCount = 0;

static void registerJsonApiCommandName(const char* name) {
  if (kJsonApiCommandCount >= kMaxJsonApiCommands) return;
  kJsonApiCommandNames[kJsonApiCommandCount++] = name;
}

// MQTT SAPI-канал, фаза 1 (docs/mqtt-web-handoff.md): спільна інфраструктура
// диспетчеризації JSON-команд. jsonApiNoArgs/handleJsonApiRequest/
// registerJsonApiEntry самі не читають жодних portal-об'єктів (лише
// JsonApiEntry/CommandQueue/MqttClient), тому лишаються тут, поза
// HAS_WEB_PORTAL - на відміну від команд, побудованих ПОВЕРХ них
// (system-info/wifi-status/ecoflow-status/mqtt-status, блок нижче), чиї
// провайдери (webPortal/webWifiModule/webEcoflowModule/webMqttModule) самі
// оголошені лише під HAS_WEB_PORTAL (src/App/AppGlobals.cpp).
//
// Команди без аргументів (система/wifi/mqtt-status тощо) використовують цей
// резолвер - "ecoflow-journal" приймає свій, з реальним "target" у тілі
// запиту (jsonApiEcoflowJournalResolve() нижче), "ecoflow-params/<sn>" - свій
// topic-per-device шлях, поза цим резолвером узагалі (drill-down нижче).
static bool jsonApiNoArgs(JsonVariantConst /*args*/, uint8_t* /*rawOut*/, size_t /*rawCapacity*/) { return true; }

// Спільна диспетчеризація запиту на будь-яку команду з реєстру нижче - три
// майже ідентичні addJsonListener()-колбеки (system-info/wifi-status/
// ecoflow-status) були б тим самим дублюванням, якого уникає CLAUDE.md
// (DRY): третій користувач того самого коду - уже не "один", а привід
// узагальнити (KISS, той самий принцип, що й "другий користувач").
static void handleJsonApiRequest(const JsonApiEntry& entry, const String& replyTopic, JsonDocument& doc) {
  const uint32_t id = doc["id"] | 0;
  uint8_t args[CommandQueue::kLineSize];
  if (!entry.resolve(doc["args"], args, sizeof(args))) {
    mqtt.publish(replyTopic.c_str(), (String("{\"id\":") + id + ",\"ok\":false,\"error\":\"bad args\"}").c_str());
    return;
  }
  auto reply = std::make_shared<MqttReplyTarget>(mqtt, std::string(replyTopic.c_str()));
  if (!commandQueue.submitJson(&entry, id, args, sizeof(args), reply)) {
    // Явна відмова, а не тиша - той самий контракт, що й для command/.
    mqtt.publish(replyTopic.c_str(), (String("{\"id\":") + id + ",\"ok\":false,\"error\":\"busy\"}").c_str());
  }
}

// Топік - devices/<client-id>/api/<cmd> (не <client-id> в одному топіку з
// cmd у payload): той самий листовий сегмент, що й у devices/<client-id>/
// status, /ecoflow/.../grid, /light-sensor - зовнішній моніторинг розрізняє
// призначення без парсингу payload (docs/mqtt-web-handoff.md, «Погоджені
// рішення фази 1»).
static void registerJsonApiEntry(const JsonApiEntry& entry) {
  const String reqTopic = String("devices/") + MQTT_CLIENT_ID + "/api/" + entry.name;
  const String replyTopic = reqTopic + "/reply";
  mqtt.addJsonListener(reqTopic.c_str(), [&entry, replyTopic](const char* topic, JsonDocument& doc) {
    (void)topic;
    handleJsonApiRequest(entry, replyTopic, doc);
  });
  registerJsonApiCommandName(entry.name);
}

// Дзеркало /api/commands/list (WebCommandsModule): перелік зареєстрованих
// serial-команд для сторінки Commands SAPI - той самий "список ліворуч", що
// на порталі, але порталу браузер тут не бачить, лише MQTT. commandHandler -
// спільний глобал (AppGlobals.hpp), завжди визначений незалежно від HAS_WEB_PORTAL,
// тому ця команда - єдина з п'яти, що лишається доступною без порталу: решта
// чотири (система нижче) читають webPortal/webWifiModule/webEcoflowModule/
// webMqttModule, а ці provider-об'єкти самі оголошені лише під HAS_WEB_PORTAL
// (src/App/AppGlobals.cpp) - винести їх звідти без переносу самих класів не можна
// (докладніше - docs/tech_debt.md).
static String jsonApiCommandsListExecute(const uint8_t* /*raw*/) {
  String json = "[";
  for (size_t i = 0; i < commandHandler.commandCount(); ++i) {
    if (i > 0) json += ',';
    json += "{\"name\":";
    json += webjson::quote(commandHandler.commandName(i).c_str());
    json += ",\"description\":";
    json += webjson::quote(commandHandler.commandDescription(i).c_str());
    json += "}";
  }
  json += "]";
  return json;
}

static const JsonApiEntry kJsonApiCommandsList = {"commands-list", jsonApiNoArgs, jsonApiCommandsListExecute};

#if HAS_WEB_PORTAL
// MQTT SAPI-канал, фаза 1 (docs/mqtt-web-handoff.md): команди, чиї дані йдуть
// через provider-об'єкти порталу (webPortal/webWifiModule/webEcoflowModule/
// webMqttModule, оголошені під HAS_WEB_PORTAL, src/App/AppGlobals.cpp) - на платі без
// порталу ці об'єкти не існують, тож команди нижче структурно не
// компілюються (jsonApiNoArgs/handleJsonApiRequest/registerJsonApiEntry/
// commands-list - вище, поза цим гейтом).

// Пре-альфа: LittleFS і SD - через webSystemModule.littleFsJson()/sdJson().
//
// "portal" - той самий об'єкт, що й /api/status (WebPortal::statusJson(),
// docs/mqtt-web-handoff.md, розділ "SAPI ... UI-сесія"): env/revision/
// uptime/auth/pendingJobs/modules - властивості ПРИСТРОЮ (не HTTP-каналу),
// тому й на MQTT-каналі мають бути справжніми, а не '***' на SAPI-боці.
// Команда взагалі не компілюється без HAS_WEB_PORTAL (гейт цього блоку
// вище) - тобто немає плати, де system-info існує, а порталу нема: окремого
// "портал вимкнено" стану тут не буває структурно.
static String jsonApiSystemInfoExecute(const uint8_t* /*raw*/) {
  String out = "{\"portal\":";
  out += webPortal.statusJson();
  out += ",\"chip\":";
  out += WebSystemModule::chipInfoJson();
  out += ",\"heap\":";
  out += WebSystemModule::heapStatsJson();
  out += ",\"flash\":";
  out += WebSystemModule::flashStatsJson();
  out += ",\"littlefs\":";
  out += webSystemModule.littleFsJson();
  out += ",\"sd\":";
  out += webSystemModule.sdJson();
  out += ",\"nvs\":";
  out += WebSystemModule::nvsStatsJson();
  out += ",\"partitions\":";
  out += WebSystemModule::partitionsJson();
  out += "}";
  return out;
}

static const JsonApiEntry kJsonApiSystemInfo = {"system-info", jsonApiNoArgs, jsonApiSystemInfoExecute};

// Дзеркало /api/wifi/status - той самий провайдерський знімок, що й портал
// (webWifiModule, src/App/AppGlobals.cpp), той самий форматер: жодного
// поля, вартого прибирати заради MQTT-бюджету (розділ «Провайдер ≠
// форматер», docs/mqtt-web-handoff.md - другий (MQTT-специфічний) форматер
// писати нема сенсу без різниці у вмісті).
static String jsonApiWifiStatusExecute(const uint8_t* /*raw*/) {
  return WebWifiModule::portalStatusJson(webWifiModule.statusSnapshot());
}

static const JsonApiEntry kJsonApiWifiStatus = {"wifi-status", jsonApiNoArgs, jsonApiWifiStatusExecute};

// Дзеркало /api/wifi/connections - збережені профілі (SAPI Wi-Fi, "Saved
// profiles", запит користувача цієї сесії: сторінка 1:1 з порталом). Той
// самий форматер, той самий знімок статусу (для поля "active" - до якого
// профілю підключені ЗАРАЗ), окрема команда, а не поле в wifi-status: список
// профілів міняється рідко (лише мутацією, якої SAPI поки не робить), тому
// не має сенсу ганяти його в кожній відповіді wifi-status.
static String jsonApiWifiConnectionsExecute(const uint8_t* /*raw*/) {
  return WebWifiModule::portalConnectionsJson(webWifiModule.connectionsSnapshot(), webWifiModule.statusSnapshot());
}

static const JsonApiEntry kJsonApiWifiConnections = {"wifi-connections", jsonApiNoArgs, jsonApiWifiConnectionsExecute};

// Дзеркало /api/fs/list (WebFilesModule::listJson(), той самий форматер):
// вміст одного каталогу LittleFS + місткість. Каталог - аргумент "path"
// (за замовчуванням "/"), перевірений тим самим isSafePath(), що й роут
// порталу, ще в resolve() - небезпечний шлях відсікається як "bad args" і в
// чергу не потрапляє. Лише читання: SAPI не пише у файлову систему.
struct FsListArgs {
  char path[WebFilesModule::kMaxPath + 1] = "/";
};
static_assert(sizeof(FsListArgs) <= CommandQueue::kLineSize, "FsListArgs has to fit into Slot.payload");

static bool jsonApiFsListResolve(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity) {
  if (rawCapacity < sizeof(FsListArgs)) return false;
  const String path = args["path"] | "/";
  if (!WebFilesModule::isSafePath(path)) return false;
  FsListArgs a;
  strncpy(a.path, path.c_str(), sizeof(a.path) - 1);
  memcpy(rawOut, &a, sizeof(a));
  return true;
}

static String jsonApiFsListExecute(const uint8_t* raw) {
  FsListArgs args;
  memcpy(&args, raw, sizeof(args));
  return webFilesModule.listJson(String(args.path));
}

static const JsonApiEntry kJsonApiFsList = {"fs-list", jsonApiFsListResolve, jsonApiFsListExecute};

// Вміст файла шматками (WebFilesModule::readJson(), base64) - для Preview
// вкладки Files SAPI. Аргументи: path + offset; клієнт добирає файл
// послідовними запитами, поки "eof". Лише читання.
struct FsReadArgs {
  uint32_t offset = 0;
  // Решта слота: kMaxPath+1 разом з offset не влізло б у kLineSize, тож шлях
  // тут трохи коротший за портальний (довший - "bad args").
  char path[CommandQueue::kLineSize - sizeof(uint32_t)] = "";
};
static_assert(sizeof(FsReadArgs) <= CommandQueue::kLineSize, "FsReadArgs has to fit into Slot.payload");

static bool jsonApiFsReadResolve(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity) {
  if (rawCapacity < sizeof(FsReadArgs)) return false;
  const String path = args["path"] | "";
  FsReadArgs a;
  if (!WebFilesModule::isSafePath(path) || path.length() >= sizeof(a.path)) return false;
  strncpy(a.path, path.c_str(), sizeof(a.path) - 1);
  a.offset = args["offset"] | 0u;
  memcpy(rawOut, &a, sizeof(a));
  return true;
}

static String jsonApiFsReadExecute(const uint8_t* raw) {
  FsReadArgs args;
  memcpy(&args, raw, sizeof(args));
  return webFilesModule.readJson(String(args.path), args.offset);
}

static const JsonApiEntry kJsonApiFsRead = {"fs-read", jsonApiFsReadResolve, jsonApiFsReadExecute};

// Дзеркало /api/nvs/list і /api/nvs/blob (WebNvsModule::listJson()/
// blobJson(), ті самі форматери): записи одного namespace і hex блоба. Лише
// читання. Імена namespace/ключа - ConfigStorage::isKeyValid() (<=15
// символів), перевірене в resolve(); порожнє ns = власний namespace.
struct NvsArgs {
  char ns[16] = "";
  char key[16] = "";
};
static_assert(sizeof(NvsArgs) <= CommandQueue::kLineSize, "NvsArgs has to fit into Slot.payload");

// needKey=false для nvs-list (ключа нема). Окрема від resolve-функцій, щоб не
// дублювати копіювання двох коротких рядків.
static bool nvsResolveArgs(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity, bool needKey) {
  if (rawCapacity < sizeof(NvsArgs)) return false;
  const String ns = args["ns"] | "";
  const String key = args["key"] | "";
  if (ns.length() > 0 && !ConfigStorage::isKeyValid(ns.c_str())) return false;
  if (needKey && !ConfigStorage::isKeyValid(key.c_str())) return false;
  NvsArgs a;
  strncpy(a.ns, ns.c_str(), sizeof(a.ns) - 1);
  strncpy(a.key, key.c_str(), sizeof(a.key) - 1);
  memcpy(rawOut, &a, sizeof(a));
  return true;
}

static bool jsonApiNvsListResolve(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity) {
  return nvsResolveArgs(args, rawOut, rawCapacity, false);
}
static bool jsonApiNvsBlobResolve(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity) {
  return nvsResolveArgs(args, rawOut, rawCapacity, true);
}

static String jsonApiNvsListExecute(const uint8_t* raw) {
  NvsArgs args;
  memcpy(&args, raw, sizeof(args));
  return webNvsModule.listJson(String(args.ns));
}
static String jsonApiNvsBlobExecute(const uint8_t* raw) {
  NvsArgs args;
  memcpy(&args, raw, sizeof(args));
  return webNvsModule.blobJson(String(args.key), String(args.ns));
}

static const JsonApiEntry kJsonApiNvsList = {"nvs-list", jsonApiNvsListResolve, jsonApiNvsListExecute};
static const JsonApiEntry kJsonApiNvsBlob = {"nvs-blob", jsonApiNvsBlobResolve, jsonApiNvsBlobExecute};

#if HAS_ECOFLOW_CLIENT
// Дзеркало /api/ecoflow/status - тут форматер уже інший
// (WebEcoflowModule::mqttStatusJson(), без сирого "params" на кожен
// пристрій, розділ «Провайдер ≠ форматер»).
static String jsonApiEcoflowStatusExecute(const uint8_t* /*raw*/) {
  return WebEcoflowModule::mqttStatusJson(webEcoflowModule.statusSnapshot(), webEcoflowModule.devicesSnapshot());
}

static const JsonApiEntry kJsonApiEcoflowStatus = {"ecoflow-status", jsonApiNoArgs, jsonApiEcoflowStatusExecute};

// Окремий запит (НЕ розширення ecoflow-status) - дзеркало serial-команди
// 'ecoflow-journal show [sn|index|all]' (docs/ecoflow.md, «Журнал переходів
// grid»): злитий хронологічний потік Transition-переходів grid. Перша
// команда фази 1, що реально приймає JSON "args" у тілі запиту (досі лише
// шаблон, docs/mqtt-web-handoff.md) - topic-per-device (як
// ecoflow-params/<sn> нижче) тут невиправданий: запит рідкісний, а не
// частий per-device polling, статична підписка на кожен пристрій дала б
// лише зайві топіки.
struct EcoflowJournalArgs {
  char target[24] = "";  // "all" або serialNumber (<=16 символів)
};
static_assert(sizeof(EcoflowJournalArgs) <= CommandQueue::kLineSize, "EcoflowJournalArgs has to fit into Slot.payload");

static bool jsonApiEcoflowJournalResolve(JsonVariantConst args, uint8_t* rawOut, size_t rawCapacity) {
  if (rawCapacity < sizeof(EcoflowJournalArgs)) return false;
  EcoflowJournalArgs a;
  const char* target = args["target"] | "all";
  strncpy(a.target, target, sizeof(a.target) - 1);
  memcpy(rawOut, &a, sizeof(a));
  return true;
}

static String jsonApiEcoflowJournalExecute(const uint8_t* raw) {
  EcoflowJournalArgs args;
  memcpy(&args, raw, sizeof(args));
  std::vector<EcoflowJournalRow> rows;
  String error;
  if (!ecoflowBuildJournalRows(ecoflowDevices, String(args.target), rows, &error)) {
    return "{\"error\":" + webjson::quote(error) + "}";
  }
  return WebEcoflowModule::journalJson(String(args.target), rows);
}

static const JsonApiEntry kJsonApiEcoflowJournal = {"ecoflow-journal", jsonApiEcoflowJournalResolve,
                                                    jsonApiEcoflowJournalExecute};

// Drill-down: "params" ОДНОГО пристрою за серійним номером у самому топіку -
// devices/<client-id>/api/ecoflow-params/<sn> (docs/mqtt-topics.md). Окремий
// шлях від registerJsonApiEntry() вище: той підписує ОДИН топік на ІМ'Я
// команди (кінцевий сегмент - стала на етапі компіляції назва), а тут
// кінцевий сегмент - серійний номер ПРИСТРОЮ (їх кілька,
// EcoflowDeviceRegistry::deviceTable()) - реєстрація нижче йде по одній
// точній підписці на пристрій, без жодного wildcard на стороні плати (той
// самий принцип, що вже застосований до registerJsonApiEntry()). Через
// CommandQueue::submitJson() - той самий "один виконавець за ітерацію", що
// й решта JSON-команд (Точка E, docs/mqtt-web-handoff.md), а не прямий
// виклик із колбека підписки.
struct EcoflowDeviceParamsArgs {
  // Вказівник, не копія символів: рядок - літерал з deviceTable() (статичне
  // сховище, живе всю роботу програми), тому memcpy самого вказівника в
  // Slot.payload безпечний.
  const char* serialNumber;
};

static String jsonApiEcoflowDeviceParamsExecute(const uint8_t* raw) {
  EcoflowDeviceParamsArgs args;
  memcpy(&args, raw, sizeof(args));
  for (const auto& snap : webEcoflowModule.devicesSnapshot()) {
    if (strcmp(snap.state.info->serialNumber, args.serialNumber) == 0) {
      return WebEcoflowModule::mqttDeviceParamsJson(snap);
    }
  }
  // Недосяжно за нормальної роботи: серійник завжди береться з того самого
  // deviceTable(), яким наповнюється й devicesSnapshot() (конструктор
  // EcoflowDeviceRegistry). Явна відмова, а не порожній об'єкт - якщо
  // колись розійдеться.
  return "{\"error\":\"unknown device\"}";
}

static const JsonApiEntry kJsonApiEcoflowDeviceParams = {"ecoflow-params/<sn>", jsonApiNoArgs,
                                                         jsonApiEcoflowDeviceParamsExecute};

// Один топік НА ПРИСТРІЙ, зареєстрований один раз при setup() - deviceTable()
// відомий заздалегідь (той самий аргумент, що й для хардкоду серійників у
// EcoflowDeviceRegistry: ACL EcoFlow/rpi5 не приймає wildcard, отже і тут
// зручніше не покладатись на підписку "+"). sn - лямбда-захоплення (не
// std::function у самому JsonApiEntry - там і далі голі С-функції, лише
// підписка на MQTT-топік лишається лямбдою, той самий патерн, що
// registerJsonApiEntry() вище).
static void registerEcoflowDeviceParamsEntries() {
  const EcoflowDeviceInfo* table = EcoflowDeviceRegistry::deviceTable();
  for (size_t i = 0; i < EcoflowDeviceRegistry::deviceCount(); ++i) {
    const char* sn = table[i].serialNumber;
    const String reqTopic = String("devices/") + MQTT_CLIENT_ID + "/api/ecoflow-params/" + sn;
    const String replyTopic = reqTopic + "/reply";
    mqtt.addJsonListener(reqTopic.c_str(), [sn, replyTopic](const char* topic, JsonDocument& doc) {
      (void)topic;
      const uint32_t id = doc["id"] | 0;
      const EcoflowDeviceParamsArgs args{sn};
      auto reply = std::make_shared<MqttReplyTarget>(mqtt, std::string(replyTopic.c_str()));
      if (!commandQueue.submitJson(&kJsonApiEcoflowDeviceParams, id, reinterpret_cast<const uint8_t*>(&args),
                                   sizeof(args), reply)) {
        mqtt.publish(replyTopic.c_str(), (String("{\"id\":") + id + ",\"ok\":false,\"error\":\"busy\"}").c_str());
      }
    });
  }
}
#endif

// Дзеркало /api/mqtt/status - webMqttModule читає лише свій MqttClient/
// ConsoleMqtt/CommandQueue напряму (жоден з них не живе у власному таску з
// мьютексом-кешем, на відміну від NetworkSupervisor/EcoflowClient), тому тут
// нема окремого provider/formatter-розділення - той самий метод, що й /api/
// mqtt/status (розділ «Провайдер ≠ форматер», docs/mqtt-web-handoff.md).
static String jsonApiMqttStatusExecute(const uint8_t* /*raw*/) { return webMqttModule.statusJson(); }

static const JsonApiEntry kJsonApiMqttStatus = {"mqtt-status", jsonApiNoArgs, jsonApiMqttStatusExecute};
#endif

}  // namespace

void registerJsonApiCommands() {
  // commands-list - не залежить від HAS_WEB_PORTAL (commandHandler завжди
  // доступний), тому реєструється поза гейтом нижче.
  registerJsonApiEntry(kJsonApiCommandsList);

#if HAS_WEB_PORTAL
  // MQTT SAPI-канал, фаза 1 (docs/mqtt-web-handoff.md): devices/<client-id>/
  // api/<cmd> - той самий листовий сегмент, що й у devices/<client-id>/status,
  // /ecoflow/.../grid, /light-sensor - зовнішній моніторинг розрізняє
  // призначення без парсингу payload. На відміну від commands-list вище, ці
  // команди читають provider-об'єкти порталу (webPortal/webWifiModule/
  // webEcoflowModule/webMqttModule), тому лишаються під цим гейтом.
  registerJsonApiEntry(kJsonApiSystemInfo);
  registerJsonApiEntry(kJsonApiWifiStatus);
  registerJsonApiEntry(kJsonApiWifiConnections);
  registerJsonApiEntry(kJsonApiFsList);
  registerJsonApiEntry(kJsonApiFsRead);
  registerJsonApiEntry(kJsonApiNvsList);
  registerJsonApiEntry(kJsonApiNvsBlob);
#if HAS_ECOFLOW_CLIENT
  registerJsonApiEntry(kJsonApiEcoflowStatus);
  registerJsonApiEntry(kJsonApiEcoflowJournal);
  registerEcoflowDeviceParamsEntries();
#endif
  registerJsonApiEntry(kJsonApiMqttStatus);
#endif
}

size_t jsonApiCommandCount() { return kJsonApiCommandCount; }

const char* jsonApiCommandName(size_t index) {
  return index < kJsonApiCommandCount ? kJsonApiCommandNames[index] : nullptr;
}

#endif
