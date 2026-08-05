#include "Process.hpp"

#include <algorithm>

namespace {

/// Спінлок для захисту реєстру кооперативних процесів (безпечно між ядрами ESP32).
portMUX_TYPE& registryLock() {
  static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  return lock;
}

}  // namespace

std::vector<std::shared_ptr<Process::AppPumpEntry>>& Process::registry() {
  static std::vector<std::shared_ptr<AppPumpEntry>> instance;
  return instance;
}

void Process::registerAppPump(std::function<bool()> pump) {
  auto entry = std::make_shared<AppPumpEntry>();
  entry->pump = std::move(pump);

  portENTER_CRITICAL(&registryLock());
  registry().push_back(entry);
  portEXIT_CRITICAL(&registryLock());
}

void Process::update() {
  // 1) Знімок поточних записів під коротким спінлоком (копіюються лише shared_ptr - дешево).
  std::vector<std::shared_ptr<AppPumpEntry>> snapshot;
  portENTER_CRITICAL(&registryLock());
  snapshot = registry();
  portEXIT_CRITICAL(&registryLock());

  // 2) Прокачуємо кожен процес ПОЗА критичною секцією - користувацький код
  //    (update-функція) може виконуватись довільний час, тримати спінлок тут не можна.
  for (auto& entry : snapshot) {
    if (entry->done.load(std::memory_order_acquire)) continue;
    if (entry->pump()) {
      entry->done.store(true, std::memory_order_release);
    }
  }

  // 3) Прибираємо завершені записи з реєстру.
  portENTER_CRITICAL(&registryLock());
  auto& reg = registry();
  reg.erase(std::remove_if(reg.begin(), reg.end(),
                           [](const std::shared_ptr<AppPumpEntry>& e) {
                             return e->done.load(std::memory_order_acquire);
                           }),
            reg.end());
  portEXIT_CRITICAL(&registryLock());
}

size_t Process::activeAppCount() {
  portENTER_CRITICAL(&registryLock());
  size_t n = registry().size();
  portEXIT_CRITICAL(&registryLock());
  return n;
}
