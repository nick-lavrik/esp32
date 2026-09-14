#include "ConsoleMqtt.hpp"

#if HAS_CONSOLE_MQTT

#include <JournalTag.hpp>

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

ConsoleMqtt::ConsoleMqtt(MqttClient& client, ConfigStorage& cfg, const char* topic)
    : _client(client), _cfg(cfg), _topic(topic != nullptr ? topic : "") {}

ConsoleMqtt::~ConsoleMqtt() { Journal::instance().unsubscribe(_sub); }

void ConsoleMqtt::begin() {
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

  // Патерн "" - беремо все, що пройшло фільтр рівня; звуження робить allow-лист
  // (він може мати кілька тегів, а патерн підписки - лише один).
  //
  // lossless НЕ ставимо навмисно: мережа може стояти хвилинами, і дзеркало,
  // яке пригальмовує продюсера, зупинило б увесь пристрій.
  _sub = Journal::instance().subscribe(
      "mqtt-mirror", "", LogLevel::Verbose,
      [this](const JournalEntry& entry) { return deliver(entry); });
  if (_sub == kInvalidJournalSub) {
    _logger.error("no free journal slot - mirror is off");
  }

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

  char err[kErrorBufSize];
  for (const String& pattern : patterns) {
    // Правило в NVS могло зберегтись від версії з регексами - не мовчимо,
    // інакше фільтр просто "не працює" без пояснень.
    if (!addRule(deny, pattern.c_str(), err, sizeof(err))) {
      _logger.error("stored %s rule '%s' rejected: %s", deny ? "deny" : "allow", pattern.c_str(), err);
    }
  }
}

void ConsoleMqtt::persistRules(bool deny) {
  const char (*list)[kTagSize] = deny ? _deny : _allow;
  std::vector<String> patterns;
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (list[i][0] != '\0') {
      patterns.push_back(String(list[i]));
    }
  }
  _cfg.setStringArray(deny ? kKeyDeny : kKeyAllow, patterns);
}

bool ConsoleMqtt::addRule(bool deny, const char* tag, char* errBuf, size_t errBufSize) {
  char (*list)[kTagSize] = deny ? _deny : _allow;

  if (tag == nullptr || tag[0] == '\0' || strlen(tag) >= kTagSize) {
    if (errBuf != nullptr && errBufSize > 0) {
      snprintf(errBuf, errBufSize, "tag must be 1..%u characters", (unsigned)(kTagSize - 1));
    }
    return false;
  }

  // Правила в NVS могли лишитись від версії з регексами. Без цієї перевірки
  // "\\[MQTT\\]" тихо ліг би як "тег", не збігався б ні з чим - і фільтр просто
  // перестав би працювати, нічого не сказавши.
  for (const char* c = tag; *c != '\0'; ++c) {
    const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                    (*c >= '0' && *c <= '9') || *c == '.' || *c == '_' || *c == '-';
    if (!ok) {
      if (errBuf != nullptr && errBufSize > 0) {
        snprintf(errBuf, errBufSize, "'%c' is not allowed - this takes a tag, not a regexp", *c);
      }
      return false;
    }
  }

  for (size_t i = 0; i < kMaxRules; ++i) {
    if (list[i][0] != '\0') {
      continue;
    }
    strncpy(list[i], tag, kTagSize - 1);
    list[i][kTagSize - 1] = '\0';
    persistRules(deny);
    return true;
  }

  if (errBuf != nullptr && errBufSize > 0) {
    snprintf(errBuf, errBufSize, "%s list is full (%u rules)", deny ? "deny" : "allow", (unsigned)kMaxRules);
  }
  return false;
}

void ConsoleMqtt::clearRules(bool deny) {
  char (*list)[kTagSize] = deny ? _deny : _allow;
  for (size_t i = 0; i < kMaxRules; ++i) {
    list[i][0] = '\0';
  }
  _cfg.setStringArray(deny ? kKeyDeny : kKeyAllow, std::vector<String>{});
}

// Порядок правил: whitelist -> deny. Вбудованих правил більше НЕМАЄ: вони
// існували проти самогодування дзеркала, а з журналом кожен запис доходить
// сюди рівно один раз (див. коментар у заголовку).
bool ConsoleMqtt::passesFilters(const char* tag) const {
  // Порожній whitelist = пропускати все. Непорожній - тег мусить зійтися хоч з
  // одним правилом.
  bool hasAllow = false;
  bool allowed = false;
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_allow[i][0] == '\0') {
      continue;
    }
    hasAllow = true;
    if (journalTagMatches(_allow[i], tag)) {
      allowed = true;
      break;
    }
  }
  if (hasAllow && !allowed) {
    return false;
  }

  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_deny[i][0] != '\0' && journalTagMatches(_deny[i], tag)) {
      return false;
    }
  }

  return true;
}

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

bool ConsoleMqtt::deliver(const JournalEntry& entry) {
  if (!_active) {
    return true;
  }

  const uint32_t now = millis();

  do {
    // Нічого не накопичуємо: поки з'єднання немає, запис для дзеркала просто
    // не існує. Саме це й означає "без буферизації" - після конекту пачка
    // старих рядків не приїде.
    if (!_client.isConnected() || _client.isSuspended()) {
      break;
    }
    if (!passesFilters(entry.tag)) {
      break;
    }
    if (!takeToken(now)) {
      ++_droppedRate;
      break;
    }

    // Той самий вигляд, що в моніторі: префікс + текст. БЕЗ '\n' - тут один
    // запис = одне повідомлення, і підписник додає перенос сам, інакше в
    // mosquitto_sub між рядками порожні рядки.
    char line[JournalEntry::kTextSize + 16];
    if (journalFormatLine(entry, line, sizeof(line)) == 0) {
      break;
    }

    _client.publish(_topic.c_str(), line);
    ++_published;
  } while (false);

  // Звіт про зрізане лімітом. Сам іде в журнал і повернеться сюди наступною
  // помпою як звичайний запис - рівно один раз, без ризику лавини.
  if (_droppedRate > 0 && (now - _lastDropReportMs) >= kDropReportIntervalMs) {
    _logger.warn("rate limit dropped %u lines in the last %u s (%u lines/s)", (unsigned)_droppedRate,
                 (unsigned)(kDropReportIntervalMs / 1000), (unsigned)kRatePerSec);
    _droppedRate = 0;
    _lastDropReportMs = now;
  }

  // Завжди true: приймач lossy, курсор має йти далі навіть коли рядок нікуди
  // не пішов. Інакше дзеркало без мережі спинило б доставку решті.
  return true;
}

void ConsoleMqtt::dumpStatus() const {
  _logger.info("mirror = %s, topic = '%s'", _active ? "on" : "off", resolvedTopic().c_str());
  _logger.info("published = %u, dropped by rate limit = %u (limit %u lines/s, burst %u)",
               (unsigned)_published, (unsigned)_droppedRate, (unsigned)kRatePerSec, (unsigned)kBurst);

  size_t rules = 0;
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_allow[i][0] != '\0') {
      _logger.info("  allow %s", _allow[i]);
      ++rules;
    }
  }
  for (size_t i = 0; i < kMaxRules; ++i) {
    if (_deny[i][0] != '\0') {
      _logger.info("  deny  %s", _deny[i]);
      ++rules;
    }
  }
  if (rules == 0) {
    _logger.info("  (no tag rules - everything goes out)");
  }
}

#endif  // HAS_CONSOLE_MQTT
