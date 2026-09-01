#pragma once

// Куди віддавати результат виконаної команди. Абстракція навмисно вужча за
// Print: доставка йде порціями готового тексту, і приймач має знати, коли
// порція остання - MQTT публікує кожну окремим повідомленням, а email збирає
// всі й шле одним листом уже на isFinal.
//
// Реалізації: MqttReplyTarget (топік <prefix>/command/<env>/reply),
// EmailTarget (GmailSender).
//
// Тримається через std::shared_ptr (див. CommandResponse) - щоб приймач міг
// пережити сам запит: cron-задача, поставлена командою через
// TaskController::addCronTask(), захоплює його по значенню й віддає
// результат при кожному спрацюванні.

#include <cstddef>

class ResponseTarget {
public:
  virtual ~ResponseTarget() = default;

  // text - null-terminated, length - його довжина без '\0'.
  // isFinal == true рівно в останньому виклику для цієї відповіді
  // (у т.ч. якщо тексту в останній порції немає, length == 0).
  virtual void deliver(const char* text, size_t length, bool isFinal) = 0;
};
