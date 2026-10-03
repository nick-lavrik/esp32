#include "Battery.hpp"

#include <Logger.hpp>

#if BOARD_HAS_BATTERY_ADC
namespace {

// Усереднення гасить шум АЦП C6 (±десятки мВ на окремому вимірі); 16 вимірів
// по ~20 мкс - непомітно навіть у таску команд.
constexpr uint8_t kSamples = 16;

// analogReadMilliVolts(), а не analogRead(): ядро застосовує заводську
// калібровку eFuse, сирий код АЦП на C6 нелінійний і зміщений.
uint32_t adcMilliVolts() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kSamples; ++i) {
    sum += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  return sum / kSamples;
}

}  // namespace
#endif

void registerBatteryCommands(SerialCommander& commander) {
#if BOARD_HAS_BATTERY_ADC
  commander.registerCommand("battery", "battery voltage via BAT_ADC divider", [](const String&) {
    const uint32_t adc = adcMilliVolts();
    Logger::info("battery: %lu mV (adc %lu mV x%d, GPIO%d)", (unsigned long)(adc * BATTERY_ADC_DIVIDER),
                 (unsigned long)adc, BATTERY_ADC_DIVIDER, BATTERY_ADC_PIN);
  });
#endif
}
