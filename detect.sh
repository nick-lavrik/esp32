#!/bin/bash

# Проверяем наличие esptool.py в системе
if ! command -v esptool &> /dev/null; then
    echo "Ошибка: esptool не найден."
    echo "Установите его командой: pip install esptool"
    exit 1
fi

echo "=================================================="
echo " Поиск и определение подключенных плат ESP... "
echo "=================================================="

found=0

# Цикл проходит по обоим типам портов: ttyUSB и ttyACM
for port in /dev/ttyUSB* /dev/ttyACM*; do
    # Проверяем, существует ли файл устройства в реальности
    if [ -e "$port" ]; then
        found=1
        echo -e "\n🔎 Опрашиваю порт: \033[1;33m$port\033[0m"
        
        # Запускаем esptool на безопасной скорости 115200 для чтения chip_id
        # Перенаправляем ошибки, чтобы они не засоряли консоль
        OUTPUT=$(esptool --port "$port" --baud 115200 chip-id 2>&1)
        
        # Извлекаем строку с типом чипа и MAC-адресом
        # esptool v4 друкує "Chip is ...", v5 - "Chip type: ..."
        CHIP=$(echo "$OUTPUT" | grep -E "Chip is|Chip type:" | head -1)
        MAC=$(echo "$OUTPUT" | grep "MAC:" | head -1)
        
        if [ ! -z "$CHIP" ]; then
            # Очищаем вывод от лишних пробелов и выводим красиво
            CHIP_CLEAN=$(echo "$CHIP" | sed -E 's/Chip is |Chip type: *//')
            MAC_CLEAN=$(echo "$MAC" | sed 's/MAC: //')
            
            echo -e " ✅ Найдено: \033[1;32mESP-$CHIP_CLEAN\033[0m"
            if [ ! -z "$MAC_CLEAN" ]; then
                echo -e " 🆔 MAC-адрес: $MAC_CLEAN"
            fi
        else
            # Если плата не ответила (занят порт или это другое устройство)
            echo " ❌ Устройство не отвечает (возможно, порт занят другой программой)"
        fi
    fi
done

if [ "$found" -eq 0 ]; then
    echo "❌ Активные порты /dev/ttyUSB* или /dev/ttyACM* не найдены."
fi

echo -e "\n=================================================="
