#pragma once

#include <string>
#include <vector>

#include "EventListener.hpp"
#include "IEvent.hpp"

class IEventSubscriber;

// Інтерфейс диспетчера подій. Аналог
// Symfony\Contracts\EventDispatcher\EventDispatcherInterface.
//
// Реєстрація/видалення слухачів - динамічна і без жорстких лімітів,
// так само як у TaskController: кількість подій і слухачів на подію
// обмежена лише доступною пам'яттю.
class IEventDispatcher {
public:
  virtual ~IEventDispatcher() = default;

  // Реєструє слухача для конкретної події.
  // eventName - ім'я події (довільний рядок, напр. "wifi.connected").
  // priority  - чим вище число, тим раніше викликається слухач.
  // Повертає ListenerId для подальшого removeListener(),
  // або kInvalidListenerId, якщо listener порожній.
  virtual ListenerId addListener(const std::string& eventName, EventListener listener,
                                 int priority = 0) = 0;

  // Видаляє раніше зареєстрованого слухача за його ListenerId.
  // Безпечно викликати з неіснуючим id.
  virtual void removeListener(ListenerId id) = 0;

  // Реєструє всі слухачі підписника (підписник сам знає, на що підписатись).
  virtual void addSubscriber(IEventSubscriber& subscriber) = 0;

  // Знімає всі слухачі підписника.
  virtual void removeSubscriber(IEventSubscriber& subscriber) = 0;

  // Викликає всіх зареєстрованих слухачів для eventName у порядку
  // пріоритету, доки один із них не викличе event.stopPropagation().
  //
  // Подія створюється всередині і живе лише до кінця виклику, тому НІЧОГО не
  // повертає: раніше цей метод віддавав IEvent& на власну локальну змінну
  // (dangling reference - UB у будь-якого викликача, що подивився б на
  // результат). Якщо результат потрібен - створіть Event самі й скористайтесь
  // перевантаженням dispatch(IEvent&, eventName), яке повертає посилання на
  // ваш власний об'єкт.
  virtual void dispatch(const std::string& eventName) = 0;

  // Викликає всіх зареєстрованих слухачів для eventName у порядку
  // пріоритету, доки один із них не викличе event.stopPropagation().
  // Повертає посилання на той самий event (для зручного чейнінгу).
  virtual IEvent& dispatch(IEvent& event, const std::string& eventName) = 0;

  // Перевіряє наявність слухачів. Якщо eventName порожній - перевіряє,
  // чи є хоч якісь слухачі взагалі.
  virtual bool hasListeners(const std::string& eventName = "") const = 0;

  // Повертає ListenerId всіх слухачів конкретної події (діагностика/тести).
  virtual std::vector<ListenerId> getListenerIds(const std::string& eventName) const = 0;
};
