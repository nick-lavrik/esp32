#pragma once

// ResponseTarget, що збирає всю відповідь і на isFinal шле її одним листом.
//
// Навіщо окремо від MqttReplyTarget: лист має бути один, тому порції тут не
// доставляються по мірі надходження, а накопичуються. Це головна причина, чому
// ResponseTarget::deliver() має аргумент isFinal.
//
// Задумано насамперед для задач, поставлених через
// TaskController::addCronTask(): cron-лямбда захоплює shared_ptr на такий
// приймач по значенню, тож він живе довше за запит, який його створив.
//
// ВАЖЛИВО (C6): SMTP-сесія не піднімається поверх живого MQTT-over-TLS - дві
// TLS-сесії не влазять у heap (див. MqttClient.hpp, коментар до suspend()).
// Тому відправка обгортається в MqttClient::suspend()/resume() - тим самим
// патерном, що EcoflowClient::withMqttSuspended(). Якщо suspend() не вдався,
// лист НЕ надсилається: краще втратити відповідь, ніж покласти mbedTLS.

#include <GmailSender.hpp>

#if HAS_GMAIL_SENDER

#include <Arduino.h>
#include <MqttClient.hpp>

#include <TLogger.hpp>
#include <utility>

#include "ResponseTarget.hpp"

#if !HAS_MQTT_CLIENT
// Плати без MQTT: клас не оголошений, але конструктор має приймати nullptr -
// для вказівника достатньо incomplete type (розіменовується він лише під
// #if HAS_MQTT_CLIENT нижче).
class MqttClient;
#endif

class EmailTarget : public ResponseTarget {
public:
  // mqtt - клієнт, який треба приспати на час SMTP-сесії; nullptr, якщо
  // MQTT на цій платі немає або пауза не потрібна.
  EmailTarget(GmailSender& mailer, String recipient, String subject, MqttClient* mqtt = nullptr)
      : _mailer(mailer), _recipient(std::move(recipient)), _subject(std::move(subject)), _mqtt(mqtt) {}

  void deliver(const char* text, size_t length, bool isFinal) override {
    (void)length;  // text null-terminated - length тут не потрібна
    if (text != nullptr) {
      _body += text;
    }
    if (!isFinal) {
      return;
    }

    if (_body.length() == 0) {
      _logger.warn("nothing to send to %s", _recipient.c_str());
      return;
    }

#if HAS_MQTT_CLIENT
    const bool needSuspend = (_mqtt != nullptr) && !_mqtt->isSuspended();
    if (needSuspend) {
      if (!_mqtt->suspend()) {
        _logger.error("failed to suspend MQTT - email skipped");
        _body = "";
        return;
      }
      // Heap тут НЕ логуємо: GmailSender::sendEmail() друкує його сам, уже
      // безпосередньо перед конектом - тобто в точці, де mbedTLS і просить
      // суцільний блок. Два рядки про одне й те саме лише збивали б з пантелику
      // (а ESP.getMaxAllocHeap() ще й не існує на ESP8266).
      _logger.debug("MQTT suspended for SMTP session");
    }
#endif

    const bool ok = _mailer.sendEmail(_recipient.c_str(), _subject.c_str(), _body.c_str());
    if (!ok) {
      _logger.error("send to %s failed", _recipient.c_str());
    }

#if HAS_MQTT_CLIENT
    if (needSuspend) {
      _mqtt->resume();
    }
#endif

    _body = "";
  }

private:
  GmailSender& _mailer;
  String _recipient;
  String _subject;
  MqttClient* _mqtt;
  String _body;

  const TLogger _logger{"reply.mail"};
};

#endif  // HAS_GMAIL_SENDER
