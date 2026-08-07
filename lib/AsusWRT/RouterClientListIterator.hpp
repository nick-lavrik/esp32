#pragma once

#include <vector>

#include "RouterClientInfo.hpp"

// Явний ітератор над вже готовим std::vector<RouterClientInfo> (hasNext()/next()).
// Не є "справжнім" стрімінгом з мережі — RouterClientListParser::parse() вже повністю
// розпарсив список у пам'яті. Цей клас лише дає зручний послідовний API для виклику
// зі SerialCommander замість прямого індексування vector.
//
// Володіє vector'ом за значенням (move) — пам'ять звільняється автоматично
// при виході з області видимості ітератора.
//
// Приклад використання:
//   RouterClientListIterator it(std::move(clients));
//   while (it.hasNext()) {
//     const RouterClientInfo& c = it.next();
//     Serial.println(c.name);
//   }
class RouterClientListIterator {
public:
  RouterClientListIterator() : _index(0) {}
  explicit RouterClientListIterator(std::vector<RouterClientInfo>&& clients)
      : _clients(std::move(clients)), _index(0) {}

  // Заповнює ітератор новим набором клієнтів ззовні, індекс скидається на 0.
  void assign(std::vector<RouterClientInfo>&& clients) {
    _clients = std::move(clients);
    _index = 0;
  }

  bool hasNext() const { return _index < _clients.size(); }

  // Викликати лише якщо hasNext() == true. Посилання дійсне до наступного next()
  // або до знищення ітератора.
  const RouterClientInfo& next() { return _clients[_index++]; }

  size_t remaining() const { return _clients.size() - _index; }
  size_t total() const { return _clients.size(); }

  void reset() { _index = 0; }

private:
  std::vector<RouterClientInfo> _clients;
  size_t _index;
};
