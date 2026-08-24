#pragma once

#if defined(ESP32)

#include <WiFi.h>

#include <cstdint>
#include <functional>

// HTTP-сервер, який віддає SD картку як один великий файл сирих байтів.
//
// НАВІЩО: коли картку не бачить хост-комп'ютер, а ESP32 її читає, плата
// лишається єдиним каналом доступу до даних. Віддавши картку як HTTP-ресурс
// з підтримкою Range-запитів, ми дозволяємо хосту працювати з нею штатними
// інструментами - без жодного драйвера на боці ESP32:
//
//   sudo modprobe nbd
//   sudo qemu-nbd --read-only --connect=/dev/nbd0 \
//     'json:{"driver":"raw","file":{"driver":"http","url":"http://<ip>:8080/sd.img"}}'
//   sudo mount -o ro,noload /dev/nbd0p2 /mnt/sd
//
// Далі ext4 розбирає ядро Linux, а не ESP32 - тобто список файлів видно без
// копіювання всієї картки: ядро витягне лише метадані.
//
// ЧОМУ СИНХРОННИЙ СЕРВЕР, А НЕ ESPAsyncWebServer (він у проєкті вже є):
// картка і дисплей висять на СПІЛЬНІЙ SPI-шині, а читання 32 KiB блокує її
// на ~23 мс. З AsyncTCP-callback це означало б або конфлікт транзакцій з
// дисплеєм, або блокування tcp-таска на десятки мс. Тут handleClient()
// викликається з loop() у режимі, де дисплей навмисно не малюється
// (див. isActive() і його використання в main.cpp), тому шина належить
// картці монопольно, а порядок доступу гарантований самим потоком коду.
//
// БЕЗПЕКА: сервер віддає ВСЮ картку будь-кому в локальній мережі і не має
// автентифікації. Це інструмент разового порятунку даних, який вмикається
// вручну командою і сам вимикається (див. begin()/end()); тримати його
// піднятим постійно не варто.
// Конфігурація сервера. Оголошена ПОЗА класом - так само, як
// HttpServerConfig у lib/HttpServer: тип, використаний у default-аргументі
// конструктора, мусить бути повним на момент оголошення цього конструктора,
// а вкладений у той самий клас таким ще не є.
struct SDImageServerConfig {
  uint16_t port = 8080;
  // 64 сектори = 32 KiB. Виміряно на esp32-c6 ("sdbench"): 1 сектор -
  // 430 KiB/s, 8 - 1116, 32 - 1348, 64 - 1383 KiB/s, тобто далі плато.
  // 128 секторів вже не влазять у найбільший вільний блок heap (~54 KiB).
  uint32_t chunkSectors = 64;
  // Скільки секунд тримати відкрите з'єднання без запиту, перш ніж закрити.
  uint32_t keepAliveTimeoutSec = 15;
};


class SDImageServer {
public:
  // Читач секторів: (перший LBA, скільки секторів, куди) -> успіх.
  // Реалізацію постачає main.cpp (SdSpiBulkReader), щоб сервер не залежав
  // ні від SD.h, ні від SD_MMC.h.
  using SectorReader = std::function<bool(uint32_t lba, uint32_t count, uint8_t *out)>;


  explicit SDImageServer(const SDImageServerConfig &config = SDImageServerConfig());

  // Заборона копіювання - власник WiFiServer і клієнтського з'єднання.
  SDImageServer(const SDImageServer &) = delete;
  SDImageServer &operator=(const SDImageServer &) = delete;

  void setSectorReader(SectorReader reader) { _reader = reader; }

  // totalSectors - повний розмір картки в секторах (SD.numSectors()).
  // Саме він стає Content-Length відповіді.
  void setTotalSectors(uint64_t totalSectors) { _totalSectors = totalSectors; }

  // Піднімає сервер. Повертає false, якщо читач не заданий, розмір нульовий
  // або немає WiFi - без будь-якого з цих трьох сервер безсенсовний.
  bool begin();
  void end();

  bool isActive() const { return _isActive; }

  // Викликати з loop(). Приймає нові з'єднання і обслуговує по одному
  // запиту з кожного, у якого є дані.
  //
  // ЧОМУ ПУЛ, А НЕ ОДИН КЛІЄНТ: перша версія тримала рівно одне з'єднання,
  // бо "qemu-nbd все одно читає послідовно". Це виявилось неправдою і давало
  // глухий дедлок: libcurl усередині qemu відкриває ДРУГЕ з'єднання для
  // наступного readahead-запиту, і поки перше живе (keep-alive), сервер
  // друге не приймав - qemu чекав відповіді, сервер чекав даних у першому
  // з'єднанні, а "mount" висів намертво.
  void handleClient();

  uint64_t bytesServed() const { return _bytesServed; }
  uint32_t requestsServed() const { return _requestsServed; }
  uint32_t badSectors() const { return _badSectors; }
  uint32_t lastBadLba() const { return _lastBadLba; }
  uint64_t totalBytes() const { return _totalSectors * kSectorSize; }

private:
  static constexpr uint32_t kSectorSize = 512;
  static constexpr size_t kRequestLineMax = 256;
  // Скільки одночасних з'єднань тримаємо. libcurl у qemu зазвичай обходиться
  // двома, але запас потрібен: паралельно можуть стукати і /status, і
  // діагностичний curl з іншої машини.
  static constexpr size_t kMaxClients = 8;
  // Скільки чекати прогресу в сокеті, перш ніж визнати з'єднання мертвим.
  // 20 с з великим запасом перекривають затори на слабкому каналі, але не
  // дають зависнути назавжди, якщо клієнт зник без FIN.
  static constexpr uint32_t kWriteStallTimeoutMs = 20000;

  // Розібраний запит. Байтові межі - включно з обома кінцями, як у HTTP.
  struct Request {
    bool isValid = false;
    bool isHead = false;      // HEAD - qemu питає ним розмір перед читанням
    bool wantsImage = false;  // шлях /sd.img
    bool wantsStatus = false;  // шлях /status
    // /zero?len=N - віддає N байт нулів, картку не читає. Потрібен, щоб
    // відокремити пропускну здатність радіо від швидкості самої картки:
    // без цього поділу неясно, що саме обмежує передачу образу.
    bool wantsZero = false;
    uint64_t zeroLength = 0;
    bool hasRange = false;
    bool keepAlive = true;
    uint64_t rangeStart = 0;
    uint64_t rangeEnd = 0;
  };

  // Обслуговує рівно один запит зі вже підключеного клієнта.
  void serveOne(WiFiClient &client);

  Request readRequest(WiFiClient &client);
  void serveImage(WiFiClient &client, const Request &request);
  void serveStatus(WiFiClient &client);
  void serveZero(WiFiClient &client, const Request &request);
  void sendSimpleResponse(WiFiClient &client, const char *statusLine, const char *body);

  // Передає діапазон байтів картки в сокет. Повертає false, якщо клієнт
  // відпав посеред передачі (звичайна річ: qemu обриває readahead-запити).
  bool sendRange(WiFiClient &client, uint64_t firstByte, uint64_t lastByte, uint8_t *buffer);

  SDImageServerConfig _config;
  WiFiServer _server;
  WiFiClient _clients[kMaxClients];
  SectorReader _reader;
  uint64_t _totalSectors = 0;
  bool _isActive = false;

  uint64_t _bytesServed = 0;
  uint32_t _requestsServed = 0;
  uint32_t _badSectors = 0;
  uint32_t _lastBadLba = 0;
};

#endif  // ESP32
