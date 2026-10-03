#include "BlinkLed.hpp"

#include <Logger.hpp>

#include "App/AppGlobals.hpp"

#if BLINK_LED_PIN
namespace {

// Один сегмент патерну: тримати рівень `on` протягом `ms`, тоді перейти
// до наступного сегменту (з циклічним поверненням на початок масиву).
struct BlinkSegment {
  uint16_t ms;
  bool on;
};

// Стан "пошук WiFi" — світиться постійно. Один сегмент з довільною
// тривалістю: рушій періодично переписує той самий рівень, видимого
// перемикання це не дає.
constexpr BlinkSegment kPatternSearchingWifi[] = {
    {1000, true},
};

// Стан "синхронізація часу" (лише перша спроба при завантаженні):
// два коротких спалахи, пауза 1с, повтор.
constexpr BlinkSegment kPatternSyncingTime[] = {
    {100, true},
    {150, false},
    {100, true},
    {1000, false},
};

// Стан "підключення MQTT": один довгий спалах, пауза 1с, повтор.
constexpr BlinkSegment kPatternConnectingMqtt[] = {
    {500, true},
    {1000, false},
};

// Стан "AP-режим": три коротких спалахи, пауза, три довгих спалахи,
// пауза, повтор із початку.
constexpr BlinkSegment kPatternApMode[] = {
    {80, true},  {100, false}, {80, true},  {100, false}, {80, true},  {500, false},
    {400, true}, {150, false}, {400, true}, {150, false}, {400, true}, {500, false},
};

// Робочий стан: один короткий спалах щосекунди.
constexpr BlinkSegment kPatternWorking[] = {
    {20, true},
    {980, false},
};

struct BlinkPatternDef {
  const BlinkSegment* segments;
  uint8_t length;
};

// Порядок елементів має збігатись з enum BlinkState нижче — використовується
// як індекс масиву.
const BlinkPatternDef kBlinkPatterns[] = {
    {kPatternApMode, sizeof(kPatternApMode) / sizeof(kPatternApMode[0])},
    {kPatternSearchingWifi, sizeof(kPatternSearchingWifi) / sizeof(kPatternSearchingWifi[0])},
    {kPatternSyncingTime, sizeof(kPatternSyncingTime) / sizeof(kPatternSyncingTime[0])},
    {kPatternConnectingMqtt, sizeof(kPatternConnectingMqtt) / sizeof(kPatternConnectingMqtt[0])},
    {kPatternWorking, sizeof(kPatternWorking) / sizeof(kPatternWorking[0])},
};

enum class BlinkState : uint8_t {
  ApMode = 0,
  SearchingWifi = 1,
  SyncingTime = 2,
  ConnectingMqtt = 3,
  Working = 4,
};

const char* blinkStateName(BlinkState state) {
  switch (state) {
    case BlinkState::ApMode:
      return "ap_mode";
    case BlinkState::SearchingWifi:
      return "searching_wifi";
    case BlinkState::SyncingTime:
      return "syncing_time";
    case BlinkState::ConnectingMqtt:
      return "connecting_mqtt";
    default:
      return "working";
  }
}

// Ручний перемикач поверх авто-індикації: off/on тримають пін у фіксованому
// рівні й ігнорують стан плати, auto запускає драбинку currentBlinkState().
enum class BlinkMode : uint8_t { Off, On, Auto };

BlinkMode gBlinkMode = BlinkMode::Auto;

BlinkMode parseBlinkMode(const String& s) {
  if (s.equalsIgnoreCase("off")) return BlinkMode::Off;
  if (s.equalsIgnoreCase("on")) return BlinkMode::On;
  return BlinkMode::Auto;
}

const char* blinkModeName(BlinkMode mode) {
  switch (mode) {
    case BlinkMode::Off:
      return "off";
    case BlinkMode::On:
      return "on";
    default:
      return "auto";
  }
}

// Ручне форсування конкретного патерну (`blink 0..4`) — лишень у RAM, у NVS
// не пишеться. Перекриває off/on/auto, доки його не скасують іншим `blink`.
bool gBlinkForced = false;
BlinkState gForcedState = BlinkState::Working;

// Пріоритетна драбинка станів. NTP враховується лише до першої вдалої
// синхронізації при завантаженні — подальший періодичний re-sync патерн
// уже не змінює (навмисно, за задачею).
BlinkState currentBlinkState() {
  const NetworkSupervisorState netState = netSupervisor.state();
  if (netState == NetworkSupervisorState::STARTING_AP || netState == NetworkSupervisorState::AP_MODE) {
    return BlinkState::ApMode;
  }
  if (!netSupervisor.isConnected()) {
    return BlinkState::SearchingWifi;
  }

  static bool ntpSyncedOnce = false;
  if (!ntpSyncedOnce) {
    if (ntp.isSynced()) {
      ntpSyncedOnce = true;
    } else {
      return BlinkState::SyncingTime;
    }
  }

  if (!mqtt.isConnected()) {
    return BlinkState::ConnectingMqtt;
  }
  return BlinkState::Working;
}

}  // namespace
#endif

void setupBlinkLED(SerialCommander& commander) {
#if BLINK_LED_PIN
  pinMode(BLINK_LED_PIN, OUTPUT);
  digitalWrite(BLINK_LED_PIN, HIGH);  // вимкнено (інверсна логіка)

  gBlinkMode = parseBlinkMode(configStorage.getString(CFG_BLINK_LED, "auto"));

  scheduler.addCronTask(10, []() {
    static bool havePrevState = false;
    static BlinkState prevState = BlinkState::Working;
    static uint8_t segmentIndex = 0;
    static uint32_t segmentStartMs = 0;

    if (!gBlinkForced && gBlinkMode == BlinkMode::Off) {
      havePrevState = false;  // перехід у auto/forced нехай почне патерн з початку
      digitalWrite(BLINK_LED_PIN, HIGH);
      return;
    }
    if (!gBlinkForced && gBlinkMode == BlinkMode::On) {
      havePrevState = false;
      digitalWrite(BLINK_LED_PIN, LOW);
      return;
    }

    const BlinkState state = gBlinkForced ? gForcedState : currentBlinkState();
    const BlinkPatternDef& pattern = kBlinkPatterns[static_cast<uint8_t>(state)];
    const uint32_t now = millis();

    // Зміна стану завжди починає патерн з першого сегменту, а не
    // продовжує з довільної точки попереднього патерну.
    if (!havePrevState || state != prevState) {
      havePrevState = true;
      prevState = state;
      segmentIndex = 0;
      segmentStartMs = now;
      digitalWrite(BLINK_LED_PIN, pattern.segments[0].on ? LOW : HIGH);
      return;
    }

    if (now - segmentStartMs >= pattern.segments[segmentIndex].ms) {
      segmentIndex = (segmentIndex + 1) % pattern.length;
      segmentStartMs = now;
      digitalWrite(BLINK_LED_PIN, pattern.segments[segmentIndex].on ? LOW : HIGH);
    }
  });

  commander.registerCommand(
      "blink", "LED control: blink [on|off|auto|0-4], no args - show status", [](const String& args) {
        if (args.isEmpty()) {
          Logger::info("blink: mode=%s%s", blinkModeName(gBlinkMode), gBlinkForced ? " (forced)" : "");
          if (gBlinkForced) {
            Logger::info("blink: forced pattern=%u (%s)", static_cast<unsigned>(gForcedState),
                         blinkStateName(gForcedState));
          }
          Logger::info("blink usage: blink [on|off|auto|0-4]");
          Logger::info("  off  - LED always off");
          Logger::info("  on   - LED always on");
          Logger::info("  auto - status indication (wifi search / time sync / mqtt connect / AP mode / working)");
          Logger::info("  0-4  - force one pattern for testing, not saved to NVS:");
          Logger::info("         0=ap_mode 1=searching_wifi 2=syncing_time 3=connecting_mqtt 4=working");
          return;
        }

        bool isNumeric = args.length() > 0;
        for (size_t i = 0; i < args.length(); ++i) {
          if (!isDigit(args[i])) {
            isNumeric = false;
            break;
          }
        }
        if (isNumeric) {
          const int n = args.toInt();
          if (n < 0 || n > 4) {
            Logger::info("blink: pattern index must be 0..4, got \"%s\"", args.c_str());
            return;
          }
          gForcedState = static_cast<BlinkState>(n);
          gBlinkForced = true;
          Logger::info("blink: forced pattern=%d (%s), not saved", n, blinkStateName(gForcedState));
          return;
        }

        if (!args.equalsIgnoreCase("off") && !args.equalsIgnoreCase("on") && !args.equalsIgnoreCase("auto")) {
          Logger::info("blink: unknown mode \"%s\" (usage: blink [on|off|auto|0-4])", args.c_str());
          return;
        }

        gBlinkForced = false;
        const BlinkMode requested = parseBlinkMode(args);
        gBlinkMode = requested;
        configStorage.setString(CFG_BLINK_LED, blinkModeName(requested));
        Logger::info("blink: mode=%s", blinkModeName(requested));
      });
#endif
}
