#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

// Неблокуючий обробник команд, що надходять через Serial.
// Викликайте update() у loop() на кожній ітерації — метод НЕ блокує
// виконання, навіть якщо у порту ще немає жодного байта.
class SerialCommandHandler {
public:
    using CommandCallback = std::function<void(const String& args)>;

    SerialCommandHandler(Stream& serial = Serial, char terminator = '\n');

    // Реєстрація нової команди.
    // name        — назва команди, порівнюється без урахування регістру
    // description — опис для команди "list"
    // callback    — функція, що отримає рядок аргументів після назви команди
    void registerCommand(const String& name, const String& description, CommandCallback callback);

    // Викликати щоразу в loop(). Неблокуючий.
    void update();

    // Максимальна довжина буфера рядка
    // (захист від переповнення, якщо хтось шле дані без термінатора).
    void setMaxLineLength(size_t maxLen) { maxLineLength_ = maxLen; }

private:
    struct Command {
        String name;
        String description;
        CommandCallback callback;
    };

    Stream& serial_;
    char terminator_;
    String buffer_;
    size_t maxLineLength_ = 128;
    std::vector<Command> commands_;

    void processLine(const String& line);
    void printUnknown(const String& name) const;
    void printList() const;

    static String trim(const String& s);
    static void splitFirstToken(const String& line, String& outName, String& outArgs);
};
