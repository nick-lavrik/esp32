// Домен SD_PROBE (під HAS_SD_WORKBENCH): діагностика шини й пінів картки,
// коли SD.begin() не піднімається - sdprobe/sdscan/sdbb. Лише SPI-режим.

#include "Sd.hpp"

#if HAS_SD_WORKBENCH && !defined(SD_USE_SDMMC)

#include <Arduino.h>
#include <SPI.h>

#include <Logger.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Screen/DisplayBusYield.hpp"
#include "SdBus.hpp"

// ---------------------------------------------------------------------------
// Низькорівневий пробник TF-картки по SPI (команда "sdprobe").
//
// НАВІЩО: SD.begin() повертає лише bool, і за ним неможливо відрізнити
// три принципово різні причини відмови:
//   1) картка взагалі не відповідає  -> помилка в пінах/проводці/живленні;
//   2) картка відповідає на CMD0/CMD8 -> шина справна, а не монтується
//      файлова система (exFAT/пошкоджений розділ/картка >32GB);
//   3) відповідає лише на низькій частоті -> проблема з якістю шини
//      (тут вона СПІЛЬНА з дисплеєм).
// Пробник шле CMD0 (GO_IDLE_STATE) і CMD8 (SEND_IF_COND) вручну, як це
// робить сама специфікація SD в SPI-режимі, і друкує сирі R1-відповіді.
// ---------------------------------------------------------------------------

// Одна команда SD в SPI-режимі; повертає R1 (0xFF = картка не відповіла).
// Картка тримає лінію DO (MISO) притиснутою в LOW, поки зайнята - і поки
// вона це робить, будь-яке читання дає 0x00.
//
// САМЕ НА ЦЕ попалась перша версія проби: вона вважала валідною R1 будь-який
// байт зі скинутим бітом 7, а 0x00 цю умову задовольняє. Тому busy-стан
// читався як "CMD0 -> R1=0x00", хоча насправді картка просто ще не
// відпустила лінію після GO_IDLE_STATE, який робить SD.end().
// Повертає false, якщо картка не звільнилась за timeoutMs.
static bool sdProbeWaitReady(uint32_t timeoutMs) {
  uint32_t start = millis();
  do {
    if (SPI.transfer(0xFF) == 0xFF) return true;
  } while (millis() - start < timeoutMs);
  return false;
}

// Одна команда SD у SPI-режимі.
// Повертає R1, або 0xFF - картка не відповіла, або 0xFE - картка не
// звільнила лінію (busy). Обидва службові коди мають виставлений біт 7,
// тому не можуть збігтися з валідною R1.
static uint8_t sdProbeCmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint32_t waitMs = 500) {
  if (!sdProbeWaitReady(waitMs)) return 0xFE;

  SPI.transfer(0xFF);
  SPI.transfer(0x40 | cmd);
  SPI.transfer((uint8_t)(arg >> 24));
  SPI.transfer((uint8_t)(arg >> 16));
  SPI.transfer((uint8_t)(arg >> 8));
  SPI.transfer((uint8_t)arg);
  SPI.transfer(crc);

  // R1 приходить у межах 8 байтів: перший байт, що НЕ 0xFF (шина в спокої)
  // і має скинутий біт 7.
  for (int i = 0; i < 10; ++i) {
    uint8_t r = SPI.transfer(0xFF);
    if (r != 0xFF && !(r & 0x80)) return r;
  }
  return 0xFF;
}

// force=false - неруйнівний режим (за замовчуванням): якщо картка вже
// змонтована, сира проба не виконується взагалі. Причина в тому, що
// відібрати шину у драйвера можна лише через SD.end(), а повторний
// SD.begin() у тому самому сеансі надійно НЕ піднімається (перевірено на
// цій платі: "sdWait: Wait Failed" -> "GO_IDLE_STATE failed" -> f_mount (3)),
// тобто проба коштувала б робочої картки до перезавантаження.
void sdProbe(bool force) {
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD probe (raw SPI) ===============================");
  Logger::info("Pins: CS=%d SCK=%d MOSI=%d MISO=%d", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  if (SD.cardType() != CARD_NONE && !force) {
    Logger::info("The card is already mounted - a raw probe is unnecessary and unsafe.");
    Logger::info("  Card details: status sd");
    Logger::info("  If you need the probe right now: sdprobe force");
    Logger::info("  (this unmounts the card for good, until reboot).");
    Logger::info("============================================================");
    return;
  }

  // Звільняємо шину від драйвера SD (якщо він піднявся) і деактивуємо
  // дисплей - інакше ST7735 їстиме наші такти як свої команди.
  const bool wasMounted = (SD.cardType() != CARD_NONE);
  if (wasMounted) {
    Logger::warn("force: unmounting the card, a reboot will be needed after the probe");
    SD.end();
    // sdcard_uninit() шле картці GO_IDLE_STATE - їй треба час відпустити DO.
    delay(50);
  }

#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Чи тягне лінію DO хтось ЗЗОВНІ (картка або зовнішній резистор)?
  //
  // Читати з INPUT_PULLUP тут БЕЗГЛУЗДО - внутрішній підтяг сам дає HIGH,
  // і тест проходить навіть на піні, до якого нічого не підключено (перша
  // версія проби саме так і брехала). Єдиний спосіб побачити ЗОВНІШНІЙ
  // підтяг - притиснути пін власним pull-down: якщо він усе одно читається
  // як HIGH, значить ззовні його тягне щось сильніше.
  //
  // Те саме стосується і роботи по SPI: spiAttachMISO() робить
  // pinMode(miso, INPUT) БЕЗ підтягу (esp32-hal-spi.c), тож непідключена
  // лінія плаває і читається як 0x00 - неотличимо від "картка зайнята".
  pinMode(SD_MISO, INPUT_PULLDOWN);
  delay(2);
  const bool pulledExternally = digitalRead(SD_MISO);
  pinMode(SD_MISO, INPUT_PULLUP);
  delay(2);
  Logger::info(
      "MISO=%d: external pull-up %s", SD_MISO,
      pulledExternally ? "PRESENT (card/resistor on the line)" : "MISSING - most likely nothing is on this pin");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

  // >=74 такти при CS=HIGH - обов'язковий перехід картки в SPI-режим.
  for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);

  digitalWrite(SD_CS, LOW);
  uint8_t r1 = sdProbeCmd(0, 0x00000000, 0x95);  // CMD0 GO_IDLE_STATE
  Logger::info("CMD0 (GO_IDLE_STATE) -> R1=0x%02X %s", r1,
               r1 == 0x01   ? "OK (card is idle)"
               : r1 == 0xFF ? "NO RESPONSE - card/pins/power"
               : r1 == 0xFE ? "the DO line stays LOW (not connected or the card is busy)"
               : r1 == 0x00 ? "answered, but not idle"
                            : "unexpected");

  // CMD8 має сенс за будь-якої валідної R1 (біт 7 скинутий), не лише 0x01.
  if (!(r1 & 0x80)) {
    uint8_t r8 = sdProbeCmd(8, 0x000001AA, 0x87);  // CMD8 SEND_IF_COND
    uint8_t echo[4] = {0};
    for (int i = 0; i < 4; ++i) echo[i] = SPI.transfer(0xFF);
    Logger::info("CMD8 (SEND_IF_COND) -> R1=0x%02X echo=%02X %02X %02X %02X %s", r8, echo[0], echo[1], echo[2], echo[3],
                 (r8 == 0x01 && echo[3] == 0xAA) ? "OK (SDHC/SDXC v2)"
                 : (r8 & 0x04)                   ? "illegal command (old SDSC v1)"
                                                 : "unexpected");
  }

  digitalWrite(SD_CS, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();

  Logger::info("------------------------------------------------------------");
  if (r1 == 0xFF) {
    Logger::info("VERDICT: the card does not respond at all - look for the cause in");
    Logger::info("  the pins (SD_CS/SD_SCK/SD_MOSI/SD_MISO in platformio.ini),");
    Logger::info("  the slot contacts or the 3V3 supply.");
  } else if (r1 == 0xFE) {
    Logger::info("VERDICT: the DO line always reads as 0. Two options:");
    Logger::info("  a) SD_MISO is the wrong pin / no card on it - if the line");
    Logger::info("     about the external pull-up above says MISSING, that is it;");
    Logger::info("  b) leftover busy after unmounting - a reboot helps then.");
    Logger::info("  Pick the pins automatically: sdscan");
  } else {
    Logger::info("VERDICT: bus and card are fine. If SD.begin() still");
    Logger::info("  fails, it is the filesystem: FAT32/FAT16 is required");
    Logger::info("  (the Arduino SD library mounts neither exFAT nor cards >32GB).");
  }
  if (wasMounted) {
    Logger::warn("The probe unmounted the card - run reboot to get SD back.");
  }
  Logger::info("============================================================");
}

// ---------------------------------------------------------------------------
// sdscan - автопідбір SD_MISO/SD_CS перебором (команда "sdscan").
//
// НАВІЩО: SD_SCK/SD_MOSI спільні з дисплеєм, тому вони вже підтверджені тим,
// що дисплей працює. А SD_MISO/SD_CS ніде більше не задіяні - якщо вони взяті
// з документації "схожої" плати і не збігаються з реальною розводкою, картка
// мовчить, і відрізнити це від несправної картки по логах SD неможливо.
// Скан перебирає пари (CS, MISO) при фіксованих SCK/MOSI і шукає ту, на якій
// CMD0 повертає 0x01.
//
// БЕЗПЕКА: лише ESP32-C6 (на інших чипах sdScan() відмовляє одразу).
// Перебираються лише GPIO зі списку нижче. Свідомо ВИКЛЮЧЕНІ піни,
// смикання яких зашкодило б: 12/13 (USB Serial/JTAG - вбило б консоль),
// 16/17 (UART0), 24..30 (шина SPI-флеша), 8/9 (strapping/boot). Піни, зайняті
// дисплеєм і самою шиною, відсіюються в рантаймі нижче.
static const uint8_t SD_SCAN_CANDIDATES[] = {0, 3, 4, 5, 6, 7, 10, 11, 18, 19, 20, 21};

static bool sdScanPinBusy(uint8_t pin) {
  if (pin == SD_SCK || pin == SD_MOSI) return true;
#if defined(TFT_CS) && (TFT_CS >= 0)
  if (pin == TFT_CS) return true;
#endif
#if defined(TFT_DC) && (TFT_DC >= 0)
  if (pin == TFT_DC) return true;
#endif
#if defined(TFT_RST) && (TFT_RST >= 0)
  if (pin == TFT_RST) return true;
#endif
#if defined(TFT_BL) && (TFT_BL >= 0)
  if (pin == TFT_BL) return true;
#endif
  return false;
}

// Одна спроба CMD0 на конкретній парі пінів. Таймаути тут навмисно короткі
// (5 мс): комбінацій понад сотня, а робоча пара відповідає одразу.
static uint8_t sdScanTry(uint8_t cs, uint8_t miso) {
  SPI.end();
  SPI.begin(SD_SCK, miso, SD_MOSI, cs);

  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);

  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);  // >=74 такти при CS=HIGH

  digitalWrite(cs, LOW);
  uint8_t r1 = sdProbeCmd(0, 0x00000000, 0x95, 5);
  digitalWrite(cs, HIGH);
  SPI.transfer(0xFF);
  SPI.endTransaction();

  return r1;
}

void sdScan() {
#if !CONFIG_IDF_TARGET_ESP32C6
  // Список SD_SCAN_CANDIDATES складено під розводку C6. На класичному ESP32
  // GPIO6..11 - лінії SPI-флеша модуля, на S3 GPIO19/20 - USB: смикати їх не
  // можна. Інший чип - окремий перелік, коли він справді знадобиться.
  Logger::warn("sdscan: the candidate pin list is ESP32-C6 only - refusing on this chip");
  return;
#endif
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD pin scan ======================================");

  if (SD.cardType() != CARD_NONE) {
    Logger::info("The card is already mounted on CS=%d MISO=%d - no scan needed.", SD_CS, SD_MISO);
    Logger::info("============================================================");
    return;
  }

  Logger::info("Fixed (shared with the display, hence already confirmed): SCK=%d MOSI=%d", SD_SCK, SD_MOSI);
  Logger::info("Current (being tested): CS=%d MISO=%d", SD_CS, SD_MISO);

  // Дисплей на тій самій шині - тримаємо його CS у HIGH на весь скан.
#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  const size_t n = sizeof(SD_SCAN_CANDIDATES);
  int found = 0;
  int tried = 0;

  for (size_t i = 0; i < n; ++i) {
    const uint8_t cs = SD_SCAN_CANDIDATES[i];
    if (sdScanPinBusy(cs)) continue;

    for (size_t j = 0; j < n; ++j) {
      const uint8_t miso = SD_SCAN_CANDIDATES[j];
      if (miso == cs || sdScanPinBusy(miso)) continue;

      ++tried;
      const uint8_t r1 = sdScanTry(cs, miso);

      if (r1 == 0x01) {
        ++found;
        Logger::info("FOUND: CS=%d MISO=%d -> CMD0 R1=0x01", cs, miso);
      } else if (!(r1 & 0x80)) {
        // Відповідь є, але не idle - теж вартий уваги кандидат.
        Logger::info("?  CS=%d MISO=%d -> CMD0 R1=0x%02X (answered, but not idle)", cs, miso, r1);
      }
    }
  }

  // Повертаємо шину на штатні піни, інакше дисплей лишиться на пінах з
  // останньої перебраної комбінації.
  SPI.end();
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  Logger::info("------------------------------------------------------------");
  Logger::info("Combinations tried: %d, found: %d", tried, found);
  if (found) {
    Logger::info("Put the found pair into platformio.ini (SD_CS/SD_MISO) and reflash.");
  } else {
    Logger::info("No pair responded. Most likely the card is not inserted,");
    Logger::info("  the slot is unpowered, or SD_SCK/SD_MOSI differ on this board,");
    Logger::info("  from the display pins (then a scan with fixed SCK/MOSI is useless).");
  }
  Logger::info("============================================================");
}

// ---------------------------------------------------------------------------
// sdbb - програмна (bit-bang) проба картки, команда "sdbb".
//
// НАВІЩО ще одна проба: sdprobe їде на апаратній SPI-периферії, тому його
// мовчання можна пояснити щонайменше трьома різними причинами - хибні піни,
// GPIO matrix/perimanager віддав пін іншій периферії, або картка справді не
// відповідає. Bit-bang не використовує SPI-периферію взагалі: тільки
// digitalWrite/digitalRead, тому лишає рівно одну можливу причину - залізо.
//
// Друкується СИРИЙ дамп прийнятих байтів, а не лише розібрана R1. Він
// однозначно розрізняє три стани лінії:
//   FF FF FF ... - лінія підтягнута, картка мовчить (немає/не живиться);
//   00 00 00 ... - лінія притиснута в нуль (немає підтягу або вічний busy);
//   будь-що інше - картка на лінії реагує, і далі вже видно як саме.
// ---------------------------------------------------------------------------
static uint8_t sdBitBangTransfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; --i) {
    digitalWrite(SD_MOSI, (out >> i) & 1);
    delayMicroseconds(5);
    digitalWrite(SD_SCK, HIGH);  // дані читаються по наростаючому фронту (SPI mode 0)
    delayMicroseconds(5);
    in = (uint8_t)((in << 1) | (digitalRead(SD_MISO) ? 1 : 0));
    digitalWrite(SD_SCK, LOW);
  }
  return in;
}

static void sdBitBangDump(const char* label, uint8_t* buf, int n) {
  char hex[3 * 12 + 1] = {0};
  int pos = 0;
  for (int i = 0; i < n && pos < (int)sizeof(hex) - 3; ++i) {
    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);
  }
  Logger::info("  %-22s %s", label, hex);
}

void sdBitBang() {
  YIELD_DISPLAY_BUS();

  Logger::info("========= SD bit-bang probe ================================");
  Logger::info("Pins: CS=%d SCK=%d MOSI=%d MISO=%d (no SPI peripheral)", SD_CS, SD_SCK, SD_MOSI, SD_MISO);

  const bool wasMounted = (SD.cardType() != CARD_NONE);
  if (wasMounted) {
    Logger::warn("The card is mounted - unmounting; a reboot will be needed after the probe");
    SD.end();
    delay(50);
  }

  // Забираємо піни в SPI-периферії, інакше pinMode/digitalWrite на них
  // конфліктують з GPIO matrix.
  SPI.end();

#if defined(TFT_CS) && (TFT_CS >= 0)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif

  pinMode(SD_SCK, OUTPUT);
  pinMode(SD_MOSI, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(SD_MISO, INPUT_PULLUP);
  digitalWrite(SD_SCK, LOW);
  digitalWrite(SD_MOSI, HIGH);
  digitalWrite(SD_CS, HIGH);
  delay(5);

  uint8_t buf[10];

  // 1. Лінія в спокої: CS=HIGH, женемо такти. Картка не вибрана, тому має
  //    віддавати суцільні 0xFF (лінія підтягнута).
  for (int i = 0; i < 10; ++i) buf[i] = sdBitBangTransfer(0xFF);
  sdBitBangDump("CS=HIGH, 80 clocks:", buf, 10);

  // 2. CMD0 при CS=LOW.
  digitalWrite(SD_CS, LOW);
  delayMicroseconds(50);
  sdBitBangTransfer(0xFF);
  sdBitBangTransfer(0x40);  // CMD0
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x00);
  sdBitBangTransfer(0x95);  // CRC для CMD0
  for (int i = 0; i < 10; ++i) buf[i] = sdBitBangTransfer(0xFF);
  sdBitBangDump("CMD0 response:", buf, 10);
  digitalWrite(SD_CS, HIGH);
  sdBitBangTransfer(0xFF);

  // 3. Чи керуються лінії взагалі: пишемо рівень і читаємо пін назад.
  //    Якщо пін не піддається - він відданий іншій периферії або
  //    закорочений на платі.
  auto checkDrive = [](const char* name, uint8_t pin) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(50);
    const bool low = digitalRead(pin);
    digitalWrite(pin, HIGH);
    delayMicroseconds(50);
    const bool high = digitalRead(pin);
    Logger::info("  %-5s (GPIO%-2d) drivable: %s", name, pin,
                 (!low && high) ? "YES" : "NO - the pin cannot be driven!");
  };
  checkDrive("SCK", SD_SCK);
  checkDrive("MOSI", SD_MOSI);
  checkDrive("CS", SD_CS);

  // Аналіз відповіді на CMD0.
  bool allFF = true, allZero = true, sawR1 = false;
  for (int i = 0; i < 10; ++i) {
    if (buf[i] != 0xFF) allFF = false;
    if (buf[i] != 0x00) allZero = false;
    if (buf[i] != 0xFF && !(buf[i] & 0x80)) sawR1 = true;
  }

  Logger::info("------------------------------------------------------------");
  if (sawR1 && wasMounted) {
    Logger::info("VERDICT: the card ANSWERED bit-bang, and hardware SPI had already");
    Logger::info("  mounted it. So hardware, pins and SPI are all fine -");
    Logger::info("  this probe diagnoses nothing here, it only unmounted the card.");
  } else if (sawR1) {
    Logger::info("VERDICT: the card ANSWERED bit-bang, but hardware SPI did not");
    Logger::info("  bring it up. Hardware and pins are fine - look into SPI");
    Logger::info("  (clock rate, bus shared with the display, CS state).");
  } else if (allFF) {
    Logger::info("VERDICT: all FF - the line is pulled up, but the card is silent.");
    Logger::info("  This is how an empty slot or an unpowered card behaves:");
    Logger::info("  check that the card is seated properly and 3V3 is on the slot.");
  } else if (allZero) {
    Logger::info("VERDICT: all 00 - something holds the line low. If MOSI/SCK/CS");
    Logger::info("  can be driven, then MISO is either the wrong pin or shorted.");
  } else {
    Logger::info("VERDICT: there is activity on the line, but it is not R1. Most likely");
    Logger::info("  a sync glitch - but the card is physically present.");
  }

  // Повертаємо шину апаратному SPI, інакше дисплей залишиться без неї.
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  if (wasMounted) {
    Logger::warn("The probe unmounted the card - run reboot to get SD back.");
  }
  Logger::info("============================================================");
}

void registerSdProbeCommands(SerialCommander& commander) {
  commander.registerCommand("sdprobe",
                            "low-level TF card probe over SPI: sdprobe [force] (force demounts the card until reboot)",
                            [](const String& args) { sdProbe(args.equalsIgnoreCase("force")); });

  commander.registerCommand("sdscan", "brute-force SD_CS/SD_MISO pins (SCK/MOSI kept fixed)",
                            [](const String& args) { sdScan(); });

  commander.registerCommand("sdbb", "bit-bang TF card probe (no SPI peripheral), raw byte dump",
                            [](const String& args) { sdBitBang(); });
}

#endif  // HAS_SD_WORKBENCH && !SD_USE_SDMMC
