"""Pre-script PlatformIO: тримає WebPortalAssets.hpp у синхроні з assets/www/.

Підключається як 'extra_scripts = pre:tools/pio_web_assets.py' - перед кожною
збіркою запускає tools/gen_web_assets.py. Генератор ідемпотентний: якщо вміст
заголовка не змінився, файл не перезаписується і перекомпіляції не буде.

Причина: правка assets/www/index.html без ручного './tools/gen_web_assets.py'
давала прошивку зі старою сторінкою - мовчки, бо збірка проходить успішно.
"""

import sys
import pathlib

Import("env")  # noqa: F821 - глобал SCons

# __file__ у SCons-скрипті недоступний - шлях беремо з PlatformIO.
sys.path.insert(0, str(pathlib.Path(env.subst("$PROJECT_DIR")) / "tools"))  # noqa: F821
import gen_web_assets  # noqa: E402

if gen_web_assets.main() != 0:
    env.Exit(1)  # noqa: F821
