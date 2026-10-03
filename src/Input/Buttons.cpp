// Кнопки PRIMARY_BUTTON_PIN і SECONDARY_BUTTON_PIN (лише ttgo-t1): читання
// піна й пересилання подій активному екрану. Розпізнавання click /
// double-click / long press - lib/ButtonEvents, що вони означають - екран
// (Screen::onButton()).

#include <Arduino.h>

#include <ButtonEvents.hpp>
#include <TLogger.hpp>

#include "App/AppGlobals.hpp"
#include "Input.hpp"
#include "Screen/ScreenManager.hpp"

#if defined(PRIMARY_BUTTON_PIN)
namespace {

struct Button {
  ButtonId id;
  uint8_t pin;
  ButtonEvents events;
};

// Обидві кнопки замикають на GND (активний LOW).
Button buttons[] = {
    {ButtonId::Primary, PRIMARY_BUTTON_PIN, ButtonEvents()},
#if defined(SECONDARY_BUTTON_PIN)
    {ButtonId::Secondary, SECONDARY_BUTTON_PIN, ButtonEvents()},
#endif
};

// GPIO34-39 на класичному ESP32 - input-only, без внутрішньої підтяжки:
// INPUT_PULLUP там нічого не дає, рівень тримає зовнішній резистор плати.
uint8_t pinModeFor(uint8_t pin) {
#if defined(CONFIG_IDF_TARGET_ESP32)
  if (pin >= 34) return INPUT;
#endif
  return INPUT_PULLUP;
}

}  // namespace
#endif

void setupButtons() {
#if defined(PRIMARY_BUTTON_PIN)
  for (Button& b : buttons) {
    pinMode(b.pin, pinModeFor(b.pin));
    const ButtonId id = b.id;
    b.events.onEvent([id](ButtonEvent event, uint32_t nowMs, uint32_t heldMs) {
      // Локальний, а не static: глобальний TLogger коштує ~150 Б RAM.
      // %-9s - "secondary" (9); %-12s - "double-click" (12).
      const TLogger logger{"button"};
      logger.debug("%-9s %-12s held=%5u ms", buttonIdName(id), buttonEventName(event), (unsigned)heldMs);
      screens.active().onButton(id, event, nowMs, heldMs);
    });
  }
  // Одна cron-задача на всі кнопки, а не по задачі на пін: подія з будь-якої
  // кнопки обробляється в тому самому контексті (loop()), що й решта екрана.
  scheduler.addCronTask(0, []() -> void {
    const uint32_t now = millis();
    for (Button& b : buttons) b.events.update(digitalRead(b.pin) == LOW, now);
  });
  const TLogger logger{"button"};
#if defined(SECONDARY_BUTTON_PIN)
  logger.info("primary=GPIO%d secondary=GPIO%d", PRIMARY_BUTTON_PIN, SECONDARY_BUTTON_PIN);
#else
  logger.info("primary=GPIO%d", PRIMARY_BUTTON_PIN);
#endif
#endif
}
