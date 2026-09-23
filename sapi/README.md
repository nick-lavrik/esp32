# ESP32 SAPI

Браузерний клієнт MQTT SAPI-каналу (`docs/mqtt-web-handoff.md`, фаза 1/2).
Одна статична сторінка (`index.html`), нуль build-кроку, нуль залежності від
цього репо під час виконання — підключається напряму до MQTT-брокера
(WebSockets), без жодного HTTP-запиту до плати.

**Не частина прошивки.** `pio run` цю теку не бачить (поза `src/lib/include`),
не потрапляє в LittleFS-образ веб-порталу. Живе в монорепо свідомо (розділ
"Монорепо" сесії, `docs/mqtt-web-handoff.md`) — контракт (топіки, JSON-shape)
і сторінка, що його споживає, ще активно змінюються синхронно.

## Запуск

Будь-який статичний http-сервер із цієї теки, напр.:
```
python3 -m http.server 8765
```
і відкрити `http://localhost:8765/`.

## Підключення

Форма підключення (host/port/prefix/username/password/client-id) —
`localStorage` цього браузера, нічого не передається нікуди, крім самого
брокера. Кредеші для конкретного пристрою — `docs/mqtt-web-handoff.md`,
розділ "storage-шар" (п.8): rpi5, окремий WS-listener (`9001`), окремі
`sapi_passwd`/`sapi_acl`, один статичний акаунт на пристрій
(`sapi-<client-id>`, read-only на `devices/<client-id>/#`, write лише на
`.../api/+`).

## Пре-альфа — жорстко зашитий allowlist

`COMMANDS` у `index.html` — той самий список, що в `src/main.cpp`
(`kJsonApiSystemInfo`/`kJsonApiWifiStatus`/`kJsonApiEcoflowStatus`). Коли
з'явиться discovery-маніфест (фаза 2), він замінить цей список UI, що
будується з даних брокера, а не з коду тут.

## Рендер і тест

`render.js` — чисті функції рендера (`renderSystemInfo`/`renderWifiStatus`/
`renderEcoflowStatus`, без DOM), винесені з `index.html` окремим файлом саме
для того, щоб той самий код можна було прогнати headless у Node:

```
node sapi/test/render.test.js
```

Тест — без test-раннера (у проєкті його ще нема, `docs/tech_debt.md`),
голий Node + `assert`, фейл — ненульовий exit code. Покриває щасливий шлях і
крайові випадки (відсутній `ap`, порожні `devices`, `nvs.available=false`,
`null`-поля, XSS-екранування) на реальній формі JSON з живого тесту в
браузері. Не перевіряє CSS/layout — лише що прийде в `innerHTML`.
