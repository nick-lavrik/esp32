#include "MqttKeyGenerator.hpp"

MqttKeyGenerator::MqttKeyGenerator(const char* prefix) : _prefix(trimSlashes(prefix)) {}

void MqttKeyGenerator::setPrefix(const char* prefix) { _prefix = trimSlashes(prefix); }

const std::string& MqttKeyGenerator::prefix() const { return _prefix; }

std::string MqttKeyGenerator::key(const char* topic) const {
  std::string cleanTopic = trimSlashes(topic);

  if (_prefix.empty()) {
    return cleanTopic;
  }
  if (cleanTopic.empty()) {
    return _prefix;
  }

  return _prefix + "/" + cleanTopic;
}

std::string MqttKeyGenerator::trimSlashes(const char* value) {
  if (value == nullptr) {
    return std::string();
  }

  std::string result(value);

  size_t start = result.find_first_not_of('/');
  if (start == std::string::npos) {
    return std::string();
  }
  size_t end = result.find_last_not_of('/');

  return result.substr(start, end - start + 1);
}
