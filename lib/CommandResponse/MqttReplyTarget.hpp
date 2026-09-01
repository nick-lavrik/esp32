#pragma once

// ResponseTarget, що публікує кожну порцію окремим MQTT-повідомленням у
// reply-топік. Топік передається БЕЗ префікса ("command/<client-id>/reply") -
// префікс підставляє сам MqttClient через MqttKeyGenerator::key()
// (MqttClient::resolveTopic()), як і для будь-якого іншого топіка.
//
// isFinal тут нічого не змінює: MQTT-підписник бачить порції одразу, по мірі
// надходження, і чекати кінця відповіді немає сенсу.

#include <MqttClient.hpp>

#if HAS_MQTT_CLIENT

#include <string>

#include "ResponseTarget.hpp"

class MqttReplyTarget : public ResponseTarget {
public:
  MqttReplyTarget(MqttClient& client, std::string topic) : _client(client), _topic(std::move(topic)) {}

  void deliver(const char* text, size_t length, bool isFinal) override {
    (void)isFinal;
    if (length == 0) {
      return;  // порожню фінальну порцію публікувати нема сенсу
    }
    // publish() у PicoMQTT-гілці асинхронний (кладе в _outgoingQueue) - тут
    // навмисно не чекаємо flushOutgoing(): виклик іде з sketch loop(), і
    // блокувати його на час мережевої відправки не можна.
    _client.publish(_topic.c_str(), text);
  }

private:
  MqttClient& _client;
  std::string _topic;
};

#endif  // HAS_MQTT_CLIENT
