#if defined(ESP32)

#include "SDImageServer.hpp"

#include <Arduino.h>
#include <TLogger.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const TLogger logger{"sdimg"};

// Читає один рядок запиту до CRLF. Повертає false при таймауті або обриві.
// Рядки довші за limit обрізаються (заголовки, які нас не цікавлять, можуть
// бути будь-якої довжини - але зберігати їх нема потреби).
bool readLine(WiFiClient &client, char *out, size_t limit, uint32_t timeoutMs) {
  size_t index = 0;
  const uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    if (!client.connected() && client.available() == 0) {
      return false;
    }

    if (client.available() == 0) {
      delay(1);
      continue;
    }

    const int byte = client.read();
    if (byte < 0) {
      continue;
    }

    if (byte == '\n') {
      // Відкидаємо '\r' перед '\n', якщо він потрапив у буфер.
      if (index > 0 && out[index - 1] == '\r') {
        --index;
      }
      out[index] = '\0';
      return true;
    }

    if (index < limit - 1) {
      out[index++] = static_cast<char>(byte);
    }
  }

  return false;
}

// Порівняння префікса заголовка без урахування регістру: назви заголовків
// у HTTP регістронезалежні, і qemu надсилає їх не так, як curl.
bool headerStartsWith(const char *line, const char *name) {
  return strncasecmp(line, name, strlen(name)) == 0;
}

// Пошук підрядка без урахування регістру. Своя реалізація, бо strcasestr() -
// GNU-розширення, якого в newlib на ESP32 немає.
bool containsIgnoreCase(const char *haystack, const char *needle) {
  const size_t needleLength = strlen(needle);

  for (const char *p = haystack; *p != '\0'; ++p) {
    if (strncasecmp(p, needle, needleLength) == 0) {
      return true;
    }
  }

  return false;
}


// Записує в сокет РІВНО length байт, скільки б часткових записів це не
// зайняло. Повертає false лише при справжньому обриві.
//
// НАВІЩО: WiFiClient::write() не зобов'язаний записати все, що дали - на
// слабкому каналі він регулярно віддає менше (буфер lwIP заповнений, вікно
// TCP закрите). Перша версія вважала це обривом і рвала з'єднання, через що
// паралельні запити curl завершувались CURLE_PARTIAL_FILE (18) з нулем
// прочитаних байт, а qemu отримував рвані відповіді замість даних.
bool writeAll(WiFiClient &client, const uint8_t *data, size_t length, uint32_t stallTimeoutMs) {
  size_t sent = 0;
  uint32_t lastProgressMs = millis();

  while (sent < length) {
    if (!client.connected()) {
      return false;
    }

    const size_t written = client.write(data + sent, length - sent);

    if (written > 0) {
      sent += written;
      lastProgressMs = millis();
      continue;
    }

    // Нуль записаних байт сам по собі не помилка: вікно TCP могло просто
    // закритися. Помилкою це стає, лише якщо прогресу немає надто довго.
    if (millis() - lastProgressMs > stallTimeoutMs) {
      return false;
    }

    delay(2);
  }

  return true;
}

}  // namespace

// WiFiServer отримує ліміт клієнтів явно: дефолтні 4 слоти в arduino-esp32
// нічого не ламають, але тримати його узгодженим з kMaxClients простіше, ніж
// потім згадувати, чому accept() перестає віддавати з'єднання.
SDImageServer::SDImageServer(const SDImageServerConfig &config)
    : _config(config), _server(config.port, kMaxClients) {}

bool SDImageServer::begin() {
  if (_isActive) {
    return true;
  }

  if (!_reader) {
    logger.error("sector reader not set");
    return false;
  }

  if (_totalSectors == 0) {
    logger.error("card size not set (0 sectors)");
    return false;
  }

  if (!WiFi.isConnected()) {
    logger.error("no WiFi - nowhere to start the server");
    return false;
  }

  _server.begin();
  _server.setNoDelay(true);
  _isActive = true;

  logger.info("сервер піднято: http://%s:%u/sd.img", WiFi.localIP().toString().c_str(),
              (unsigned)_config.port);
  logger.info("image size: %llu bytes (%llu sectors)",
              (unsigned long long)totalBytes(), (unsigned long long)_totalSectors);

  return true;
}

void SDImageServer::end() {
  if (!_isActive) {
    return;
  }

  for (WiFiClient &client : _clients) {
    if (client) {
      client.stop();
    }
  }

  _server.end();
  _isActive = false;

  logger.info("server stopped (served %llu bytes, requests %lu, bad sectors %lu)",
              (unsigned long long)_bytesServed, (unsigned long)_requestsServed,
              (unsigned long)_badSectors);
}

void SDImageServer::handleClient() {
  if (!_isActive) {
    return;
  }

  // 1. Прибираємо відпалі з'єднання і приймаємо новіі у вільні слоти.
  //    Порядок саме такий: спершу звільнити слот, потім accept() - інакше
  //    новий клієнт відкидався б, поки в масиві висить закритий сокет.
  for (WiFiClient &client : _clients) {
    if (client && !client.connected() && client.available() == 0) {
      client.stop();
    }
  }

  for (WiFiClient &slot : _clients) {
    if (slot && slot.connected()) {
      continue;
    }

    WiFiClient incoming = _server.accept();
    if (!incoming) {
      break;  // черга accept порожня
    }

    incoming.setNoDelay(true);
    logger.info("client %s connected", incoming.remoteIP().toString().c_str());
    slot = incoming;
  }

  // 2. Обслуговуємо по ОДНОМУ запиту з кожного слоту, у якому вже є дані.
  //    По одному, а не до кінця потоку запитів: інакше активний клієнт міг би
  //    монополізувати loop() і не дати вимкнути режим командою "sdimg off".
  for (WiFiClient &client : _clients) {
    if (client && client.available() > 0) {
      serveOne(client);
    }
  }
}

void SDImageServer::serveOne(WiFiClient &client) {
  const Request request = readRequest(client);

  if (!request.isValid) {
    client.stop();
    return;
  }

  ++_requestsServed;

  if (request.wantsStatus) {
    serveStatus(client);
  } else if (request.wantsZero) {
    serveZero(client, request);
  } else if (request.wantsImage) {
    serveImage(client, request);
  } else {
    sendSimpleResponse(client, "404 Not Found", "no such path; use /sd.img or /status\n");
  }

  if (!request.keepAlive) {
    client.stop();
  }
}

SDImageServer::Request SDImageServer::readRequest(WiFiClient &client) {
  Request request;
  char line[kRequestLineMax];

  // Перший рядок: "<METHOD> <PATH> HTTP/1.1"
  if (!readLine(client, line, sizeof(line), _config.keepAliveTimeoutSec * 1000UL)) {
    return request;
  }

  char method[8] = {};
  char path[64] = {};
  if (sscanf(line, "%7s %63s", method, path) != 2) {
    return request;
  }

  request.isHead = (strcasecmp(method, "HEAD") == 0);
  const bool isGet = (strcasecmp(method, "GET") == 0);

  if (!isGet && !request.isHead) {
    return request;  // POST/PUT/DELETE тут не мають сенсу: сервер лише читає
  }

  request.wantsImage = (strcmp(path, "/sd.img") == 0);
  request.wantsStatus = (strcmp(path, "/status") == 0);

  if (strncmp(path, "/zero", 5) == 0) {
    request.wantsZero = true;
    const char *lengthParam = strstr(path, "len=");
    request.zeroLength = (lengthParam != nullptr) ? strtoull(lengthParam + 4, nullptr, 10) : 1048576;
  }
  request.isValid = true;

  // Заголовки до порожнього рядка. Цікавлять лише Range і Connection.
  while (readLine(client, line, sizeof(line), 3000)) {
    if (line[0] == '\0') {
      break;
    }

    if (headerStartsWith(line, "Range:")) {
      // Приймаємо лише одиничний діапазон у байтах: "bytes=<start>-<end>",
      // причому end може бути відсутній ("до кінця файлу"). Multipart-range
      // (кілька діапазонів через кому) не підтримуємо - ні qemu-nbd, ні curl
      // його для блочного доступу не використовують.
      const char *value = strchr(line, '=');
      if (value != nullptr) {
        ++value;
        char *endPtr = nullptr;
        const uint64_t start = strtoull(value, &endPtr, 10);

        if (endPtr != nullptr && *endPtr == '-') {
          const char *tail = endPtr + 1;
          request.rangeStart = start;
          request.hasRange = true;

          if (*tail != '\0' && *tail != ',') {
            request.rangeEnd = strtoull(tail, nullptr, 10);
          } else {
            request.rangeEnd = (totalBytes() > 0) ? (totalBytes() - 1) : 0;
          }
        }
      }
    } else if (headerStartsWith(line, "Connection:")) {
      if (containsIgnoreCase(line, "close")) {
        request.keepAlive = false;
      }
    }
  }

  return request;
}

void SDImageServer::sendSimpleResponse(WiFiClient &client, const char *statusLine,
                                       const char *body) {
  char header[192];
  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: %u\r\n"
           "Connection: keep-alive\r\n"
           "\r\n",
           statusLine, (unsigned)strlen(body));

  client.print(header);
  client.print(body);
}

void SDImageServer::serveStatus(WiFiClient &client) {
  char body[256];
  snprintf(body, sizeof(body),
           "{\"totalBytes\":%llu,\"totalSectors\":%llu,\"bytesServed\":%llu,"
           "\"requests\":%lu,\"badSectors\":%lu,\"lastBadLba\":%lu}\n",
           (unsigned long long)totalBytes(), (unsigned long long)_totalSectors,
           (unsigned long long)_bytesServed, (unsigned long)_requestsServed,
           (unsigned long)_badSectors, (unsigned long)_lastBadLba);

  char header[192];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: %u\r\n"
           "Connection: keep-alive\r\n"
           "\r\n",
           (unsigned)strlen(body));

  client.print(header);
  client.print(body);
}

void SDImageServer::serveZero(WiFiClient &client, const Request &request) {
  char header[192];
  snprintf(header, sizeof(header),
           "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/octet-stream\r\n"
           "Content-Length: %llu\r\n"
           "Connection: keep-alive\r\n"
           "\r\n",
           (unsigned long long)request.zeroLength);
  client.print(header);

  if (request.isHead) {
    return;
  }

  const size_t bufferSize = _config.chunkSectors * kSectorSize;
  uint8_t *buffer = static_cast<uint8_t *>(calloc(1, bufferSize));
  if (buffer == nullptr) {
    client.stop();
    return;
  }

  // Той самий розмір блоку і той самий шлях у сокет, що й у sendRange() -
  // інакше порівняння з реальною передачею образу було б некоректним.
  uint64_t left = request.zeroLength;
  while (left > 0 && client.connected()) {
    const size_t toSend = (left < bufferSize) ? (size_t)left : bufferSize;
    if (!writeAll(client, buffer, toSend, kWriteStallTimeoutMs)) {
      break;
    }
    left -= toSend;
    yield();
  }

  free(buffer);
}

void SDImageServer::serveImage(WiFiClient &client, const Request &request) {
  const uint64_t total = totalBytes();

  uint64_t firstByte = 0;
  uint64_t lastByte = (total > 0) ? (total - 1) : 0;

  if (request.hasRange) {
    firstByte = request.rangeStart;
    lastByte = request.rangeEnd;

    if (firstByte >= total) {
      // 416 обов'язковий за RFC: без нього qemu вважатиме, що прочитав нулі
      // за межею образу, і зіпсує собі кеш.
      char header[160];
      snprintf(header, sizeof(header),
               "HTTP/1.1 416 Range Not Satisfiable\r\n"
               "Content-Range: bytes */%llu\r\n"
               "Content-Length: 0\r\n"
               "Connection: keep-alive\r\n"
               "\r\n",
               (unsigned long long)total);
      client.print(header);
      return;
    }

    if (lastByte >= total) {
      lastByte = total - 1;
    }
  }

  const uint64_t length = lastByte - firstByte + 1;

  // Accept-Ranges обов'язковий: qemu-nbd відмовиться працювати з ресурсом,
  // який не заявив підтримку діапазонів, навіть якщо вона фактично є.
  // Заголовок збирається послідовно, бо Content-Range існує лише у 206.
  char header[288];
  int pos = snprintf(header, sizeof(header),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: application/octet-stream\r\n"
                     "Accept-Ranges: bytes\r\n"
                     "Content-Length: %llu\r\n",
                     request.hasRange ? "206 Partial Content" : "200 OK",
                     (unsigned long long)length);

  if (request.hasRange) {
    pos += snprintf(header + pos, sizeof(header) - pos,
                    "Content-Range: bytes %llu-%llu/%llu\r\n",
                    (unsigned long long)firstByte, (unsigned long long)lastByte,
                    (unsigned long long)total);
  }

  snprintf(header + pos, sizeof(header) - pos, "Connection: %s\r\n\r\n",
           request.keepAlive ? "keep-alive" : "close");

  client.print(header);

  if (request.isHead) {
    return;  // HEAD - лише заголовки, тілом qemu тут не цікавиться
  }

  uint8_t *buffer = static_cast<uint8_t *>(malloc(_config.chunkSectors * kSectorSize));
  if (buffer == nullptr) {
    logger.error("no heap for %lu B buffer - dropping connection",
                 (unsigned long)(_config.chunkSectors * kSectorSize));
    client.stop();
    return;
  }

  const bool completed = sendRange(client, firstByte, lastByte, buffer);
  free(buffer);

  // Прогрес раз на 64 запити: знімання образу триває години, і без цих
  // рядків у логі неможливо відрізнити повільну роботу від зависання.
  // Логувати кожен запит не можна - сам Serial став би вузьким місцем.
  if ((_requestsServed % 64) == 0) {
    logger.info("request #%lu: offset %llu, total served %llu B, failures %lu",
                (unsigned long)_requestsServed, (unsigned long long)firstByte,
                (unsigned long long)_bytesServed, (unsigned long)_badSectors);
  }

  if (!completed) {
    // Обрив посеред передачі - штатна ситуація: qemu скасовує зайві
    // readahead-запити, закриваючи з'єднання.
    client.stop();
  }
}

bool SDImageServer::sendRange(WiFiClient &client, uint64_t firstByte, uint64_t lastByte,
                              uint8_t *buffer) {
  uint64_t position = firstByte;

  while (position <= lastByte) {
    // Range задається в байтах, а картка читається секторами, тому кожен
    // чанк вирівнюємо на межу сектора і віддаємо лише потрібний зріз.
    const uint32_t lba = static_cast<uint32_t>(position / kSectorSize);
    const uint32_t offsetInSector = static_cast<uint32_t>(position % kSectorSize);

    const uint64_t bytesLeft = lastByte - position + 1;
    const uint32_t sectorsNeeded =
        static_cast<uint32_t>((offsetInSector + bytesLeft + kSectorSize - 1) / kSectorSize);
    const uint32_t chunk =
        (sectorsNeeded < _config.chunkSectors) ? sectorsNeeded : _config.chunkSectors;

    if (!_reader(lba, chunk, buffer)) {
      // Пачка не прочиталась - переходимо на посекторне читання, щоб
      // врятувати сусідів справді битого сектора. Саме для цього випадку
      // ця гілка й існує: на картці, що вмирає, збій рідко покриває всі
      // 64 сектори пачки.
      for (uint32_t i = 0; i < chunk; ++i) {
        uint8_t *target = buffer + i * kSectorSize;

        if (!_reader(lba + i, 1, target)) {
          // Нулі замість даних, як робить ddrescue: обірвати передачу було б
          // гірше - хост втратив би і решту образу. Позиції збоїв ідуть у лог
          // і в /status, щоб потім знати, які файли постраждали.
          memset(target, 0, kSectorSize);
          ++_badSectors;
          _lastBadLba = lba + i;
          logger.warn("bad sector LBA %lu -> serving zeros", (unsigned long)(lba + i));
        }
      }
    }

    const uint32_t available = chunk * kSectorSize - offsetInSector;
    const uint32_t toSend =
        (bytesLeft < static_cast<uint64_t>(available)) ? static_cast<uint32_t>(bytesLeft) : available;

    if (!client.connected()) {
      return false;
    }

    if (!writeAll(client, buffer + offsetInSector, toSend, kWriteStallTimeoutMs)) {
      return false;  // сокет справді відпав
    }

    _bytesServed += toSend;
    position += toSend;

    // Віддаємо процесор: інакше довга передача не дає крутитись ні
    // WiFi-таску, ні watchdog-у.
    yield();
  }

  return true;
}

#endif  // ESP32
