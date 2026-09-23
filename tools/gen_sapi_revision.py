#!/usr/bin/env python3
"""Стемпить короткий git-sha у sapi/revision.js.

Той самий короткий sha (git rev-parse --short HEAD) одночасно йде build-
flag'ом GIT_REVISION у прошивку (tools/pio_sapi_revision.py) і сюди -
sapi/revision.js. Точний збіг цих двох значень - те, чим SAPI перевіряє, що
браузерна сторінка й прошивка зібрані з того самого коміту (докладніше -
docs/mqtt-web-handoff.md, розділ "Discovery payload").

-dirty-суфікс (незакомічені зміни) свідомо не додається - див. той самий
розділ документа: наївний git status тут завжди "брудний" через файли, свідомо
не закомічені й не в .gitignore.

Запускається сам перед кожною збіркою - через pre-script
tools/pio_sapi_revision.py (див. extra_scripts у platformio.ini). Руками:
    ./tools/gen_sapi_revision.py
Згенерований файл комітиться разом із джерелом (той самий принцип, що й
WebPortalAssets.hpp).
"""

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "sapi" / "revision.js"


def get_revision() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        # Немає git у PATH або ROOT поза репозиторієм (напр. розпакований
        # архів джерел) - краще очевидна плашка "unknown", ніж провал збірки.
        return "unknown"


def main() -> int:
    revision = get_revision()
    content = (
        "// ЗГЕНЕРОВАНО tools/gen_sapi_revision.py - РУКАМИ НЕ ПРАВИТИ.\n"
        "// Перегенерувати: ./tools/gen_sapi_revision.py\n"
        "//\n"
        '// Короткий git-sha, з яким SAPI звіряє поле "revision" у\n'
        "// devices/<client-id>/discovery - розбіжність означає, що сторінка\n"
        "// й прошивка зібрані з різних комітів (докладніше -\n"
        "// docs/mqtt-web-handoff.md, розділ \"Discovery payload\").\n"
        f'window.SAPI_EXPECTED_REVISION = "{revision}";\n'
    )

    # Пишемо лише при реальній зміні - той самий принцип, що й у
    # gen_web_assets.py: інакше кожен 'pio run' оновлював би mtime файла.
    if OUT.is_file() and OUT.read_text() == content:
        return 0

    OUT.write_text(content)
    print(f"{OUT.relative_to(ROOT)} written ({revision})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
