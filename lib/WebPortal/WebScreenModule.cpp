#include "WebScreenModule.hpp"

#include "WebJson.hpp"
#include "WebPortal.hpp"

namespace {

// Назва формату для заголовка відповіді. Порядок байтів RGB565 у спрайті
// залежить від бекенда (LovyanGFX тримає swap565), і замість того щоб
// перевертати байти на платі, ми просто кажемо браузеру, які вони: для
// DataView.getUint16() це один булевий аргумент.
const char* formatName(ScreenMirror::Format format, bool swapped) {
  if (format == ScreenMirror::Format::Rgb332) return "332";
  return swapped ? "565be" : "565le";
}

}  // namespace

void WebScreenModule::registerRoutes(AsyncWebServer& server, WebPortal& portal) {
  (void)portal;

  // Геометрія екрана. Сторінка бере звідси розмір <canvas> і висоту смуги,
  // тобто зсув, з яким малювати кожен шматок.
  server.on("/api/screen/info", HTTP_GET, [](AsyncWebServerRequest* request) {
    const ScreenMirror& mirror = ScreenMirror::instance();
    String json = String("{\"available\":") + webjson::boolean(mirror.available());
    json += ",\"width\":" + String(mirror.width());
    json += ",\"height\":" + String(mirror.height());
    json += ",\"splitCount\":" + String(mirror.splitCount());
    json += ",\"splitHeight\":" + String(mirror.splitHeight());
    json += ",\"swapped565\":" + webjson::boolean(mirror.swapped565());
    json += "}";
    request->send(200, "application/json", json);
  });

  // Одна смуга сирими байтами. Метадані - заголовками, а не в тілі: так тіло
  // лишається рівно буфером пікселів, який браузер кладе в ImageData без
  // розбору й зайвих копій.
  server.on("/api/screen/strip", HTTP_GET, [](AsyncWebServerRequest* request) {
    ScreenMirror& mirror = ScreenMirror::instance();
    if (!mirror.available()) {
      request->send(503, "application/json", webjson::error("No frame buffer on this board"));
      return;
    }

    ScreenMirror::Format format = ScreenMirror::Format::Rgb332;
    if (request->hasParam("fmt") && request->getParam("fmt")->value() == "565") {
      format = ScreenMirror::Format::Rgb565;
    }

    ScreenMirror::Snapshot snap;
    if (!mirror.take(format, snap)) {
      // Знімка ще немає - запит уже поставлено, смуга зніметься найближчим
      // кадром. 204 замість помилки: для клієнта це не збій, а "прийди
      // наступним тіком", і backoff у poller() тут спрацьовувати не повинен.
      request->send(204);
      return;
    }

    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/octet-stream", [snap](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
          const size_t left = index < snap.size ? snap.size - index : 0;
          if (left == 0) {
            // Останній шматок пішов - буфер вільний, і дзеркало одразу просить
            // наступну смугу, щоб вона була готова ДО наступного запиту.
            ScreenMirror::instance().release(snap.frame);
            return 0;
          }
          if (maxLen == 0) return RESPONSE_TRY_AGAIN;
          const size_t chunk = left < maxLen ? left : maxLen;
          memcpy(buffer, snap.data + index, chunk);
          return chunk;
        });

    // Обірване з'єднання - теж кінець віддачі. Без цього буфер тримався б до
    // таймауту tick(), а клієнт усі ці секунди отримував би 204.
    request->onDisconnect([frame = snap.frame]() { ScreenMirror::instance().release(frame); });

    response->addHeader("X-Screen-Index", String(snap.index));
    response->addHeader("X-Screen-Count", String(snap.splitCount));
    response->addHeader("X-Screen-Frame", String(snap.frame));
    response->addHeader("X-Screen-Width", String(snap.width));
    response->addHeader("X-Screen-Height", String(snap.height));
    response->addHeader("X-Screen-Format", formatName(snap.format, mirror.swapped565()));
    // Кадр живе рівно один запит - кеш зіпсував би саме те, заради чого все.
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
}
