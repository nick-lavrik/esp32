#include "EcoflowDeviceRegistry.hpp"

#include <TLogger.hpp>

namespace {
TLogger logger{"ecoflow"};

// Прошитий перелік. Назви - латиницею (весь вивід англійською, див. CLAUDE.md);
// звірка з хмарою робиться за serialNumber, тому вони можуть не збігатися
// дослівно з назвами, заданими в застосунку EcoFlow.
const EcoflowDeviceInfo kDevices[] = {
    {"DBEBZ5XD9180271", "DELTA mini", EcoflowDeviceType::DeltaMini},
    // FOB (Forward Operating Base) — Передова операційна база. Найпопулярніший аналог ПТД.
    // {"DCEBZ8ZE9250273", "DELTA Pro (FOB)", EcoflowDeviceType::DeltaPro},
    {"DCEBZ8ZF2230701", "DELTA Pro (104)", EcoflowDeviceType::DeltaPro},
    // {"DG21ZEB5REAF0196", "Smart Generator (Dual Fuel)", EcoflowDeviceType::SmartGenerator},
    // {"DG21ZEB5REAF0196", "Smart Generator", EcoflowDeviceType::SmartGenerator},
    {"R331ZEB4ZEBW0026", "DELTA 2", EcoflowDeviceType::Delta2},
};

// Ключі quota для одного логічного показника, у порядку пріоритету.
// EcoFlow використовує ДВІ схеми: з префіксом модуля (DELTA mini/Pro) і плоску
// (DELTA 2). Перевіряємо обидві, тому тип пристрою тут не потрібен - це
// стійкіше, ніж таблиця "тип -> схема".
// Порядок = ПРІОРИТЕТ, і він важливий саме через extra battery.
//
// EMS-рівень (lcdShowSoc) дає заряд УСІЄЇ системи - те саме число, що на екрані
// станції і в застосунку. BMS-рівень (bmsMaster/bms_bmsStatus.soc) - лише
// основна батарея. На DELTA 2 з підключеною extra battery різниця помітна:
// bms_bmsStatus.soc=61, bms_slave.soc=82, а bms_emsStatus.lcdShowSoc=72.
const char *const kSocKeys[] = {
    "bms_emsStatus.lcdShowSoc", "ems.lcdShowSoc", "lcdShowSoc",
    "bmsMaster.soc", "bms_bmsStatus.soc", "soc", "pd.soc",
};
const char *const kSocPreciseKeys[] = {
    "bms_emsStatus.f32LcdShowSoc", "ems.f32LcdShowSoc", "f32LcdShowSoc",
    "bmsMaster.f32ShowSoc", "bms_bmsStatus.f32ShowSoc", "f32ShowSoc",
};
// Напруга на AC-вході - основний індикатор наявності мережі.
const char *const kAcInVoltKeys[] = {"inv.acInVol", "acInVol"};
// Частота AC-входу - запасний індикатор, коли напруга не прийшла. Саме
// запасний, а не основний: заміряно, що DELTA 2 шле в MQTT лише acInVol
// (41 раз за 2 хв), а acInFreq у неї є ТІЛЬКИ в REST-знімку. DELTA Pro шле
// обидва. Snake_case-варіантів (inv_ac_in_freq) в API немає - перевірено і в
// MQTT, і в REST.
const char *const kAcInFreqKeys[] = {"inv.acInFreq", "acInFreq"};
const char *const kInputWattsKeys[] = {"inv.inputWatts", "inputWatts", "pd.wattsInSum", "wattsInSum"};
const char *const kOutputWattsKeys[] = {"inv.outputWatts", "outputWatts", "pd.wattsOutSum",
                                        "wattsOutSum"};
// remainTime беремо як МАКСИМУМ по всіх батареях (див. findMaxNumber): з extra
// battery кожна має власний прогноз (pd.remainTime=336 при
// bms_slave.remainTime=130), а систему можна вважати зарядженою/розрядженою
// лише коли закінчить остання.
const char *const kRemainTimeKeys[] = {"pd.remainTime", "remainTime", "bms_slave.remainTime",
                                       "ems.chgRemainTime", "chgRemainTime"};

// Білий список: захоплюється ЗАВЖДИ, незалежно від captureAll - це поля, з яких
// будується стан пристрою, плюс кілька корисних для діагностики.
//
// УВАГА: патерни тут - у НОРМАЛІЗОВАНОМУ вигляді (див. normalizeKey), бо
// матчаться проти вже нормалізованих ключів. Не плутати з kSocKeys і рештою
// вище: ті шукають СИРІ ключі у JSON, як їх шле API.
const char *const kWhitelistParams[] = {
    "*soc", "*ac_in_vol", "*ac_in_freq", "*ac_in_amp",
    "*input_watts", "*output_watts", "*watts_in_sum", "*watts_out_sum",
    "*remain_time", "*temp", "*chg_state", "*chg_dsg_state",
};

// НОРМАЛІЗОВАНІ ключі за пріоритетом - застосовуються до НАКОПИЧЕНОГО стану
// (trackedParams), а не до окремого повідомлення.
//
// Чому не можна брати "перший наявний у цьому повідомленні": quota шле ДЕЛЬТИ,
// тож повідомлення з одним лише bms_bmsStatus.soc=61 перезаписало б системний
// bms_emsStatus.lcdShowSoc=72, який прийшов раніше окремим повідомленням. Саме
// так DELTA 2 з extra battery показувала 61% замість 72%.
const char *const kSocPriority[] = {
    "bms_ems_status_lcd_show_soc", "ems_lcd_show_soc", "lcd_show_soc",
    "bms_master_soc", "bms_bms_status_soc", "soc", "pd_soc",
};
const char *const kSocPrecisePriority[] = {
    "bms_ems_status_f32_lcd_show_soc", "ems_f32_lcd_show_soc", "f32_lcd_show_soc",
    "bms_master_f32_show_soc", "bms_bms_status_f32_show_soc", "f32_show_soc",
};

// Ігноруються ЗАВЖДИ, навіть при captureAll: поля, що описують стан іконок на
// екрані самої станції. Їх десятки, вони міняються постійно і не кажуть нічого
// про енергію - тобто це чистий тиск на пам'ять.
const char *const kBlacklistParams[] = {
    "pd_icon*", "*icon_state*", "*unused*", "*reserve*",
};

// Вище цього EcoFlow віддає «невизначено», а не прогноз: у логах траплялись
// 5999, 5940 і 5939 (тобто ~99 годин). Реальний прогноз такої довжини все одно
// неінформативний, тому відсікаємо все, що більше.
constexpr int32_t kRemainTimeUnknownFrom = 6000; // 5900;

// Стеля на пристрій. 353 поля DELTA 2 у std::map - це десятки кілобайт, а heap
// на цій платі ми виборювали окремо (див. команду 'heap').
constexpr size_t kMaxTrackedParams = 400;

template <size_t N>
bool matchesAny(const char *const (&patterns)[N], const char *key) {
  for (size_t i = 0; i < N; i++) {
    if (EcoflowDeviceRegistry::wildcardMatch(patterns[i], key)) {
      return true;
    }
  }
  return false;
}

// Максимум серед НАЯВНИХ ключів, а не перший знайдений. 5999+ - маркер
// «невизначено» в EcoFlow, тому в підрахунок не входить.
template <size_t N>
bool findMaxNumber(JsonObjectConst params, const char *const (&keys)[N], double &out) {
  bool found = false;
  double best = 0;
  for (size_t i = 0; i < N; i++) {
    JsonVariantConst value = params[keys[i]];
    if (value.isNull() || !value.is<double>()) {
      continue;
    }
    const double candidate = value.as<double>();
    if (candidate < 0 || candidate >= kRemainTimeUnknownFrom) {
      continue;
    }
    if (!found || candidate > best) {
      best = candidate;
      found = true;
    }
  }
  if (found) {
    out = best;
  }
  return found;
}

template <size_t N>
bool findNumber(JsonObjectConst params, const char *const (&keys)[N], double &out) {
  for (size_t i = 0; i < N; i++) {
    JsonVariantConst value = params[keys[i]];
    if (!value.isNull() && value.is<double>()) {
      out = value.as<double>();
      return true;
    }
  }
  return false;
}
}  // namespace

const char *ecoflowDeviceTypeName(EcoflowDeviceType type) {
  switch (type) {
    case EcoflowDeviceType::DeltaMini: return "DELTA mini";
    case EcoflowDeviceType::DeltaPro: return "DELTA Pro";
    case EcoflowDeviceType::Delta2: return "DELTA 2";
    case EcoflowDeviceType::SmartGenerator: return "Smart Generator";
    default: return "unknown";
  }
}

const char *ecoflowGridStateName(EcoflowGridState state) {
  switch (state) {
    case EcoflowGridState::OnGrid: return "on-grid";
    case EcoflowGridState::OffGrid: return "off-grid";
    default: return "unknown";
  }
}

bool EcoflowDeviceRegistry::wildcardMatch(const char *pattern, const char *text) {
  // Ітеративний glob із поверненням до останньої '*': рекурсія тут не потрібна,
  // а стек мережевого таска й так не безмежний.
  const char *star = nullptr;
  const char *textAfterStar = nullptr;

  while (*text != '\0') {
    if (*pattern == '*') {
      star = pattern++;
      textAfterStar = text;
    } else if (*pattern == *text) {
      pattern++;
      text++;
    } else if (star != nullptr) {
      pattern = star + 1;
      text = ++textAfterStar;
    } else {
      return false;
    }
  }
  while (*pattern == '*') {
    pattern++;
  }
  return *pattern == '\0';
}

void EcoflowDeviceRegistry::normalizeKey(const char *src, char *dst, size_t dstSize) {
  if (dstSize == 0) { return; }
  size_t out = 0;
  bool previousWasUnderscore = false;

  for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; i++) {
    const char c = src[i];

    if (c == '.' || c == '_' || c == '-') {
      // Роздільники зводимо до '_' і не дублюємо: "bms_bmsStatus.x" не має
      // перетворитись на "bms_bms_status__x".
      if (!previousWasUnderscore && out > 0) {
        dst[out++] = '_';
        previousWasUnderscore = true;
      }
      continue;
    }

    if (c >= 'A' && c <= 'Z') {
      // Межа слова - лише там, де перед великою літерою йде маленька або цифра.
      // Інакше абревіатури (напр. "AC") розсипались би на "a_c".
      const char previous = i > 0 ? src[i - 1] : '\0';
      const bool boundary = (previous >= 'a' && previous <= 'z') ||
                            (previous >= '0' && previous <= '9');
      if (boundary && !previousWasUnderscore && out > 0 && out + 2 < dstSize) {
        dst[out++] = '_';
      }
      dst[out++] = (char)(c - 'A' + 'a');
      previousWasUnderscore = false;
      continue;
    }

    dst[out++] = c;
    previousWasUnderscore = false;
  }

  dst[out] = '\0';
}

bool EcoflowDeviceRegistry::isWhitelistedParam(const char *key) {
  return matchesAny(kWhitelistParams, key);
}

bool EcoflowDeviceRegistry::isBlacklistedParam(const char *key) {
  return matchesAny(kBlacklistParams, key);
}

size_t EcoflowDeviceRegistry::setCaptureAll(const String &serialNumber, bool enabled) {
  size_t affected = 0;
  for (auto &state : _devices) {
    if (serialNumber.length() > 0 && serialNumber != state.info->serialNumber) {
      continue;
    }
    state.captureAll = enabled;
    if (!enabled) {
      // Звільняємо пам'ять одразу: інакше вимкнення не поверне heap, бо map
      // тримає вузли до самого знищення.
      std::map<std::string, float> onlyImportant;
      for (const auto &kv : state.trackedParams) {
        if (isWhitelistedParam(kv.first.c_str())) {
          onlyImportant.insert(kv);
        }
      }
      state.trackedParams.swap(onlyImportant);
      state.droppedParams = 0;
    }
    affected++;
  }
  return affected;
}

const float *EcoflowDeviceRegistry::findParam(const String &serialNumber,
                                             const String &key) const {
  for (const auto &state : _devices) {
    if (serialNumber != state.info->serialNumber) {
      continue;
    }
    // Запит нормалізуємо так само, щоб 'inv.acInVol' знаходило 'inv_ac_in_vol'.
    char normalized[kMaxKeyLength];
    normalizeKey(key.c_str(), normalized, sizeof(normalized));
    auto it = state.trackedParams.find(normalized);
    return it != state.trackedParams.end() ? &it->second : nullptr;
  }
  return nullptr;
}

EcoflowDeviceRegistry::EcoflowDeviceRegistry() {
  _devices.reserve(deviceCount());
  for (size_t i = 0; i < deviceCount(); i++) {
    EcoflowDeviceState state;
    state.info = &kDevices[i];
    _devices.push_back(state);
  }
}

const EcoflowDeviceInfo *EcoflowDeviceRegistry::deviceTable() { return kDevices; }
size_t EcoflowDeviceRegistry::deviceCount() { return sizeof(kDevices) / sizeof(kDevices[0]); }

EcoflowDeviceState *EcoflowDeviceRegistry::find(const String &serialNumber) {
  for (auto &state : _devices) {
    if (serialNumber == state.info->serialNumber) {
      return &state;
    }
  }
  return nullptr;
}

String EcoflowDeviceRegistry::formatDuration(uint32_t milliseconds) {
  const uint32_t totalSeconds = milliseconds / 1000UL;
  const uint32_t days = totalSeconds / 86400UL;
  const uint32_t hours = (totalSeconds % 86400UL) / 3600UL;
  const uint32_t minutes = (totalSeconds % 3600UL) / 60UL;
  const uint32_t seconds = totalSeconds % 60UL;

  char buffer[32];
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%lud%02luh%02lum", static_cast<unsigned long>(days),
             static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  } else if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%luh%02lum%02lus", static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
  } else if (minutes > 0) {
    snprintf(buffer, sizeof(buffer), "%lum%02lus", static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
  } else {
    snprintf(buffer, sizeof(buffer), "%lus", static_cast<unsigned long>(seconds));
  }
  return String(buffer);
}

String EcoflowDeviceRegistry::formatRemainTime(int32_t minutes) {
  // 0 - теж «нема що прогнозувати» (заряд завершено), а не «нуль хвилин».
  if (minutes <= 0 || minutes >= kRemainTimeUnknownFrom) {
    return String("-");
  }

  char buffer[16];
  if (minutes >= 60) {
    snprintf(buffer, sizeof(buffer), "%ldh%02ldm", (long)(minutes / 60), (long)(minutes % 60));
  } else {
    snprintf(buffer, sizeof(buffer), "%ldm", (long)minutes);
  }

  return String(buffer);
}

void EcoflowDeviceRegistry::setGrid(EcoflowDeviceState &state, EcoflowGridState next) {
  if (next == state.grid) {
    return;
  }

  const uint32_t now = millis();
  state.previousGrid = state.grid;
  // Перше визначення стану - не "перехід", тривалості попереднього немає.
  state.previousGridDurationMs = state.gridSinceMs != 0 ? now - state.gridSinceMs : 0;

  state.grid = next;
  state.gridSinceMs = now;
  state.gridSinceEpoch = time(nullptr);

  if (state.previousGrid != EcoflowGridState::Unknown) {
    state.gridChangeCount++;
  }

  if (_gridCallback) {
    _gridCallback(state);
  }
}

bool EcoflowDeviceRegistry::applySnapshot(const String &serialNumber, JsonDocument &doc) {
  EcoflowDeviceState *state = find(serialNumber);
  if (state == nullptr) {
    return false;
  }
  JsonObjectConst data = doc["data"].as<JsonObjectConst>();
  if (data.isNull()) {
    return false;
  }
  return applyParams(*state, data);
}

void EcoflowDeviceRegistry::markSnapshotUnavailable(const String &serialNumber) {
  EcoflowDeviceState *state = find(serialNumber);
  if (state != nullptr) {
    state->snapshotAvailable = false;
    // REST-знімка не буде, тож MQTT - єдине джерело: вмикаємо повний захват
    // автоматично. Таких пристроїв мало і полів у них небагато (DELTA mini - 23).
    state->captureAll = true;
  }
}

bool EcoflowDeviceRegistry::applyQuota(const String &serialNumber, JsonDocument &doc) {
  EcoflowDeviceState *state = find(serialNumber);
  if (state == nullptr) {
    return false;
  }

  JsonObjectConst params = doc["params"].as<JsonObjectConst>();
  if (params.isNull()) {
    return false;
  }

  const uint32_t now = millis();
  if (state->lastMessageMs > 0 && !state->online) {
    logger.debug("%s (%s) woke up after %s silence", state->info->serialNumber,
                state->info->name, formatDuration(now - state->lastMessageMs).c_str());
  }

  state->lastMessageMs = now;
  state->lastMessageEpoch = time(nullptr);
  state->messageCount++;
  state->online = true;

  return applyParams(*state, params);
}

bool EcoflowDeviceRegistry::applyParams(EcoflowDeviceState &stateRef, JsonObjectConst params) {
  EcoflowDeviceState *state = &stateRef;
  double value = 0;

  // Дзеркало полів: важливі - завжди, решта - лише при captureAll і ніколи те,
  // що в чорному списку.
  for (JsonPairConst kv : params) {
    if (!kv.value().is<double>()) {
      continue;
    }
    char key[kMaxKeyLength];
    normalizeKey(kv.key().c_str(), key, sizeof(key));
    if (isBlacklistedParam(key)) {
      continue;
    }
    if (!isWhitelistedParam(key) && !state->captureAll) {
      continue;
    }
    // Новий ключ понад стелю не додаємо, але вже відомі оновлюємо далі.
    if (state->trackedParams.size() >= kMaxTrackedParams &&
        state->trackedParams.find(key) == state->trackedParams.end()) {
      if (state->droppedParams < 0xFFFF) { state->droppedParams++; }
      continue;
    }
    state->trackedParams[key] = (float)kv.value().as<double>();
  }

  // quota - це ДЕЛЬТА: кожне поле оновлюємо лише коли воно реально прийшло,
  // інакше затерли б накопичений стан нулями.
  int socDelta = 0;
  // Беремо з НАКОПИЧЕНОГО стану за пріоритетом (див. kSocPriority), а не з
  // цього повідомлення: інакше дельта з полем нижчого рівня затирає системне.
  double socFromTracked = 0;
  bool haveSoc = false;
  for (const char *key : kSocPriority) {
    auto it = state->trackedParams.find(key);
    if (it != state->trackedParams.end()) {
      socFromTracked = it->second;
      haveSoc = true;
      break;
    }
  }

  if (haveSoc) {
    value = socFromTracked;
    const int8_t previousSoc = state->socPercent;
    state->socPercent = (int8_t)value;
    if (previousSoc >= 0) { socDelta = (int)state->socPercent - (int)previousSoc; }
    // previousSoc < 0 - це ПЕРШЕ значення після старту, а не зміна заряду:
    // інакше колбек репортував би "-1% -> 60%".
    if (previousSoc >= 0 && previousSoc != state->socPercent && _socCallback) {
      _socCallback(*state, previousSoc);
    }
  }
  for (const char *key : kSocPrecisePriority) {
    auto it = state->trackedParams.find(key);
    if (it != state->trackedParams.end()) {
      state->socPrecise = it->second;
      break;
    }
  }
  if (findNumber(params, kInputWattsKeys, value)) {
    state->inputWatts = (int32_t)value;
  }
  if (findNumber(params, kOutputWattsKeys, value)) {
    state->outputWatts = (int32_t)value;
  }
  // Максимум по всіх батареях з НАКОПИЧЕНОГО стану: з extra battery кожна має
  // власний прогноз, а система готова лише коли закінчить остання.
  // TODO: ems_chg_remain_time / pd_remain_time - якщо "заряджається" (світло є)
  // TODO: ems_dsg_remain_time / pd_remain_time - якщо "розряджається" (світла нема)
  {
    float best = -1;
    for (const auto &kv : state->trackedParams) {
      // if (!wildcardMatch("*remain_time", kv.first.c_str())) { continue; }
      if (!wildcardMatch("pd_remain_time", kv.first.c_str())) { continue; }
      // if (kv.second < 0 || kv.second >= kRemainTimeUnknownFrom) { continue; }
      if (abs(kv.second) > best) { best = abs(kv.second); }
    }
    if (best >= 0) { state->remainTimeMinutes = static_cast<int32_t>(best); }
  }

  // Наявність напруги на AC-вході = мережа є. Розрахунок за потужністю тут
  // НЕ годиться: inputWatts падає до 0, коли батарея зарядилась, хоча мережа
  // на місці.
  if (findNumber(params, kAcInVoltKeys, value)) {
    state->acInputMilliVolts = (int32_t)value;
    state->gridInferred = false;
    setGrid(*state, value > 0 ? EcoflowGridState::OnGrid : EcoflowGridState::OffGrid);
  } else if (findNumber(params, kAcInFreqKeys, value)) {
    // Напруга в цьому повідомленні не прийшла, а частота - так. Ознака та сама:
    // є частота на вході - є мережа. Пріоритет нижчий за напругу навмисно, щоб
    // не переписувати свіжіший і частіший сигнал застарілим.
    state->acInputFrequency = (int32_t)value;
    state->gridInferred = false;
    setGrid(*state, value > 0 ? EcoflowGridState::OnGrid : EcoflowGridState::OffGrid);
  } else if (socDelta != 0 && state->acInputMilliVolts < 0 && state->acInputFrequency < 0) {
    // Прямого сигналу немає і не буде (пристрій не шле acInVol) - лишається
    // динаміка заряду. Зростає -> щось його заряджає; для DELTA mini це саме
    // мережа: DC/car-вхід у неї вимкнений (mppt.carState = 0).
    // Похибка можлива (сонце, генератор), тому позначаємо як виведений стан.
    state->gridInferred = true;
    setGrid(*state, socDelta > 0 ? EcoflowGridState::OnGrid : EcoflowGridState::OffGrid);
  }

  return true;
}

bool EcoflowDeviceRegistry::applyStatus(const String &serialNumber, JsonDocument &doc) {
  EcoflowDeviceState *state = find(serialNumber);
  if (state == nullptr) {
    return false;
  }

  const int status = doc["params"]["status"] | -1;
  if (status < 0) {
    return false;
  }

  state->online = (status == 1);
  if (state->online) {
    state->lastMessageMs = millis();
    state->lastMessageEpoch = time(nullptr);
  }
  logger.info("%s (%s) is %s", state->info->serialNumber, state->info->name,
              state->online ? "online" : "offline");
  return true;
}

size_t EcoflowDeviceRegistry::expireStale(uint32_t timeoutMs) {
  const uint32_t now = millis();
  size_t expired = 0;

  for (auto &state : _devices) {
    if (!state.online || state.lastMessageMs == 0) {
      continue;
    }
    if (now - state.lastMessageMs < timeoutMs) {
      continue;
    }
    state.online = false;
    expired++;
    logger.warn("%s (%s) went silent for %s - marking offline", state.info->serialNumber,
                state.info->name, formatDuration(now - state.lastMessageMs).c_str());
  }
  return expired;
}
