"""Pre-script PlatformIO: стемпить короткий git-sha у прошивку й sapi/revision.js.

Підключається як 'extra_scripts = pre:tools/pio_sapi_revision.py' - перед
кожною збіркою запускає tools/gen_sapi_revision.py (оновлює sapi/revision.js,
ідемпотентно) і додає build-flag GIT_REVISION з тим самим значенням - за
зразком уже наявного '-D PIO_PIOENV=\"${PIOENV}\"' у platformio.ini, тільки
значення тут не з secrets/PIOENV, а з git, тому й окремий Python-скрипт.

Окремий скрипт від tools/pio_web_assets.py навмисно - "один механізм на одну
задачу" (KISS, CLAUDE.md): той стежить за статикою порталу, цей - за
ідентичністю коміту. Детальніше - docs/mqtt-web-handoff.md, розділ
"Discovery payload".
"""

import sys
import pathlib

Import("env")  # noqa: F821 - глобал SCons

# __file__ у SCons-скрипті недоступний - шлях беремо з PlatformIO.
sys.path.insert(0, str(pathlib.Path(env.subst("$PROJECT_DIR")) / "tools"))  # noqa: F821
import gen_sapi_revision  # noqa: E402

if gen_sapi_revision.main() != 0:
    env.Exit(1)  # noqa: F821

revision = gen_sapi_revision.get_revision()
env.Append(BUILD_FLAGS=[f'-D GIT_REVISION=\\"{revision}\\"'])  # noqa: F821
