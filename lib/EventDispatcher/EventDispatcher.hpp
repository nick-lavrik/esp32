#pragma once

#include <map>
#include <string>
#include <vector>

#include "Event.hpp"
#include "IEventDispatcher.hpp"

// Динамічний диспетчер подій. Кількість імен подій і слухачів на кожну
// подію не обмежена наперед - сховище росте/зменшується через
// std::map/std::vector, аналогічно динамічній черзі завдань у
// TaskController.
//
// Не є потокобезпечним "з коробки". Якщо addListener()/removeListener()/
// dispatch() викликаються з різних FreeRTOS-тасків або з переривань -
// огорніть виклики власним м'ютексом/critical section на рівні використання.
class EventDispatcher : public IEventDispatcher {
public:
  EventDispatcher() = default;

  // Некопійований: ListenerId прив'язані до конкретного екземпляра диспетчера.
  EventDispatcher(const EventDispatcher&) = delete;
  EventDispatcher& operator=(const EventDispatcher&) = delete;

  ListenerId addListener(const std::string& eventName, EventListener listener,
                         int priority = 0) override;

  void removeListener(ListenerId id) override;

  void addSubscriber(IEventSubscriber& subscriber) override;
  void removeSubscriber(IEventSubscriber& subscriber) override;

  IEvent& dispatch(const std::string& eventName) override;
  IEvent& dispatch(IEvent& event, const std::string& eventName) override;

  bool hasListeners(const std::string& eventName = "") const override;

  std::vector<ListenerId> getListenerIds(const std::string& eventName) const override;

private:
  struct Entry {
    ListenerId id;
    int priority;
    EventListener listener;
  };

  static void sortByPriority(std::vector<Entry>& entries);

  std::map<std::string, std::vector<Entry>> _listenersByEvent;
  ListenerId _nextId = 1;
};
