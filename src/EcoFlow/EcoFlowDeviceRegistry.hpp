#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#include <vector>

// Статичний перелік пристроїв акаунта + накопичений стан кожного з них.
//
// Чому хардкод, а не REST-список: серійні номери потрібні ще ДО першої підписки
// (ACL EcoFlow не приймає жодного wildcard), а REST вимагає синхронізованого
// часу для підпису. З хардкодом MQTT піднімається одразу після WiFi, а REST
// лишається необов'язковою звіркою.

enum class EcoFlowDeviceType : uint8_t {
    Unknown = 0,
    DeltaMini,
    DeltaPro,
    Delta2,
    SmartGenerator,
};

// Тристан: поки відповідне поле не прийшло в quota, стан саме невідомий -
// плутати його з "немає мережі" не можна (quota надсилає ЛИШЕ те, що змінилось,
// тому багато полів довго лишаються без значення).
enum class EcoFlowGridState : uint8_t {
    Unknown = 0,
    OffGrid,
    OnGrid,
};

const char *ecoFlowDeviceTypeName(EcoFlowDeviceType type);
const char *ecoFlowGridStateName(EcoFlowGridState state);

// Незмінна частина: те, що прошите у firmware.
struct EcoFlowDeviceInfo {
    const char *serialNumber;
    const char *name;
    EcoFlowDeviceType type;
};

// Змінна частина: накопичується з quota/status.
struct EcoFlowDeviceState {
    const EcoFlowDeviceInfo *info = nullptr;

    // --- присутність ---
    bool online = false;
    // millis() останнього повідомлення від пристрою; 0 - жодного не було.
    uint32_t lastMessageMs = 0;
    // Стінний час останнього повідомлення; 0 - невідомий (напр. ще не було NTP).
    time_t lastMessageEpoch = 0;
    uint32_t messageCount = 0;

    // --- заряд ---
    int8_t socPercent = -1;    // -1 - невідомо
    float socPrecise = NAN;    // дробовий SOC, якщо пристрій його шле

    // --- мережа (on grid) ---
    EcoFlowGridState grid = EcoFlowGridState::Unknown;
    // Коли пристрій перейшов у ПОТОЧНИЙ grid-стан - база для "скільки він у
    // цьому стані". 0, якщо стан ще не встановлювався.
    uint32_t gridSinceMs = 0;
    time_t gridSinceEpoch = 0;
    // Скільки тривав ПОПЕРЕДНІЙ grid-стан (мс) і який він був - заповнюється
    // на кожному переході.
    uint32_t previousGridDurationMs = 0;
    EcoFlowGridState previousGrid = EcoFlowGridState::Unknown;
    uint32_t gridChangeCount = 0;

    // --- супутні показники (для діагностики й майбутніх правил) ---
    int32_t acInputMilliVolts = -1;  // -1 - невідомо
    int32_t inputWatts = -1;
    int32_t outputWatts = -1;
    int32_t remainTimeMinutes = -1;

    bool hasSoc() const { return socPercent >= 0; }
};

// Колбек на зміну grid-стану: викликається ПІСЛЯ оновлення state, тому
// state.previousGrid / state.previousGridDurationMs уже актуальні.
using EcoFlowGridChangeCallback = std::function<void(const EcoFlowDeviceState &state)>;
using EcoFlowSocChangeCallback = std::function<void(const EcoFlowDeviceState &state, int8_t previousSoc)>;

class EcoFlowDeviceRegistry {
public:
    EcoFlowDeviceRegistry();

    // Прошитий перелік пристроїв.
    static const EcoFlowDeviceInfo *deviceTable();
    static size_t deviceCount();

    // Оновлює стан із params повідомлення /quota. Невідомий sn ігнорується
    // (з поверненням false) - у топіках акаунта можуть з'явитись пристрої,
    // яких немає в прошитому переліку.
    bool applyQuota(const String &serialNumber, JsonDocument &doc);

    // Оновлює online-статус із повідомлення /status.
    bool applyStatus(const String &serialNumber, JsonDocument &doc);

    // Позначає офлайн пристрої, від яких давно не було повідомлень.
    // Викликати періодично; повертає кількість тих, що щойно "відпали".
    size_t expireStale(uint32_t timeoutMs = 5 * 60 * 1000UL);

    const std::vector<EcoFlowDeviceState> &devices() const { return _devices; }
    EcoFlowDeviceState *find(const String &serialNumber);

    void onGridChange(EcoFlowGridChangeCallback callback) { _gridCallback = callback; }
    void onSocChange(EcoFlowSocChangeCallback callback) { _socCallback = callback; }

    // "1d 03h 12m" / "45s" - для читабельних тривалостей у логах.
    static String formatDuration(uint32_t milliseconds);

private:
    std::vector<EcoFlowDeviceState> _devices;
    EcoFlowGridChangeCallback _gridCallback;
    EcoFlowSocChangeCallback _socCallback;

    void setGrid(EcoFlowDeviceState &state, EcoFlowGridState next);
};
