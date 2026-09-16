#pragma once

#include <cstdint>

// Тестова таблиця дисплея: набір статичних патернів для звірки панелі з
// вкладкою Screen (порядок кольору/байтів, яскравість і гама, зсуви й
// ротація, різкість, шрифти) - режим, що заміщає звичайну сцену тим самим
// способом, що dino test/background (див. src/main.cpp::loop()).
// Задум і причина кожного патерну - docs/tech_debt.md, розділ
// "Команда перевірки дисплея (тестова таблиця)".
enum class TestGfxPattern : uint8_t {
  Bars,        // кольорові смуги - порядок каналів
  Gray,        // сіра шкала + латки біля чорного й білого - яскравість, гама
  Gradient,    // градієнт RGB565 (R/G/B нарізно) + повна палітра RGB332 (256 кольорів)
  Frame,       // рамка, кути, перехрестя - зсуви й ротація
  Checker,     // шахівниця - різкість і помилки stride
  Primitives,  // фігури й шрифти - drawRect/fillRect/drawCircle/drawBitmapScaled/LOAD_FONT*
};

// Коротке ім'я патерну для команди й логів (те саме, що приймає команда
// "test-gfx <name>"). Повертає "?" для значення поза enum (не мало б статись).
const char* testGfxPatternName(TestGfxPattern p);

// Розбір рядка команди в патерн; false, якщо назва невідома.
bool testGfxPatternFromName(const char* name, TestGfxPattern* out);

// Наступний патерн у демонстраційному циклі (за порядком enum, з переходом
// з Primitives назад на Bars). Використовує "test-gfx on" без явного імені
// патерну - див. src/main.cpp::loop().
TestGfxPattern testGfxNextPattern(TestGfxPattern p);

// Малює ОДНУ ітерацію (== одну активну смугу) обраного патерну. Візерунок
// статичний - не залежить від millis()/random(), інакше кожна смуга (яка
// малюється в окремому виклику функції) показала б свою фазу (див.
// Display::isFrameStart()). Викликати щоітерації loop(), поки режим
// активний - так само, як renderSpriteSheet() у DinoRenderer.
void drawTestGfx(TestGfxPattern pattern);
