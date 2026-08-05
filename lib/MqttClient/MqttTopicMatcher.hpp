#pragma once

// Порівнює topic filter (може містити '+' та '#') проти конкретного topic.
// '+' - один рівень, '#' - решта рівнів (лише в кінці filter).
// "sport/#" також матчить "sport" (рівень перед '#' необов'язковий) - за специфікацією MQTT.
class MqttTopicMatcher {
public:
  static bool match(const char* filter, const char* topic) {
    if (filter == nullptr || topic == nullptr) {
      return false;
    }

    const char* f = filter;
    const char* t = topic;

    while (true) {
      if (*f == '#') {
        return true;
      }

      if (*f == '+') {
        while (*t != '\0' && *t != '/') {
          ++t;
        }
        ++f;
      } else {
        while (*f != '\0' && *f != '/') {
          if (*t == '\0' || *t != *f) {
            return false;
          }
          ++f;
          ++t;
        }
      }

      if (*f == '\0') {
        return *t == '\0';
      }

      if (*t == '\0') {
        if (f[1] == '#' && f[2] == '\0') {
          return true;
        }
        return false;
      }

      if (*t != '/') {
        return false;
      }

      ++f;
      ++t;
    }
  }
};
