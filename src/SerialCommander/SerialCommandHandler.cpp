#include "SerialCommandHandler.hpp"

SerialCommandHandler::SerialCommandHandler(Stream& serial, char terminator)
    : serial_(serial), terminator_(terminator) {
    // Вбудована команда "list" / "help" — виводить усі зареєстровані команди.
    registerCommand("list", "Показати список доступних команд", [this](const String&) { printList(); });
    // registerCommand("help", "Показати список доступних команд", [this](const String&) { printList(); });
}

void SerialCommandHandler::registerCommand(const String& name, const String& description, CommandCallback callback) {
    commands_.push_back(Command{ name, description, callback });
}

void SerialCommandHandler::update() {
    // Неблокуюче: заходимо у while лише якщо дані вже є в буфері UART.
    while (serial_.available() > 0) {
        char c = static_cast<char>(serial_.read());

        if (c == terminator_) {
            String line = trim(buffer_);
            buffer_ = "";
            if (line.length() > 0) {
                processLine(line);
            }
            // Обробили один рядок за виклик update() — достатньо для
            // відгуку в межах одного циклу loop(); за потреби можна
            // прибрати return/break, щоб обробити декілька рядків одразу.
            break;
        } else if (c == '\r') {
            // ігноруємо CR, чекаємо на LF (або окремий CR як термінатор)
            continue;
        } else {
            buffer_ += c;
            if (buffer_.length() > maxLineLength_) {
                // захист від переповнення буфера, якщо термінатор не прийшов
                serial_.println(F("[SerialCommandHandler] Рядок занадто довгий, буфер очищено"));
                buffer_ = "";
            }
        }
    }
}

void SerialCommandHandler::processLine(const String& line) {
    String name, args;
    splitFirstToken(line, name, args);

    for (const auto& cmd : commands_) {
        if (cmd.name.equalsIgnoreCase(name)) {
            cmd.callback(args);
            return;
        }
    }

    printUnknown(name);
}

void SerialCommandHandler::printUnknown(const String& name) const {
    serial_.print(F("Невідома команда: "));
    serial_.println(name);
    serial_.println(F("Введіть 'list' для перегляду доступних команд"));
}

void SerialCommandHandler::printList() const {
    serial_.println(F("Доступні команди:"));
    for (const auto& cmd : commands_) {
        serial_.printf("  %-10s - %s\n", cmd.name.c_str(), cmd.description.c_str());
    }
}

String SerialCommandHandler::trim(const String& s) {
    int start = 0;
    int end = s.length() - 1;
    while (start <= end && isspace(s[start])) start++;
    while (end >= start && isspace(s[end])) end--;

    return s.substring(start, end + 1);
}

void SerialCommandHandler::splitFirstToken(const String& line, String& outName, String& outArgs) {
    int spaceIdx = line.indexOf(' ');
    if (spaceIdx == -1) {
        outName = line;
        outArgs = "";
    } else {
        outName = line.substring(0, spaceIdx);
        outArgs = trim(line.substring(spaceIdx + 1));
    }
}
