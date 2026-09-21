#pragma once

#include <cstdint>

// Тристан: поки відповідне поле не прийшло в quota, стан саме невідомий -
// плутати його з "немає мережі" не можна (quota надсилає ЛИШЕ те, що змінилось,
// тому багато полів довго лишаються без значення).
//
// Живе в окремому файлі, а не в EcoflowDeviceRegistry.hpp: EcoflowGridJournal
// потребує цей enum у своєму публічному API, а EcoflowDeviceRegistry тримає
// масив EcoflowGridJournal - без винесення вийшов би циклічний #include.
enum class EcoflowGridState : uint8_t {
  Unknown = 0,
  OffGrid,
  OnGrid,
};

const char *ecoflowGridStateName(EcoflowGridState state);
