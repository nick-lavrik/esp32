#!/usr/bin/env python3
"""Робить шляхи в compile_commands.json абсолютними.

PlatformIO генерує їх ВІДНОСНИМИ ("-Ilib/Logger", "file": "src/main.cpp") і
покладається на поле "directory". Формально це коректно, але CompDB-плагін
IntelliJ IDEA таких шляхів не резолвить - у редакторі відвалюються всі власні
бібліотеки з lib/ ("Cannot resolve symbol 'TLogger'"), хоча збірка проходить.

Запускати ПІСЛЯ `pio run -t compiledb`:
    python3 tools/abs_compiledb.py [шлях до compile_commands.json]
"""
import json
import os
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "compile_commands.json"
with open(path, encoding="utf-8") as handle:
    entries = json.load(handle)

def absolutize(token: str, base: str) -> str:
    for flag in ("-I", "-iquote", "-isystem"):
        if token.startswith(flag) and len(token) > len(flag):
            value = token[len(flag):]
            if not os.path.isabs(value):
                return flag + os.path.normpath(os.path.join(base, value))
            return token
    return token

patched_files = patched_includes = 0
for entry in entries:
    base = entry.get("directory") or os.getcwd()

    if not os.path.isabs(entry.get("file", "")):
        entry["file"] = os.path.normpath(os.path.join(base, entry["file"]))
        patched_files += 1

    if "arguments" in entry:
        new_args = [absolutize(a, base) for a in entry["arguments"]]
        patched_includes += sum(1 for a, b in zip(entry["arguments"], new_args) if a != b)
        entry["arguments"] = new_args
    elif "command" in entry:
        # Розбір по пробілах: шляхи з пробілами в командному рядку PlatformIO
        # взяті в лапки, тому такий токен просто лишається без змін.
        tokens = entry["command"].split(" ")
        new_tokens = [absolutize(t, base) for t in tokens]
        patched_includes += sum(1 for a, b in zip(tokens, new_tokens) if a != b)
        entry["command"] = " ".join(new_tokens)

with open(path, "w", encoding="utf-8") as handle:
    json.dump(entries, handle, indent=1)

print(f"{path}: {len(entries)} записів, "
      f"абсолютизовано {patched_files} file і {patched_includes} include-шляхів")
