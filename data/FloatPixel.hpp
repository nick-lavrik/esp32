#pragma once

#include <stdio.h>

/// Структура для промежуточных вычислений с плавающей точкой (диапазон 0.0f ... 1.0f)
struct FloatPixel {
    float r;
    float g;
    float b;
};

// --- ФУНКЦИИ РАСПАКОВКИ ---

FloatPixel unpack_RGB332(uint8_t color) {
    return {
        ((color >> 5) & 0x07) / 7.0f,
        ((color >> 2) & 0x07) / 7.0f,
        (color & 0x03) / 3.0f
    };
}

FloatPixel unpack_RGB565(uint16_t color) {
    return {
        ((color >> 11) & 0x1F) / 31.0f,
        ((color >> 5)  & 0x3F) / 63.0f,
        (color & 0x1F) / 31.0f
    };
}

FloatPixel unpack_RGB888(uint32_t color) {
    return {
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 8)  & 0xFF) / 255.0f,
        (color & 0xFF) / 255.0f
    };
}

// --- ФУНКЦИИ УПАКОВКИ (с математическим округлением +0.5f) ---

uint8_t pack_RGB332(FloatPixel p) {
    if (p.r < 0.0f) p.r = 0.0f; else if (p.r > 1.0f) p.r = 1.0f;
    if (p.g < 0.0f) p.g = 0.0f; else if (p.g > 1.0f) p.g = 1.0f;
    if (p.b < 0.0f) p.b = 0.0f; else if (p.b > 1.0f) p.b = 1.0f;
    return ((uint8_t)(p.r * 7.0f + 0.5f) << 5) | 
           ((uint8_t)(p.g * 7.0f + 0.5f) << 2) | 
            (uint8_t)(p.b * 3.0f + 0.5f);
}

uint16_t pack_RGB565(FloatPixel p) {
    if (p.r < 0.0f) p.r = 0.0f; else if (p.r > 1.0f) p.r = 1.0f;
    if (p.g < 0.0f) p.g = 0.0f; else if (p.g > 1.0f) p.g = 1.0f;
    if (p.b < 0.0f) p.b = 0.0f; else if (p.b > 1.0f) p.b = 1.0f;
    return ((uint16_t)(p.r * 31.0f + 0.5f) << 11) | 
           ((uint16_t)(p.g * 63.0f + 0.5f) << 5)  | 
            (uint16_t)(p.b * 31.0f + 0.5f);
}

uint32_t pack_RGB888(FloatPixel p) {
    if (p.r < 0.0f) p.r = 0.0f; else if (p.r > 1.0f) p.r = 1.0f;
    if (p.g < 0.0f) p.g = 0.0f; else if (p.g > 1.0f) p.g = 1.0f;
    if (p.b < 0.0f) p.b = 0.0f; else if (p.b > 1.0f) p.b = 1.0f;
    return ((uint32_t)(p.r * 255.0f + 0.5f) << 16) | 
           ((uint32_t)(p.g * 255.0f + 0.5f) << 8)  | 
            (uint32_t)(p.b * 255.0f + 0.5f);
}

// --- Единый интерфейс РАСПАКОВКИ ---
FloatPixel unpack_pixel(uint8_t color)  { return { ((color >>  5) & 0x07) /   7.0f, ((color >> 2) & 0x07) /   7.0f, (color & 0x03) /   3.0f }; }
FloatPixel unpack_pixel(uint16_t color) { return { ((color >> 11) & 0x1F) /  31.0f, ((color >> 5) & 0x3F) /  63.0f, (color & 0x1F) /  31.0f }; }
FloatPixel unpack_pixel(uint32_t color) { return { ((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f }; }

// --- Единый интерфейс УПАКОВКИ ---
// Используем выходной параметр по ссылке, чтобы компилятор различал перегрузку по сигнатуре
void pack_pixel(FloatPixel p, uint8_t& out) {
    if (p.r < 0.0f) p.r = 0.0f; else if (p.r > 1.0f) p.r = 1.0f;
    if (p.g < 0.0f) p.g = 0.0f; else if (p.g > 1.0f) p.g = 1.0f;
    if (p.b < 0.0f) p.b = 0.0f; else if (p.b > 1.0f) p.b = 1.0f;

    out = ((uint8_t)(p.r * 7.0f + 0.5f) << 5) | ((uint8_t)(p.g * 7.0f + 0.5f) << 2) | (uint8_t)(p.b * 3.0f + 0.5f);
}

void pack_pixel(FloatPixel p, uint16_t& out) {
    if (p.r < 0.0f) p.r = 0.0f; else if (p.r > 1.0f) p.r = 1.0f;
    if (p.g < 0.0f) p.g = 0.0f; else if (p.g > 1.0f) p.g = 1.0f;
    if (p.b < 0.0f) p.b = 0.0f; else if (p.b > 1.0f) p.b = 1.0f;

    out = ((uint16_t)(p.r * 31.0f + 0.5f) << 11) | ((uint16_t)(p.g * 63.0f + 0.5f) << 5) | (uint16_t)(p.b * 31.0f + 0.5f);
}

void pack_pixel(FloatPixel p, uint32_t& out) {
    if (p.r < 0.0f) p.r = 0.0f; else if (p.r > 1.0f) p.r = 1.0f;
    if (p.g < 0.0f) p.g = 0.0f; else if (p.g > 1.0f) p.g = 1.0f;
    if (p.b < 0.0f) p.b = 0.0f; else if (p.b > 1.0f) p.b = 1.0f;

    out = ((uint32_t)(p.r * 255.0f + 0.5f) << 16) | ((uint32_t)(p.g * 255.0f + 0.5f) << 8) | (uint32_t)(p.b * 255.0f + 0.5f);
}


// ----------------------------------------------------------------------------------------------------------------------------
// ЧАСТЬ 2: Попиксельные фильтры (Звенья цепочки)
// Эти функции принимают FloatPixel, трансформируют его структуру и возвращают измененный FloatPixel.
// Их можно вызывать строго одну за другой.
// ----------------------------------------------------------------------------------------------------------------------------

// 1. ДЕСАТУРАЦИЯ И ГАММА-КОРРЕКЦИЯ (Luma Rec. 709)
// factor: 0.0f = полная серость (ЧБ), 1.0f = оригинальные цвета.
FloatPixel fx_desaturate(FloatPixel p, float factor) {
    // Рассчитываем светимость с учетом физиологии человеческого глаза (зеленый ярче всего)
    float luma = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
    
    // Линейная интерполяция между ЧБ спектром и цветом
    p.r = luma + (p.r - luma) * factor;
    p.g = luma + (p.g - luma) * factor;
    p.b = luma + (p.b - luma) * factor;
    return p;
}

// 2. ОСВЕТЛЕНИЕ (Light Watermark эффекты)
// factor: 0.0f = без изменений, 1.0f = превратить в чисто белый фон
FloatPixel fx_lighten(FloatPixel p, float factor) {
    p.r = p.r + (1.0f - p.r) * factor;
    p.g = p.g + (1.0f - p.g) * factor;
    p.b = p.b + (1.0f - p.b) * factor;
    return p;
}

// 3. ЗАТЕМНЕНИЕ (Dark UI / Кинематографичный фон)
// factor: 1.0f = без изменений, 0.0f = увести в кромешную тьму
FloatPixel fx_darken(FloatPixel p, float factor) {
    p.r *= factor;
    p.g *= factor;
    p.b *= factor;
    return p;
}

// 4. ТОНИРОВАНИЕ (Color Tinting под цвет темы)
// tint: целевой цвет, в который окрашивается подложка (например, кремовый или темно-синий)
// alpha: 0.0f = только картинка, 1.0f = полностью цвет заливки
FloatPixel fx_tint(FloatPixel p, FloatPixel tint, float alpha) {
    p.r = p.r * (1.0f - alpha) + tint.r * alpha;
    p.g = p.g * (1.0f - alpha) + tint.g * alpha;
    p.b = p.b * (1.0f - alpha) + tint.b * alpha;
    return p;
}

// -------------------------------------------------------------------------------------------
// ЧАСТЬ 3: Буферные фильтры (Полноэкранные эффекты)
// Размытие по Гауссу и Дизеринг невозможно сделать попиксельно «в вакууме», так как размытие 
// требует информацию о соседях, а дизеринг — глобальные экранные координаты.
// 
// 1. Размытие по Гауссу (Разделяемый Box Blur)
// Честный Гаусс на МК работает неприлично медленно. Вместо него применяется математический 
// стандарт игровой индустрии — Box Blur, выполненный в два прохода (горизонтальный + вертикальный).
// Повторенный 2-3 раза, он дает визуально неотличимый от Гаусса гладкий результат с эффектом
// матового стекла.
// В примере показана функция для буфера RGB565 (для остальных типов меняется только тип указателя данных).
//
// blur_radius: радиус размытия (3, 5, 7 пикселей). Чем больше, тем сильнее размытие.
void fx_blur_RGB565(uint16_t* buffer, int width, int height, int radius) {
    if (radius < 1) return;
    
    // Создаем временный промежуточный буфер в RAM для раздельного прохода
    uint16_t* temp = (uint16_t*)malloc(width * height * sizeof(uint16_t));
    if (!temp) return; // Защита от нехватки памяти

    // Проход 1: Горизонтальное размытие из 'buffer' в 'temp'
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;
            
            for (int k = -radius; k <= radius; k++) {
                int nx = x + k;
                if (nx >= 0 && nx < width) {
                    FloatPixel p = unpack_RGB565(buffer[y * width + nx]);
                    r_sum += p.r; g_sum += p.g; b_sum += p.b;
                    count++;
                }
            }
            temp[y * width + x] = pack_RGB565({r_sum / count, g_sum / count, b_sum / count});
        }
    }

    // Проход 2: Вертикальное размытие из 'temp' обратно в исходный 'buffer'
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;
            
            for (int k = -radius; k <= radius; k++) {
                int ny = y + k;
                if (ny >= 0 && ny < height) {
                    FloatPixel p = unpack_RGB565(temp[ny * width + x]);
                    r_sum += p.r; g_sum += p.g; b_sum += p.b;
                    count++;
                }
            }
            buffer[y * width + x] = pack_RGB565({r_sum / count, g_sum / count, b_sum / count});
        }
    }
    
    free(temp); // Освобождаем память
}


// 2. Упорядоченный Дизеринг (Ordered Dithering 8x8)
// Критически необходим для схемы RGB332 и крайне полезен для RGB565.
// Он убирает полосы ступенчатых градиентов с помощью 64-уровневой
// матрицы Байера, которая динамически подмешивает шум квантования.
// Матрица Байера 8x8, масштабированная в диапазон [-0.5 ... 0.5]
const float bayer_8x8[8][8] = {
    {-0.5000,  0.2500, -0.3125,  0.4375, -0.4531,  0.2969, -0.2656,  0.4844},
    { 0.1250, -0.1250,  0.3125,  0.0625,  0.1719, -0.0781,  0.3594,  0.2188},
    {-0.3750,  0.3750, -0.4375,  0.1875, -0.3281,  0.4219, -0.3906,  0.3281},
    { 0.2500,  0.0000,  0.1250, -0.2500,  0.2344, -0.0156,  0.1094, -0.1406},
    {-0.4688,  0.2813, -0.2813,  0.4688, -0.4844,  0.2656, -0.3438,  0.4531},
    { 0.1563, -0.0938,  0.3438,  0.2031,  0.0938, -0.1563,  0.2813,  0.0313},
    {-0.3438,  0.4063, -0.4063,  0.3125, -0.3594,  0.3906, -0.4219,  0.1563},
    { 0.2188, -0.0313,  0.0938, -0.1563,  0.2031, -0.0469,  0.0625, -0.2188}
};

void fx_dithering_RGB332(uint8_t* buffer, int width, int height) {
    // Интенсивность шума (подбирается под разрядность сетки, для 3 бит ~ 1.0f/7.0f)
    const float spread = 0.14f; 

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            
            // 1. Достаем чистый пиксель
            FloatPixel p = unpack_RGB332(buffer[idx]);
            
            // 2. Получаем смещение шума из матрицы на основе координат экрана
            float noise = bayer_8x8[y & 7][x & 7] * spread;
            
            // 3. Подмешиваем шум
            p.r += noise;
            p.g += noise;
            p.b += noise;
            
            // 4. Пакуем обратно (при квантовании матрица создаст плавный оптический узор)
            buffer[idx] = pack_RGB332(p);
        }
    }
}

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
extern uint16_t* my_screen_buffer;
// ЧАСТЬ 4: Пример использования в реальной цепочке (Pipeline)
// Посмотрим, как элегантно выстраивается цепочка обработки кадра, например,
// в памяти ESP32 после того, как TJpg_Decoder заполнил ваш экранный буфер:
void process_background_image() {
    // ШАГ 1: Попиксельная обработка (Насыщенность -> Осветление -> Тонирование)
    FloatPixel target_ui_tint = {0.9f, 0.95f, 1.0f}; // Нежно-голубой оттенок UI

    for (int i = 0; i < (SCREEN_WIDTH * SCREEN_HEIGHT); i++) {
        // 1. Читаем исходный пиксель из буфера (допустим, экран RGB565)
        FloatPixel pixel = unpack_RGB565(my_screen_buffer[i]);

        // 2. Убираем ядерную насыщенность на 70% (оставляем 0.3f)
        pixel = fx_desaturate(pixel, 0.3f);

        // 3. Осветляем картинку наполовину под светлую тему
        pixel = fx_lighten(pixel, 0.5f);

        // 4. Слегка тонируем её в цвет нашего интерфейса
        pixel = fx_tint(pixel, target_ui_tint, 0.2f);

        // 5. Сохраняем результат промежуточного этапа обратно
        my_screen_buffer[i] = pack_RGB565(pixel);
    }

    // ШАГ 2: Размываем всю сцену для эффекта "Frosted Glass" (Матовое стекло)
    // Радиус 4 превратит JPEG артефакты фона в благородный софт-фокус
    fx_blur_RGB565(my_screen_buffer, SCREEN_WIDTH, SCREEN_HEIGHT, 4);

    // ШАГ 3: Если бы мы переводили в RGB332 — здесь вызывался бы fx_dithering_RGB332()
    
    // ВСЕ! Фон готов. Теперь поверх этой области памяти можно смело писать 
    // высококонтрастный текст штатными функциями: tft.drawString("Привет!");
}


// (!) TEMPLATES (!)
// Шаг 2. Создаем универсальный шаблон для Размытия (Blur)
// Теперь мы пишем один единственный шаблон функции.
// Компилятор сам подставит нужный тип T для выделения памяти (sizeof(T))
// и вызовет правильные перегрузки unpack_pixel и pack_pixel.
template <typename T>
void fx_blur(T* buffer, int width, int height, int radius) {
    if (radius < 1) return;
    
    // Динамически выделяем временный буфер под точный размер типа T
    T* temp = (T*)malloc(width * height * sizeof(T));
    if (!temp) return; 

    // Проход 1: Горизонтальное размытие (из buffer в temp)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;
            
            for (int k = -radius; k <= radius; k++) {
                int nx = x + k;
                if (nx >= 0 && nx < width) {
                    // Автоматически вызовется нужный unpack на основе типа T
                    FloatPixel p = unpack_pixel(buffer[y * width + nx]);
                    r_sum += p.r; g_sum += p.g; b_sum += p.b;
                    count++;
                }
            }
            // Автоматически вызовется нужный pack на основе типа T
            pack_pixel({r_sum / count, g_sum / count, b_sum / count}, temp[y * width + x]);
        }
    }

    // Проход 2: Вертикальное размытие (из temp обратно в buffer)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;
            
            for (int k = -radius; k <= radius; k++) {
                int ny = y + k;
                if (ny >= 0 && ny < height) {
                    FloatPixel p = unpack_pixel(temp[ny * width + x]);
                    r_sum += p.r; g_sum += p.g; b_sum += p.b;
                    count++;
                }
            }
            pack_pixel({r_sum / count, g_sum / count, b_sum / count}, buffer[y * width + x]);
        }
    }
    
    free(temp); 
}


// Главное отличие метода fx_blur_light заключается в том, что мы не выделяем память под копию всего экрана.
// Вместо этого мы создаем временный буфер размером ровно в одну строку/столбец (в зависимости от прохода).
// Это снижает потребление оперативной памяти (RAM) в сотни раз — например,
// для экрана 240x240 вместо 115 КБ понадобится менее 1 КБ.
// Ниже представлен универсальный шаблон функции с использованием перегруженных ранее unpack_pixel и pack_pixel.
// Оптимизированная по памяти функция fx_blur_light
template <typename T>
void fx_blur_light(T* buffer, int width, int height, int radius) {
    if (radius < 1) return;

    // Выделяем память только под ОДНУ строку/столбец
    // Размер берется по максимальной стороне, чтобы буфера хватило на оба прохода
    int max_dim = (width > height) ? width : height;
    T* line_buffer = (T*)malloc(max_dim * sizeof(T));
    
    if (!line_buffer) return; // Защита от нехватки памяти

    // ==========================================
    // ПРОХОД 1: ГОРИЗОНТАЛЬНОЕ РАЗМЫТИЕ
    // ==========================================
    for (int y = 0; y < height; y++) {
        // 1. Копируем текущую строку во временный линейный буфер
        for (int x = 0; x < width; x++) {
            line_buffer[x] = buffer[y * width + x];
        }

        // 2. Размываем пиксели из временного буфера обратно в основной
        for (int x = 0; x < width; x++) {
            float r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;

            for (int k = -radius; k <= radius; k++) {
                int nx = x + k;
                if (nx >= 0 && nx < width) {
                    FloatPixel p = unpack_pixel(line_buffer[nx]);
                    r_sum += p.r; g_sum += p.g; b_sum += p.b;
                    count++;
                }
            }
            pack_pixel({r_sum / count, g_sum / count, b_sum / count}, buffer[y * width + x]);
        }
    }

    // ==========================================
    // ПРОХОД 2: ВЕРТИКАЛЬНОЕ РАЗМЫТИЕ
    // ==========================================
    for (int x = 0; x < width; x++) {
        // 1. Копируем текущий столбец во временный линейный буфер
        for (int y = 0; y < height; y++) {
            line_buffer[y] = buffer[y * width + x];
        }

        // 2. Размываем пиксели из временного буфера обратно в основной
        for (int y = 0; y < height; y++) {
            float r_sum = 0, g_sum = 0, b_sum = 0;
            int count = 0;

            for (int k = -radius; k <= radius; k++) {
                int ny = y + k;
                if (ny >= 0 && ny < height) {
                    FloatPixel p = unpack_pixel(line_buffer[ny]);
                    r_sum += p.r; g_sum += p.g; b_sum += p.b;
                    count++;
                }
            }
            pack_pixel({r_sum / count, g_sum / count, b_sum / count}, buffer[y * width + x]);
        }
    }

    // Освобождаем мини-буфер строки
    free(line_buffer);
}


// Как это вызывается в коде?Вам даже не нужно вручную указывать угловые скобки <uint16_t> при вызове.
// Компилятор C++ выполняет выведение типов шаблона (Template Argument Deduction) автоматически на основе передаваемого указателя:
//
// uint8_t*  buffer_8bit  = ...; // Ваш буфер RGB332
// uint16_t* buffer_16bit = ...; // Ваш буфер RGB565
// uint32_t* buffer_32bit = ...; // Ваш буфер RGB888

// Компилятор сам создаст 3 разные функции под капотом и вызовет их:
// fx_blur(buffer_8bit,  240, 240, 4); // Вызовется версия для uint8_t (RGB332)
// fx_blur(buffer_16bit, 320, 240, 5); // Вызовется версия для uint16_t (RGB565)
// fx_blur(buffer_32bit, 480, 320, 6); // Вызовется версия для uint32_t (RGB888)

// 1. Эффект инверсии цвета (Invert / Негатив)
// Используется для быстрого выделения элементов (например, при наведении курсора или клике на иконку).
// Вместо того чтобы перерисовывать иконку, вы просто инвертируете пиксели под ней.
// Логика: Зеркально отразить значения каналов относительно 1.0f.
FloatPixel fx_invert(FloatPixel p) {
    p.r = 1.0f - p.r;
    p.g = 1.0f - p.g;
    p.b = 1.0f - p.b;
    return p;
}

// 2. Бинаризация / Пороговое ЧБ (Threshold)
// Превращает картинку в строго двухцветную (черно-белую, без серых оттенков).
// Отлично подходит для создания стилизованного ретро-интерфейса, комикс-эффектов
// или подготовки фонов для монохромных экранов (e-Ink).
// Логика: Если яркость выше порога — пиксель белый, если ниже — черный.
FloatPixel fx_threshold(FloatPixel p, float threshold) {
    // threshold: от 0.0f до 1.0f (обычно 0.5f)
    float luma = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
    float val = (luma >= threshold) ? 1.0f : 0.0f;
    return { val, val, val };
}

// 3. Эффект старой фотографии (Истинная Сепия)
// В отличие от обычного тонирования, честная сепия рассчитывается по специальной психологической
// матрице, которая имитирует химическое окрашивание фотобумаги солями серебра.
// Картинка выглядит очень винтажно и благородно.
FloatPixel fx_sepia(FloatPixel p) {
    FloatPixel out;
    out.r = (p.r * 0.393f) + (p.g * 0.769f) + (p.b * 0.189f);
    out.g = (p.r * 0.349f) + (p.g * 0.686f) + (p.b * 0.168f);
    out.b = (p.r * 0.272f) + (p.g * 0.534f) + (p.b * 0.131f);
    // Ограничение (clamping) выполнится автоматически при упаковке в pack_pixel
    return out;
}

// 4. Повышение экспозиции и контраста (Contrast Boost)
// Если исходный JPEG блеклый или темный, этот фильтр сделает его сочным и «кричащим».
// Темные участки станут еще темнее, а светлые — ярче.
// Логика: Использование сигмоидальной функции или смещения относительно центра 0.5f.
FloatPixel fx_contrast(FloatPixel p, float contrast) {
    // contrast: 1.0f — оригинал, выше 1.0f (например, 1.5f) — повышение контраста
    p.r = (p.r - 0.5f) * contrast + 0.5f;
    p.g = (p.g - 0.5f) * contrast + 0.5f;
    p.b = (p.b - 0.5f) * contrast + 0.5f;
    return p;
}

// 5. Цветовой сдвиг (Hue Rotate / Психоделический эффект)
// Позволяет циклически менять цвета изображения. Например, из зеленого листа сделать красный,
// не теряя теней и текстуры. В UI это безумно полезно: вы можете загрузить в память одну
// фоновую картинку, а меняя параметр Hue, динамически перекрашивать её под разные экраны
// (меню настроек — синее, меню тревоги — красное, меню эко — зеленое).
// Логика: Упрощенный поворот цветового пространства вокруг оси яркости.
#include <math.h>
FloatPixel fx_hue_rotate(FloatPixel p, float angle) {
// angle: угол поворота в радианах (от 0.0f до 6.28f (2*PI))
    float cosA = cos(angle);
    float sinA = sin(angle);

    // Матрица трансформации для поворота Hue
    FloatPixel out;
    out.r = (cosA + (1.0f - cosA) / 3.0f) * p.r +
            ((1.0f - cosA) / 3.0f - sqrt(1.0f / 3.0f) * sinA) * p.g +
            ((1.0f - cosA) / 3.0f + sqrt(1.0f / 3.0f) * sinA) * p.b;

    out.g = ((1.0f - cosA) / 3.0f + sqrt(1.0f / 3.0f) * sinA) * p.r +
            (cosA + (1.0f - cosA) / 3.0f) * p.g +
            ((1.0f - cosA) / 3.0f - sqrt(1.0f / 3.0f) * sinA) * p.b;

    out.b = ((1.0f - cosA) / 3.0f - sqrt(1.0f / 3.0f) * sinA) * p.r +
            ((1.0f - cosA) / 3.0f + sqrt(1.0f / 3.0f) * sinA) * p.g +
            (cosA + (1.0f - cosA) / 3.0f) * p.b;
    return out;
}

// 6. Эффект тепловизора / Градиентная карта (Thermal Vision)
// Этот эффект полностью отбрасывает оригинальные цвета и красит картинку на основе её яркости.
// Темные участки становятся синими, средние — зелеными и желтыми, яркие — красными и белыми.
// Прекрасный способ превратить скучное фото в футуристичный интерфейс в стиле киберпанка.
FloatPixel fx_thermal(FloatPixel p) {
    // Получаем яркость пикселя (0.0f ... 1.0f)
    float luma = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
    
    FloatPixel out;
    // Аппроксимация спектра тепловизора
    out.r = clamp((luma - 0.4f) * 3.0f, 0.0f, 1.0f);
    out.g = clamp(1.0f - abs(luma - 0.5f) * 3.0f, 0.0f, 1.0f);
    out.b = clamp((0.6f - luma) * 3.0f, 0.0f, 1.0f);
    
    // Вспомогательный лямбда-клемп, если в коде нет стандартного std::clamp
    return out;
}

// Вспомогательный мини-метод для тепловизора
inline float clamp(float v, float min, float max) {
    return (v < min) ? min : (v > max) ? max : v;
}


// Как это выглядит в итоговом конвейере?
// Благодаря вашей архитектуре, создание сложного составного эффекта превращается в конструктор.
// Например, сделаем «Стильный неоновый фон для киберпанк-темы»:
//
// for (int i = 0; i < SCREEN_PIXELS; i++) {
//     FloatPixel pixel = unpack_pixel(buffer[i]);
//
//     pixel = fx_desaturate(pixel, 0.1f);      // Почти полностью обесцвечиваем (оставляем 10% цвета)
//     pixel = fx_contrast(pixel, 1.4f);        // Накручиваем контраст, делая тени глубокими
//     pixel = fx_hue_rotate(pixel, 4.18f);     // Сдвигаем оставшиеся цвета в фиолетово-бирюзовую гамму
//     pixel = fx_darken(pixel, 0.4f);          // Затемняем на 60%, чтобы текст горел поверх фона
//
//     pack_pixel(pixel, buffer[i]);
// }
// Дошлифуем строчным легким размытием для софт-эффекта
// fx_blur_light(buffer, width, height, 3);

// Шаблон функции fx_emboss_light
// Этот алгоритм берет текущую строку, сравнивает её пиксели с пикселями следующей строки со смещением по оси X,
// рассчитывает карту высот и заливает результат на нейтрально-серый фон (50% яркости).
// Края, направленные к воображаемому "источнику света" (сверху-слева), становятся ярко-белыми, а противоположные края уходят в тень.
template <typename T>
void fx_emboss_light(T* buffer, int width, int height) {
    // Для работы алгоритма нам нужны исходные значения текущей и следующей строки.
    // Выделяем память всего под 2 строки, чтобы не затирать данные во время обработки.
    T* row_current = (T*)malloc(width * sizeof(T));
    T* row_next    = (T*)malloc(width * sizeof(T));
    
    if (!row_current || !row_next) {
        if (row_current) free(row_current);
        if (row_next) free(row_next);
        return; // Защита от нехватки RAM
    }

    // Инициализируем первую строку перед стартом цикла
    memcpy(row_current, buffer, width * sizeof(T));

    // Проходим по вертикали (последнюю строку пропускаем, так как у нее нет "следующего" соседа)
    for (int y = 0; y < height - 1; y++) {
        // Копируем следующую строку во временный буфер
        memcpy(row_next, &buffer[(y + 1) * width], width * sizeof(T));

        // Проходим по горизонтали (последний пиксель строки пропускаем)
        for (int x = 0; x < width - 1; x++) {
            // 1. Распаковываем текущий пиксель (A) и его соседа по диагонали снизу-справа (B)
            FloatPixel p_A = unpack_pixel(row_current[x]);
            FloatPixel p_B = unpack_pixel(row_next[x + 1]);

            // 2. Считаем их воспринимаемую яркость (Luma Rec. 709)
            float luma_A = 0.2126f * p_A.r + 0.7152f * p_A.g + 0.0722f * p_A.b;
            float luma_B = 0.2126f * p_B.r + 0.7152f * p_B.g + 0.0722f * p_B.b;

            // 3. Вычисляем разность (рельеф)
            // Умножение на коэффициент (например, 2.0f..4.0f) регулирует глубину/резкость тиснения
            float diff = (luma_A - luma_B) * 3.0f;

            // 4. Накладываем рельеф на нейтральный серый фон (0.5f)
            FloatPixel emboss_pixel;
            emboss_pixel.r = 0.5f + diff;
            emboss_pixel.g = 0.5f + diff;
            emboss_pixel.b = 0.5f + diff;

            // Ограничение (Clamping) выполнится автоматически внутри pack_pixel
            // Перезаписываем пиксель в основном буфере кадра
            pack_pixel(emboss_pixel, buffer[y * width + x]);
        }

        // Текущая "следующая" строка становится "текущей" для следующей итерации цикла Y
        T* temp = row_current;
        row_current = row_next;
        row_next = temp;
    }

    // Освобождаем память двух строк
    free(row_current);
    free(row_next);
}


// Секреты кастомизации эффекта Emboss
// Классический серый Emboss может выглядеть слишком мрачно для современного UI. 
// Однако на базе этой математики можно сделать два потрясающих подэффекта:
//
// 1. Цветное тиснение (Color Emboss)Если вместо серого фона (0.5f) подмешивать 
// рельеф к оригинальному цвету пикселя, вы получите эффект объемной цветной 
// текстуры (как будто картинку выдавили на холсте или коже).
// Как изменить в коде:
//
// emboss_pixel.r = p_A.r + diff;
// emboss_pixel.g = p_A.g + diff;
// emboss_pixel.b = p_A.b + diff;

// 2. Эффект металлической гравировки (Chrome / Metal)
// Если пропустить полученный серый Emboss через фильтр Contrast Boost 
// (который мы разбирали в прошлом шаге) со значением контраста 2.5f - 3.5f,
// то мягкие серые тени превратятся в жесткие глянцевые блики.
// Картинка станет выглядеть как вытравленная на пластине полированного хрома или алюминия.
//
// 3. Изменение направления света
// В коде выше мы сравниваем пиксель (x, y) с (x + 1, y + 1).
// Это имитирует свет, падающий строго из верхнего левого угла.
// Если вы захотите, чтобы свет падал справа налево, нужно сравнивать текущий пиксель с (x - 1, y + 1).
//
//
// # Цепочка для идеального "водяного" знака на кнопках:
//
// Если вы хотите сделать красивую объемную, но при этом ненавязчивую подложку для сенсорного меню:
//
// 1. Делаем цветное тиснение (модифицировав функцию под оригинальный цвет)
// fx_emboss_color_light(my_buffer, 240, 240);

// 2. Слегка размываем, чтобы сгладить "шумные" острые пиксели краев JPEG
// fx_blur_light(my_buffer, 240, 240, 1);

// 3. Осветляем (или затемняем), чтобы поверх идеально читался шрифт интерфейса
// for(int i = 0; i < TOTAL_PIXELS; i++) {
//     my_buffer[i] = pack_pixel(
//         fx_lighten(
//            unpack_pixel(my_buffer[i]),
//            0.4f
//         ),
//         my_buffer[i]
//     );
// }

