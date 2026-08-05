#include "EventDispatcher.hpp"

#include <algorithm>
#include <utility>

#include "IEventSubscriber.hpp"

ListenerId EventDispatcher::addListener(const std::string& eventName, EventListener listener,
                                        int priority) {
  if (!listener) {
    return kInvalidListenerId;
  }

  const ListenerId id = _nextId++;
  auto& entries = _listenersByEvent[eventName];
  entries.push_back(Entry{id, priority, std::move(listener)});
  sortByPriority(entries);
  return id;
}

void EventDispatcher::removeListener(ListenerId id) {
  for (auto it = _listenersByEvent.begin(); it != _listenersByEvent.end();) {
    auto& entries = it->second;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [id](const Entry& entry) { return entry.id == id; }),
                  entries.end());

    if (entries.empty()) {
      it = _listenersByEvent.erase(it);
    } else {
      ++it;
    }
  }
}

void EventDispatcher::addSubscriber(IEventSubscriber& subscriber) { subscriber.subscribe(*this); }

void EventDispatcher::removeSubscriber(IEventSubscriber& subscriber) {
  subscriber.unsubscribe(*this);
}
IEvent& EventDispatcher::dispatch(const std::string& eventName) {
  Event e;
  return dispatch(e, eventName);
}

IEvent& EventDispatcher::dispatch(IEvent& event, const std::string& eventName) {
  auto it = _listenersByEvent.find(eventName);
  if (it == _listenersByEvent.end()) {
    return event;
  }

  // Копіюємо список слухачів, щоб слухач міг сам додати/видалити
  // слухачів (в т.ч. самого себе) під час dispatch(), не зламавши ітерацію.
  std::vector<Entry> entries = it->second;

  for (const auto& entry : entries) {
    if (event.isPropagationStopped()) {
      break;
    }
    if (entry.listener) {
      entry.listener(event);
    }
  }

  return event;
}

bool EventDispatcher::hasListeners(const std::string& eventName) const {
  if (eventName.empty()) {
    return !_listenersByEvent.empty();
  }
  auto it = _listenersByEvent.find(eventName);
  return it != _listenersByEvent.end() && !it->second.empty();
}

std::vector<ListenerId> EventDispatcher::getListenerIds(const std::string& eventName) const {
  std::vector<ListenerId> ids;
  auto it = _listenersByEvent.find(eventName);
  if (it != _listenersByEvent.end()) {
    ids.reserve(it->second.size());
    for (const auto& entry : it->second) {
      ids.push_back(entry.id);
    }
  }
  return ids;
}

void EventDispatcher::sortByPriority(std::vector<Entry>& entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& a, const Entry& b) { return a.priority > b.priority; });
}
