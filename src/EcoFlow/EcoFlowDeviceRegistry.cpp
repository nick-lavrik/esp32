#include "EcoFlowDeviceRegistry.hpp"

#include <TLogger.hpp>

namespace {
TLogger logger{"ecoflow"};

// Прошитий перелік. Назви - латиницею (весь вивід англійською, див. CLAUDE.md);
// звірка з хмарою робиться за serialNumber, тому вони можуть не збігатися
// дослівно з назвами, заданими в застосунку EcoFlow.
const EcoFlowDeviceInfo kDevices[] = {
    {"DBEBZ5XD9180271", "DELTA mini", EcoFlowDeviceType::DeltaMini},
    // FOB (Forward Operating Base) — Передова операційна база. Найпопулярніший аналог ПТД.
    {"DCEBZ8ZE9250273", "DELTA Pro (FOB)", EcoFlowDeviceType::DeltaPro},
    {"DCEBZ8ZF2230701", "DELTA Pro (104)", EcoFlowDeviceType::DeltaPro},
    // {"DG21ZEB5REAF0196", "Smart Generator (Dual Fuel)", EcoFlowDeviceType::SmartGenerator},
    {"DG21ZEB5REAF0196", "Smart Generator", EcoFlowDeviceType::SmartGenerator},
    {"R331ZEB4ZEBW0026", "DELTA 2", EcoFlowDeviceType::Delta2},
};

// Ключі quota для одного логічного показника, у порядку пріоритету.
// EcoFlow використовує ДВІ схеми: з префіксом модуля (DELTA mini/Pro) і плоску
// (DELTA 2). Перевіряємо обидві, тому тип пристрою тут не потрібен - це
// стійкіше, ніж таблиця "тип -> схема".
const char *const kSocKeys[] = {"bmsMaster.soc", "soc", "pd.soc", "ems.lcdShowSoc", "lcdShowSoc"};
const char *const kSocPreciseKeys[] = {"bmsMaster.f32ShowSoc", "f32ShowSoc", "ems.f32LcdShowSoc",
                                       "f32LcdShowSoc"};
// Напруга на AC-вході - основний індикатор наявності мережі.
const char *const kAcInVoltKeys[] = {"inv.acInVol", "acInVol"};
const char *const kInputWattsKeys[] = {"inv.inputWatts", "inputWatts", "pd.wattsInSum", "wattsInSum"};
const char *const kOutputWattsKeys[] = {"inv.outputWatts", "outputWatts", "pd.wattsOutSum",
                                        "wattsOutSum"};
const char *const kRemainTimeKeys[] = {"pd.remainTime", "remainTime", "ems.chgRemainTime",
                                       "chgRemainTime"};

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

const char *ecoFlowDeviceTypeName(EcoFlowDeviceType type) {
  switch (type) {
    case EcoFlowDeviceType::DeltaMini: return "DELTA mini";
    case EcoFlowDeviceType::DeltaPro: return "DELTA Pro";
    case EcoFlowDeviceType::Delta2: return "DELTA 2";
    case EcoFlowDeviceType::SmartGenerator: return "Smart Generator";
    default: return "unknown";
  }
}

const char *ecoFlowGridStateName(EcoFlowGridState state) {
  switch (state) {
    case EcoFlowGridState::OnGrid: return "on-grid";
    case EcoFlowGridState::OffGrid: return "off-grid";
    default: return "unknown";
  }
}

EcoFlowDeviceRegistry::EcoFlowDeviceRegistry() {
  _devices.reserve(deviceCount());
  for (size_t i = 0; i < deviceCount(); i++) {
    EcoFlowDeviceState state;
    state.info = &kDevices[i];
    _devices.push_back(state);
  }
}

const EcoFlowDeviceInfo *EcoFlowDeviceRegistry::deviceTable() { return kDevices; }
size_t EcoFlowDeviceRegistry::deviceCount() { return sizeof(kDevices) / sizeof(kDevices[0]); }

EcoFlowDeviceState *EcoFlowDeviceRegistry::find(const String &serialNumber) {
  for (auto &state : _devices) {
    if (serialNumber == state.info->serialNumber) {
      return &state;
    }
  }
  return nullptr;
}

String EcoFlowDeviceRegistry::formatDuration(uint32_t milliseconds) {
  const uint32_t totalSeconds = milliseconds / 1000UL;
  const uint32_t days = totalSeconds / 86400UL;
  const uint32_t hours = (totalSeconds % 86400UL) / 3600UL;
  const uint32_t minutes = (totalSeconds % 3600UL) / 60UL;
  const uint32_t seconds = totalSeconds % 60UL;

  char buffer[32];
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "%lud %02luh %02lum", (unsigned long)days,
             (unsigned long)hours, (unsigned long)minutes);
  } else if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%luh %02lum %02lus", (unsigned long)hours,
             (unsigned long)minutes, (unsigned long)seconds);
  } else if (minutes > 0) {
    snprintf(buffer, sizeof(buffer), "%lum %02lus", (unsigned long)minutes,
             (unsigned long)seconds);
  } else {
    snprintf(buffer, sizeof(buffer), "%lus", (unsigned long)seconds);
  }
  return String(buffer);
}

void EcoFlowDeviceRegistry::setGrid(EcoFlowDeviceState &state, EcoFlowGridState next) {
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

  if (state.previousGrid != EcoFlowGridState::Unknown) {
    state.gridChangeCount++;
  }

  if (_gridCallback) {
    _gridCallback(state);
  }
}

bool EcoFlowDeviceRegistry::applyQuota(const String &serialNumber, JsonDocument &doc) {
  EcoFlowDeviceState *state = find(serialNumber);
  if (state == nullptr) {
    return false;
  }

  JsonObjectConst params = doc["params"].as<JsonObjectConst>();
  if (params.isNull()) {
    return false;
  }

  state->lastMessageMs = millis();
  state->lastMessageEpoch = time(nullptr);
  state->messageCount++;
  state->online = true;

  double value = 0;

  // quota - це ДЕЛЬТА: кожне поле оновлюємо лише коли воно реально прийшло,
  // інакше затерли б накопичений стан нулями.
  if (findNumber(params, kSocKeys, value)) {
    const int8_t previousSoc = state->socPercent;
    state->socPercent = (int8_t)value;
    // previousSoc < 0 - це ПЕРШЕ значення після старту, а не зміна заряду:
    // інакше колбек репортував би "-1% -> 60%".
    if (previousSoc >= 0 && previousSoc != state->socPercent && _socCallback) {
      _socCallback(*state, previousSoc);
    }
  }
  if (findNumber(params, kSocPreciseKeys, value)) {
    state->socPrecise = (float)value;
  }
  if (findNumber(params, kInputWattsKeys, value)) {
    state->inputWatts = (int32_t)value;
  }
  if (findNumber(params, kOutputWattsKeys, value)) {
    state->outputWatts = (int32_t)value;
  }
  if (findNumber(params, kRemainTimeKeys, value)) {
    state->remainTimeMinutes = (int32_t)value;
  }

  // Наявність напруги на AC-вході = мережа є. Розрахунок за потужністю тут
  // НЕ годиться: inputWatts падає до 0, коли батарея зарядилась, хоча мережа
  // на місці.
  if (findNumber(params, kAcInVoltKeys, value)) {
    state->acInputMilliVolts = (int32_t)value;
    setGrid(*state, value > 0 ? EcoFlowGridState::OnGrid : EcoFlowGridState::OffGrid);
  }

  return true;
}

bool EcoFlowDeviceRegistry::applyStatus(const String &serialNumber, JsonDocument &doc) {
  EcoFlowDeviceState *state = find(serialNumber);
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

size_t EcoFlowDeviceRegistry::expireStale(uint32_t timeoutMs) {
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
