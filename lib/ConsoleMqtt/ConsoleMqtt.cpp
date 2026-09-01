#include "ConsoleMqtt.hpp"

#if HAS_CONSOLE_MQTT

#include <LogMirror.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
// Ключі ConfigStorage. Обмеження - 15 символів (ConfigStorage::MAX_KEY_LENGTH).
constexpr const char* kKeyActive = "console.mqtt";
constexpr const char* kKeyAllow = "console.allow";
constexpr const char* kKeyDeny = "console.deny";

// Як часто звітувати про рядки, зрізані лімітом швидкості.
constexpr uint32_t kDropReportIntervalMs = 30000;

// Ловля millis()-стрибка: якщо дзеркало довго мовчало, не даємо накопичити
// астрономічну кількість токенів (і не переповнюємо множення).
constexpr uint32_t kMaxRefillWindowMs = 10000;

constexpr size_t kErrorBufSize = 96;
}  // namespace

// Вбудовані deny-правила. Не персистяться, не видаляються, застосовуються
// ПЕРШИМИ - бо без них дзеркало годує саме себе:
//
//   \[MQTT\]              "[MQTT] queue overflow, dropped: N outgoing" і
//                         "broker denied N subscriptions" (MqttClient.cpp).
//                         Обидва - про той самий канал, яким ми публікуємо:
//                         переповнення черги породжувало б рядок, який іде в
//                         ту саму чергу.
//   mqtt\.loop\(\) took   src/main.cpp, спрацьовує щоітерації під навантаженням.
//   ^\[.\]\[console       власний тег - звіт про зрізане лімітом і dumpStatus().
const char* const ConsoleMqtt::kBuiltinDeny[] = {
  "\\[MQTT\\]",
  "mqtt\\.loop\\(\\) took",
  "^\\[.\\]\\[console",
};
const size_t ConsoleMqtt::kBuiltinDenyCount =
    sizeof(ConsoleMqtt::kBuiltinDeny) / sizeof(ConsoleMqtt::kBuiltinDeny[0]);

ConsoleMqtt::ConsoleMqtt(MqttClient& client, ConfigStorage& cfg, const char* topic)
    : _client(client), _cfg(cfg), _topic(topic != nullptr ? topic : "") {}

void ConsoleMqtt::begin() {
  static_assert(sizeof(ConsoleMqtt::kBuiltinDeny) / sizeof(ConsoleMqtt::kBuiltinDeny[0]) <=
                    sizeof(_builtin) / sizeof(_builtin[0]),
                "ConsoleMqtt::_builtin is smaller than kBuiltinDeny");

  char err[kErrorBufSize];
  for (size_t i = 0; i < kBuiltinDenyCount; ++i) {
    if (!_builtin[i].compile(kBuiltinDeny[i], err, sizeof(err))) {
      // Не фатально, але означає, що зникла головна перепона зациклюванню.
      _logger.error("built-in rule '%s' failed: %s", kBuiltinDeny[i], err);
    }
  }

  _active = _cfg.getBool(kKeyActive, CONSOLE_MQTT_ACTIVE != 0);
  loadRules(/*deny=*/false, kKeyAllow);
  loadRules(/*deny=*/true, kKeyDeny);

  _lastRefillMs = millis();
  _lastDropReportMs = _lastRefillMs;

  // Фільтр ехо ставимо на СВІЙ клієнт, а не покладаємось на викликача: при
  // root-підписці "#" брокер повертає нам наші ж публікації, і кожен рядок
  // логу з'їдав би слот у вхідній черзі (32, drop-oldest) - власний шум
  // здатний витіснити справжню вхідну команду.
  _client.setEchoIgnoreTopic(_topic.c_str());

  LogMirror::set(this);

  _logger.info("mirror %s -> %s", _active ? "on" : "off", resolvedTopic().c_str());
}

std::string ConsoleMqtt::resolvedTopic() const { return _client.keyGenerator().key(_topic.c_str()); }

void ConsoleMqtt::setActive(bool on, bool persist) {
  _active = on;
  if (persist) {
    _cfg.setBool(kKeyActive, on);
  }
  _logger.info("mirror %s -> %s", on ? "on" : "off", resolvedTopic().c_str());
}

void ConsoleMqtt::loadRules(bool deny, const char* key) {
  std::vector<String> patterns;
  _cfg.getStringArray(key, patterns);

  LogRule* list = deny ? _deny : _allow;
  char err[kErrorBufSize];
  size_t slot = 0;

  for (const String& pattern : patterns) {
    if (slot >= kMaxRules) {
      _logger.warn("stored %s list has more than %u rules - the rest ignored", deny ? "deny" : "allow",
                   (unsigned)kMaxRules);
      break;
    }
    if (!list[slot].compile(pattern.c_str(), err, sizeof(err))) {
      // Патерн у NVS міг зберегтись при іншій версії прошивки - не мовчимо,
      // інакше фільтр просто "не працює" без пояснень.
      _logger.error("stored %s rule '%s' rejected: %s", deny ? "deny" : "allow", pattern.c_str(), err);
      continue;
    }
    ++slot;
  }
}

void ConsoleMqtt::persistRules(bool deny) {
  const LogRule* list = deny ? _deny : _allow;
  std::vector<String> patterns;
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (list[i].valid()) {
      patterns.push_back(String(list[i].pattern()));
    }
  }
  _cfg.setStringArray(deny ? kKeyDeny : kKeyAllow, patterns);
}

bool ConsoleMqtt::addRule(bool deny, const char* pattern, char* errBuf, size_t errBufSize) {
  LogRule* list = deny ? _deny : _allow;

  for (size_t i = 0; i < kMaxRules; ++i) {
    if (list[i].valid()) {
      continue;
    }
    if (!list[i].compile(pattern, errBuf, errBufSize)) {
      return false;
    }
    persistRules(deny);
    return true;
  }

  if (errBuf != nullptr && errBufSize > 0) {
    snprintf(errBuf, errBufSize, "%s list is full (%u rules)", deny ? "deny" : "allow", (unsigned)kMaxRules);
  }
  return false;
}

void ConsoleMqtt::clearRules(bool deny) {
  LogRule* list = deny ? _deny : _allow;
  for (size_t i = 0; i < kMaxRules; ++i) {
    list[i].reset();
  }
  _cfg.setStringArray(deny ? kKeyDeny : kKeyAllow, std::vector<String>{});
}

// Порядок правил: вбудований deny -> whitelist -> deny. Вбудований іде першим і
// перекрити його не можна навмисно: це єдиний захист від того, щоб дзеркало
// публікувало рядки, породжені власною ж публікацією.
bool ConsoleMqtt::passesFilters(const char* line) const {
  for (size_t i = 0; i < kBuiltinDenyCount; ++i) {
    if (_builtin[i].matches(line)) {
      return false;
    }
  }

  // Порожній whitelist = пропускати все. Непорожній - рядок мусить зійтися
  // хоч з одним правилом.
  bool hasAllow = false;
  bool allowed = false;
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (!_allow[i].valid()) {
      continue;
    }
    hasAllow = true;
    if (_allow[i].matches(line)) {
      allowed = true;
      break;
    }
  }
  if (hasAllow && !allowed) {
    return false;
  }

  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_deny[i].valid() && _deny[i].matches(line)) {
      return false;
    }
  }

  return true;
}

bool ConsoleMqtt::wouldPass(const char* line) const { return passesFilters(line); }

bool ConsoleMqtt::takeToken(uint32_t now) {
  uint32_t elapsed = now - _lastRefillMs;  // коректно й через переповнення millis()
  if (elapsed > kMaxRefillWindowMs) {
    elapsed = kMaxRefillWindowMs;
  }
  _lastRefillMs = now;

  _tokensMilli += elapsed * kRatePerSec;
  if (_tokensMilli > kBurst * 1000) {
    _tokensMilli = kBurst * 1000;
  }

  if (_tokensMilli < 1000) {
    return false;
  }
  _tokensMilli -= 1000;
  return true;
}

size_t ConsoleMqtt::write(const uint8_t* buffer, size_t size) {
  if (!_active || buffer == nullptr || size == 0) {
    return size;
  }

  // Guard ставимо ДО всього іншого: далі йдуть regexec, publish і власний лог -
  // будь-що з цього може народити новий рядок, який прийшов би сюди ж.
  if (_busy.test_and_set(std::memory_order_acquire)) {
    return size;
  }

  const uint32_t now = millis();

  do {
    // Нічого не накопичуємо: поки з'єднання немає, рядок для дзеркала просто
    // не існує. Саме це й означає "без буферизації" - після конекту пачка
    // старих рядків не приїде.
    if (!_client.isConnected() || _client.isSuspended()) {
      break;
    }

    char line[PrintQueue::kLineSize];
    size_t length = (size < sizeof(line) - 1) ? size : sizeof(line) - 1;
    memcpy(line, buffer, length);
    line[length] = '\0';

    // Хвостовий '\n' зрізаємо: тут один рядок = одне повідомлення, і підписник
    // додає перенос сам - інакше в mosquitto_sub між рядками порожні рядки.
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
      line[--length] = '\0';
    }
    if (length == 0) {
      break;
    }

    if (!passesFilters(line)) {
      break;
    }

    if (!takeToken(now)) {
      ++_droppedRate;
      break;
    }

    _client.publish(_topic.c_str(), line);
    ++_published;
  } while (false);

  // Звіт про зрізане лімітом. Іде під тим самим guard-ом, тому сам у дзеркало
  // не потрапить (та й вбудоване правило "^\[.\]\[console" його зрізало б).
  if (_droppedRate > 0 && (now - _lastDropReportMs) >= kDropReportIntervalMs) {
    _logger.warn("rate limit dropped %u lines in the last %u s (%u lines/s)", (unsigned)_droppedRate,
                 (unsigned)(kDropReportIntervalMs / 1000), (unsigned)kRatePerSec);
    _droppedRate = 0;
    _lastDropReportMs = now;
  }

  _busy.clear(std::memory_order_release);
  return size;
}

void ConsoleMqtt::dumpStatus() const {
  _logger.info("mirror = %s, topic = '%s'", _active ? "on" : "off", resolvedTopic().c_str());
  _logger.info("published = %u, dropped by rate limit = %u (limit %u lines/s, burst %u)",
               (unsigned)_published, (unsigned)_droppedRate, (unsigned)kRatePerSec, (unsigned)kBurst);

  for (size_t i = 0; i < kBuiltinDenyCount; ++i) {
    _logger.info("  deny  (built-in) %s", _builtin[i].valid() ? _builtin[i].pattern() : kBuiltinDeny[i]);
  }

  size_t userRules = 0;
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_allow[i].valid()) {
      _logger.info("  allow %s", _allow[i].pattern());
      ++userRules;
    }
  }
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_deny[i].valid()) {
      _logger.info("  deny  %s", _deny[i].pattern());
      ++userRules;
    }
  }
  if (userRules == 0) {
    _logger.info("  (no user rules - everything except the built-in deny goes out)");
  }
}

#endif  // HAS_CONSOLE_MQTT
