#pragma once

#include <string>
#include <vector>

class IEventDispatcher;

// Один запис із переліку подій, на які підписується IEventSubscriber.
// Носить довідковий характер (для логів/діагностики) - фактичну
// прив'язку робить сам метод subscribe().
struct SubscribedEvent {
  std::string eventName;
  int priority = 0;
};

// Аналог Symfony\Component\EventDispatcher\EventSubscriberInterface: клас,
// який хоче слухати одразу декілька подій, оголошує їх через
// getSubscribedEvents() і сам вміє прикріпитись/відкріпитись від
// диспетчера, зберігаючи власні ListenerId всередині себе.
//
// Приклад:
//   class WifiLogger : public IEventSubscriber {
//   public:
//       std::vector<SubscribedEvent> getSubscribedEvents() const override {
//           return { {"wifi.connected", 0}, {"wifi.disconnected", 0} };
//       }
//       void subscribe(IEventDispatcher& d) override {
//           _connectedId = d.addListener("wifi.connected", [this](IEvent& e){ onConnected(e); });
//           _disconnectedId = d.addListener("wifi.disconnected", [this](IEvent& e){
//           onDisconnected(e); });
//       }
//       void unsubscribe(IEventDispatcher& d) override {
//           d.removeListener(_connectedId);
//           d.removeListener(_disconnectedId);
//       }
//   private:
//       ListenerId _connectedId = kInvalidListenerId;
//       ListenerId _disconnectedId = kInvalidListenerId;
//       void onConnected(IEvent&) { ... }
//       void onDisconnected(IEvent&) { ... }
//   };
class IEventSubscriber {
public:
  virtual ~IEventSubscriber() = default;

  // Перелік подій, які цей підписник слухає (для інтроспекції).
  virtual std::vector<SubscribedEvent> getSubscribedEvents() const = 0;

  // Реєструє слухачі цього підписника на переданому диспетчері.
  virtual void subscribe(IEventDispatcher& dispatcher) = 0;

  // Знімає слухачі цього підписника з переданого диспетчера.
  virtual void unsubscribe(IEventDispatcher& dispatcher) = 0;
};
