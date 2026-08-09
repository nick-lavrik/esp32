#pragma once

#include <string>

// Генерує MQTT-топіки з префіксом (dev/prod/qa/local, регіон, тенант, тощо - будь-який
// топ-рівневий розділ каналів на одному брокері).
//
// Формат: "{prefix}/{topic}" (prefix - окремий топ-рівень).
// Порожній prefix -> topic повертається без змін (без зайвого "/").
//
// Використання:
//   MqttKeyGenerator keyGen("dev");
//   keyGen.key("devices/esp32-st7789/status");
//   // -> "dev/devices/esp32-st7789/status"
//
//   keyGen.setPrefix("prod");
//   keyGen.key("#");
//   // -> "prod/#"  (wildcard-топіки '+' / '#' обробляються так само,
//   //    як звичайний topic - генератор лише додає top-level сегмент)
class MqttKeyGenerator {
public:
  explicit MqttKeyGenerator(const char* prefix = "");

  void setPrefix(const char* prefix);
  const std::string& prefix() const;

  // topic == nullptr трактується як порожній рядок.
  std::string key(const char* topic) const;

private:
  static std::string trimSlashes(const char* value);

  std::string _prefix;
};
