#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#include <map>
#include <string>
#include <vector>

// Статичний перелік пристроїв акаунта + накопичений стан кожного з них.
//
// Чому хардкод, а не REST-список: серійні номери потрібні ще ДО першої підписки
// (ACL EcoFlow не приймає жодного wildcard), а REST вимагає синхронізованого
// часу для підпису. З хардкодом MQTT піднімається одразу після WiFi, а REST
// лишається необов'язковою звіркою.

enum class EcoflowDeviceType : uint8_t {
    Unknown = 0,
    DeltaMini,
    DeltaPro,
    Delta2,
    SmartGenerator,
};

// Тристан: поки відповідне поле не прийшло в quota, стан саме невідомий -
// плутати його з "немає мережі" не можна (quota надсилає ЛИШЕ те, що змінилось,
// тому багато полів довго лишаються без значення).
enum class EcoflowGridState : uint8_t {
    Unknown = 0,
    OffGrid,
    OnGrid,
};

const char *ecoflowDeviceTypeName(EcoflowDeviceType type);
const char *ecoflowGridStateName(EcoflowGridState state);

// Незмінна частина: те, що прошите у firmware.
struct EcoflowDeviceInfo {
    const char *serialNumber;
    const char *name;
    EcoflowDeviceType type;
};

// Змінна частина: накопичується з quota/status.
struct EcoflowDeviceState {
    const EcoflowDeviceInfo *info = nullptr;

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
    EcoflowGridState grid = EcoflowGridState::Unknown;
    // true -> grid визначено НЕ за напругою на AC-вході, а виведено з динаміки
    // заряду. Так доводиться робити для DELTA mini: вона публікує лише soc,
    // temp і remainTime, а inputWatts/outputWatts у неї завжди нулі, попри
    // реальні сотні ватів. У таблиці такий стан позначається зірочкою.
    bool gridInferred = false;
    // Коли пристрій перейшов у ПОТОЧНИЙ grid-стан - база для "скільки він у
    // цьому стані". 0, якщо стан ще не встановлювався.
    uint32_t gridSinceMs = 0;
    time_t gridSinceEpoch = 0;
    // Скільки тривав ПОПЕРЕДНІЙ grid-стан (мс) і який він був - заповнюється
    // на кожному переході.
    uint32_t previousGridDurationMs = 0;
    EcoflowGridState previousGrid = EcoflowGridState::Unknown;
    uint32_t gridChangeCount = 0;

    // --- супутні показники (для діагностики й майбутніх правил) ---
    int32_t acInputMilliVolts = -1;  // -1 - невідомо
    int32_t acInputFrequency = -1;   // Гц; -1 - невідомо
    int32_t inputWatts = -1;
    int32_t outputWatts = -1;
    int32_t remainTimeMinutes = -1;

    // false -> REST-знімок для цього пристрою заборонений (код 1006).
    bool snapshotAvailable = true;

    // Останнє значення полів, що приходили в quota.
    //
    // За замовчуванням тут лише поля з білого списку (kWhitelistParams) - це
    // на пристрій. captureAll вмикає захоплення ВСІХ, крім чорного списку:
    // тоді для DELTA 2 це 353 поля, тобто десятки кілобайт, тому вмикається
    // вручну командою 'ecoflow-capture'.
    std::map<std::string, float> trackedParams;
    bool captureAll = false;
    // Скільки полів відкинуто через kMaxTrackedParams - щоб мовчазне обрізання
    // не виглядало як "пристрій цього не шле".
    uint16_t droppedParams = 0;

    bool hasSoc() const { return socPercent >= 0; }
};

// Колбек на зміну grid-стану: викликається ПІСЛЯ оновлення state, тому
// state.previousGrid / state.previousGridDurationMs уже актуальні.
using EcoflowGridChangeCallback = std::function<void(const EcoflowDeviceState &state)>;
using EcoflowSocChangeCallback = std::function<void(const EcoflowDeviceState &state, int8_t previousSoc)>;

class EcoflowDeviceRegistry {
public:
    EcoflowDeviceRegistry();

    // Прошитий перелік пристроїв.
    static const EcoflowDeviceInfo *deviceTable();
    static size_t deviceCount();

    // Оновлює стан із params повідомлення /quota. Невідомий sn ігнорується
    // (з поверненням false) - у топіках акаунта можуть з'явитись пристрої,
    // яких немає в прошитому переліку.
    bool applyQuota(const String &serialNumber, JsonDocument &doc);

    // Те саме, але для REST-знімка (/device/quota/all): там поля лежать у
    // doc["data"] пласким списком, без обгортки "params".
    bool applySnapshot(const String &serialNumber, JsonDocument &doc);

    // Пристрій віддає 1006 "not allowed to get device info" - REST-знімок для
    // нього недоступний назавжди, повторно не смикаємо.
    void markSnapshotUnavailable(const String &serialNumber);

    // Оновлює online-статус із повідомлення /status.
    bool applyStatus(const String &serialNumber, JsonDocument &doc);

    // Позначає офлайн пристрої, від яких давно не було повідомлень.
    // Викликати періодично; повертає кількість тих, що щойно "відпали".
    size_t expireStale(uint32_t timeoutMs = 5 * 60 * 1000UL);

    const std::vector<EcoflowDeviceState> &devices() const { return _devices; }
    EcoflowDeviceState *find(const String &serialNumber);

    // Захоплення ВСІХ параметрів. serialNumber порожній -> для всіх пристроїв.
    // Повертає кількість пристроїв, яких торкнулись.
    size_t setCaptureAll(const String &serialNumber, bool enabled);

    // Значення одного параметра. nullptr, якщо такого немає (ще не приходив,
    // не проходить фільтр, або пристрій невідомий).
    const float *findParam(const String &serialNumber, const String &key) const;

    // Чи потрапляє ключ у білий / чорний список - для довідки в командах.
    static bool isWhitelistedParam(const char *key);
    static bool isBlacklistedParam(const char *key);
    // glob із '*' (будь-яка кількість символів). Регістр важливий.
    static bool wildcardMatch(const char *pattern, const char *text);

    // Зводить ключі EcoFlow до одного вигляду: camelCase -> snake_case,
    // '.' -> '_'. "inv.acInVol" -> "inv_ac_in_vol", "bmsMaster.soc" ->
    // "bms_master_soc". Потрібно тому, що API мішає схеми навіть для одного
    // пристрою: DELTA 2 у MQTT шле "acInVol", а в REST-знімку те саме поле
    // зветься "inv.acInVol". Пише у наданий буфер, без алокацій - викликається
    // на кожне поле кожного повідомлення.
    static void normalizeKey(const char *src, char *dst, size_t dstSize);
    static constexpr size_t kMaxKeyLength = 64;

    void onGridChange(EcoflowGridChangeCallback callback) { _gridCallback = callback; }
    void onSocChange(EcoflowSocChangeCallback callback) { _socCallback = callback; }

    // "1d 03h 12m" / "45s" - для читабельних тривалостей у логах.
    static String formatDuration(uint32_t milliseconds);

    // Поле remainTime у людський вигляд: "7h51m" / "29m", "-" якщо невідомо.
    // Без пояснень «до заряду / до розряду»: EcoFlow віддає ОДНЕ поле на оба
    // випадки, а що саме воно означає, видно з колонки GRID поруч.
    static String formatRemainTime(int32_t minutes);

private:
    std::vector<EcoflowDeviceState> _devices;
    EcoflowGridChangeCallback _gridCallback;
    EcoflowSocChangeCallback _socCallback;

    void setGrid(EcoflowDeviceState &state, EcoflowGridState next);
    // Спільне ядро applyQuota()/applySnapshot(): обидва отримують плаский
    // набір "ключ -> число", різниця лише в тому, де він лежить у документі.
    bool applyParams(EcoflowDeviceState &state, JsonObjectConst params);
};
