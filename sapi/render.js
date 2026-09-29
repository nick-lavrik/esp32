// Чисті функції рендера SAPI-панелей (без DOM/document) - винесені сюди з
// index.html, щоб той самий код можна було прогнати і в браузері (звичайний
// <script src="render.js"> перед основним інлайн-скриптом), і headless у
// Node (test/render.test.js через require()) без жодного DOM/браузера.
//
// Пре-альфа: рендер під конкретні поля кожної команди, жорстко прив'язаний
// до COMMANDS в index.html, той самий дух пре-альфи - коли з'явиться
// discovery-маніфест (фаза 2), він і замінить цей список разом із рендерами
// на щось, що будується з даних, а не з коду тут.

function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
}
function badge(text, cls) { return `<span class="badge ${cls}">${esc(text)}</span>`; }
function boolBadge(b, okText = 'yes', noText = 'no') { return badge(b ? okText : noText, b ? 'ok' : 'err'); }
// Той самий рівень сигналу, що signal() у порталі (assets/www/index.html) -
// блоки фіксованого розміру (CSS .sig, sapi/index.html), не гліфи ▮/▯ (різна
// ширина в system-ui хитала б шкалу). rssi відсутній (0/null/undefined) -
// прочерк, без спроби намалювати нульову шкалу.
function signal(rssi, quality) {
  if (!rssi) return '<span class="muted">-</span>';
  const bars = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
  let html = '<span class="sig"><b>';
  for (let i = 1; i <= 4; i++) html += i <= bars ? '<i class="on"></i>' : '<i></i>';
  html += `</b><span class="val">${Number(rssi)} <span class="u">dBm</span></span>`;
  if (quality !== undefined && quality !== null) html += `<span class="q">${Number(quality)}%</span>`;
  return html + '</span>';
}
function kv(pairs) {
  return '<dl class="kv">' + pairs.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${v}</dd>`).join('') + '</dl>';
}
// dt/dd рядки для голого <dl> (без .kv) - той самий словник, що
// dlRows()/renderSystemStatus() у порталі (assets/www/index.html): System-
// вкладка SAPI від цієї функції й далі копіює вигляд порталу буквально,
// а не власний стиль kv()/badge() (той лишається для wifi-status/
// ecoflow-status, ще не переведених).
function dlRows(pairs) {
  return pairs.map(([k, v]) => `<dt>${esc(k)}</dt><dd>${v}</dd>`).join('');
}
// Поле, якого ще немає в цій MQTT-команді (system-info несе лише chip/heap/
// flash/nvs/partitions - portal.js/src/main.cpp kJsonApiSystemInfo) - "***"
// замість того, щоб мовчки пропустити рядок або підставити 0: так само
// видно РЯДОК (форма картки вже готова під майбутнє поле), і видно, що САМЕ
// значення ще не приїхало, а не "приїхав нуль".
function mockDl(labels) {
  return labels.map((k) => `<dt>${esc(k)}</dt><dd class="muted">***</dd>`).join('');
}
// Пробіли на краю SSID - видимою міткою (буквальний порт ssidLabel() з
// порталу, assets/www/index.html): без неї "Asus " і "Asus" у таблиці
// збережених профілів виглядають однаково, і другий рядок здається дублем
// першого. CSS .ws/.ws::before - sapi/index.html, той самий словник, що в
// порталі. Повертає розмітку: екранування вже всередині (esc()).
function ssidLabel(ssid) {
  return esc(ssid).replace(/^\s+|\s+$/g, (run) => '<span class="ws" title="space"></span>'.repeat(run.length));
}
function dash(v) { return v === null || v === undefined ? '-' : v; }
function fmtBytes(n) {
  if (n === null || n === undefined) return '-';
  if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(2) + ' MB';
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}
// Спільна одиниця виміру на всю трійку used/total/free - той самий підхід,
// що fsUnit()/fsSizeIn() у порталі (assets/www/index.html): "1.20 MB" і
// "0.32 MB" вирівнюються по ширині, а "1.2 MB" поруч із "324.0 KB" ні.
function fsUnit(bytes) {
  if (bytes < 1024) return { div: 1, suffix: ' B', digits: 0 };
  if (bytes < 1024 * 1024) return { div: 1024, suffix: ' KB', digits: 1 };
  if (bytes < 1024 * 1024 * 1024) return { div: 1024 * 1024, suffix: ' MB', digits: 2 };
  return { div: 1024 * 1024 * 1024, suffix: ' GB', digits: 2 };
}
function fsSizeIn(bytes, unit) { return (bytes / unit.div).toFixed(unit.digits) + unit.suffix; }
function fmtEpoch(sec) { return sec ? new Date(sec * 1000).toLocaleString() : '-'; }

// Uptime пристрою (System > Device) - "XXd hh:mm:ss", дні лише якщо плата
// активна довше доби (за проханням користувача); без днів узагалі, коли
// uptime < 24 год - "01:02:05", не "0d 01:02:05".
function fmtUptime(ms) {
  if (ms === null || ms === undefined) return '-';
  const total = Math.floor(ms / 1000);
  const days = Math.floor(total / 86400);
  const pad = (n) => String(n).padStart(2, '0');
  const dayPrefix = days > 0 ? days + 'd ' : '';
  return dayPrefix + pad(Math.floor(total / 3600) % 24) + ':' + pad(Math.floor(total / 60) % 60) + ':' + pad(total % 60);
}

// Той самий рядок, що statusRows() у порталі (assets/www/index.html) -
// System-картка Network заповнюється ЖИВИМИ даними з wifi-status (той самий
// SAPI-запит, що й вкладка Wi-Fi, лише інша команда вже мала свою відповідь -
// renderReply() в index.html кладе останню відому сюди), а не власним другим
// MQTT-запитом. "Signal" - той самий signal() (смужки), що й портал.
function wifiSystemRows(data) {
  const ap = data.ap || {};
  return [
    ['State', esc(dash(data.state))],
    ['SSID', esc(data.ssid || '-')],
    ['IP address', esc(data.ip || '-')],
    ['Gateway', esc(data.connected ? (data.gateway || '-') : '-')],
    ['Signal', data.connected ? signal(data.rssi, data.quality) : '<span class="muted">-</span>'],
    ['Connection type', esc(data.connected ? (data.phyMode || '-') : '-')],
    ['MAC', esc(dash(data.mac))],
    ['Auto reconnect', data.autoReconnect ? 'on' : 'off'],
    ['Hotspot', ap.active
      ? `${esc(dash(ap.ssid))} · ${esc(dash(ap.ip))} · ${dash(ap.clients)} client(s) · ${esc(dash(ap.security))}`
      : 'off'],
  ];
}

// Той самий підсумок, що renderEcoflowSystemSummary() у порталі - рядки
// Connection/Broker/.../Messages received, буквально той самий текст
// (portal: без badge на "Connection" - лишається так само тут, для 1:1).
function ecoflowSystemRows(data) {
  const devices = Array.isArray(data.devices) ? data.devices : [];
  const online = devices.filter((d) => d.presence === 'online').length;
  const offline = devices.filter((d) => d.presence === 'offline').length;
  const unknown = devices.length - online - offline;
  const counts = [[online, 'online'], [offline, 'offline'], [unknown, 'unknown']]
    .filter(([n]) => n > 0).map(([n, label]) => `<span>${n} ${label}</span>`).join('');
  return [
    ['Connection', data.connected ? 'connected' : 'disconnected'],
    ['Broker', esc(dash(data.brokerHost)) + (data.brokerHost ? ':' + esc(dash(data.brokerPort)) : '')],
    ['Connection type', data.viaProxy ? 'proxy (plain MQTT)' : 'direct (TLS)'],
    ['Devices', `<span class="eco-counts">${counts}</span>`, true],
    ['Messages received', dash(data.messageCount)],
  ];
}

// --- EcoFlow: компактний розгортний перелік пристроїв (System-картка) -
// буквальний порт portal (assets/www/index.html: ecoDuration/ecoRemain/
// ecoGrid/ecoWatts/ecoAcVolts/ecoAcText/ecoCharge/ecoChargeRounded/ecoRow/
// ecoAgo/ecoPresence/ecoDeviceDetailRows/ecoSysDeviceRow), не переказ -
// "1:1 як на порталі" (запит користувача цієї сесії). Ті самі формули, той
// самий набір/порядок полів у розгорнутій картці.

function ecoDuration(ms) {
  if (ms == null) return '-';
  const total = Math.floor(ms / 1000);
  const d = Math.floor(total / 86400), h = Math.floor((total % 86400) / 3600), m = Math.floor((total % 3600) / 60);
  if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
  if (h > 0) return h + 'h ' + m + 'm';
  if (m > 0) return m + 'm';
  return (total % 60) + 's';
}
function ecoRemain(minutes) {
  if (minutes == null) return '-';
  const h = Math.floor(minutes / 60), m = minutes % 60;
  return h > 0 ? h + 'h' + String(m).padStart(2, '0') + 'm' : m + 'm';
}
function ecoAgo(ms) { return ms == null ? 'never' : ecoDuration(ms) + ' ago'; }
function ecoPresence(presence) {
  const color = presence === 'online' ? 'var(--ok)' : presence === 'offline' ? 'var(--err)' : 'var(--muted)';
  return `<span style="color:${color}">${esc(dash(presence))}</span>`;
}
// Одна таблиця grid -> колір замість двох копій тернарника (текст ecoGrid()
// + смуга journal-рядка, ecoJournalRow() нижче) - DRY (CLAUDE.md).
function ecoGridColorVar(grid, dim) {
  const key = grid === 'on-grid' ? 'ok' : grid === 'off-grid' ? 'warn' : 'muted';
  return dim ? `var(--${key}-dim)` : `var(--${key})`;
}
function ecoGrid(grid, inferred) {
  return `<span style="color:${ecoGridColorVar(grid, false)}">${esc(dash(grid))}</span>` + (inferred ? '<span class="muted"> (inferred)</span>' : '');
}
function ecoWatts(inputWatts, outputWatts) {
  if (inputWatts == null && outputWatts == null) return '-';
  return 'in ' + (inputWatts ?? '-') + ' W · out ' + (outputWatts ?? '-') + ' W';
}
function ecoCharge(d) {
  if (d.socPercent == null) return '-';
  return d.socPrecise != null ? d.socPrecise + '%' : d.socPercent + '%';
}
function ecoChargeRounded(d) { return d.socPercent == null ? '-' : d.socPercent + '%'; }
function ecoAcVolts(d) { return d.acInputMilliVolts == null ? '-' : (d.acInputMilliVolts / 1000).toFixed(1) + ' V'; }
function ecoAcText(d) {
  const hz = d.acInputFrequency != null ? ' · ' + d.acInputFrequency + ' Hz' : '';
  const text = ecoAcVolts(d) + hz;
  return d.acInputMilliVolts === 0 ? `<span class="muted">${text}</span>` : text;
}
// Другорядна частина значення - завжди окремим рядком під основним (.eco-sub,
// CSS), а не впритул через " · " (переносилось би посередині слова на
// вузькій картці). sub зібраний тут же - esc() усередині не потрібен.
function ecoRow(primary, sub) { return primary + (sub ? `<span class="eco-sub">${sub}</span>` : ''); }

// Спільний перелік dt/dd розгорнутого пристрою - той самий список, що на
// повній картці вкладки EcoFlow мав би бути (тут - System-картка, компактний
// вхід), щоб поля не розходились між копіями (DRY, CLAUDE.md).
function ecoDeviceDetailRows(d) {
  const acVolts = ecoAcVolts(d);
  return [
    ['Presence', ecoPresence(d.presence), true],
    ['Charge', ecoCharge(d)],
    ['Grid', ecoRow(ecoGrid(d.grid, d.gridInferred),
      (d.gridForMs != null ? 'for ' + ecoDuration(d.gridForMs) + ' · ' : '') +
      dash(d.gridChangeCount) + ' change(s) all-time'), true],
    ['Power', ecoWatts(d.inputWatts, d.outputWatts), true],
    ['Remaining', ecoRemain(d.remainTimeMinutes)],
    ['AC input', ecoRow(acVolts, d.acInputFrequency != null ? d.acInputFrequency + ' Hz' : ''), true],
    ['Last message', ecoRow(esc(ecoAgo(d.ageMs)), dash(d.messageCount) + ' message(s)'), true],
    ['REST snapshot', d.snapshotAvailable ? 'available' : 'not allowed by device'],
  ].map(([k, v, raw]) => `<dt>${esc(k)}</dt><dd>${raw ? v : esc(String(v))}</dd>`).join('');
}

// isOpen - переданий явно, не читання DOM тут (render.js без DOM, той самий
// принцип, що й решта файлу): index.html сканує поточні відкриті <details>
// ПЕРЕД перемальовкою (той самий патерн, що ecoSysOpenDetails у порталі) і
// передає множину серійників в ecoflowSystemDevicesHtml() нижче.
function ecoSysDeviceRow(d, isOpen) {
  return `<details class="sys-eco-device"${isOpen ? ' open' : ''} data-sn="${esc(d.serialNumber)}">
    <summary>
      <span class="eco-caret">▸</span>
      <span class="eco-name">${esc(d.name)}</span>
      ${ecoGrid(d.grid, d.gridInferred)}
      <span class="eco-charge">${ecoChargeRounded(d)}</span>
      <span class="eco-remain">${ecoRemain(d.remainTimeMinutes)}</span>
    </summary>
    <dl>${ecoDeviceDetailRows(d)}</dl>
  </details>`;
}

function ecoflowSystemDevicesHtml(devices, openSerials) {
  const list = Array.isArray(devices) ? devices : [];
  if (!list.length) return '<p class="muted">No devices configured.</p>';
  const open = openSerials || new Set();
  return list.map((d) => ecoSysDeviceRow(d, open.has(d.serialNumber))).join('');
}

// --- EcoFlow: повна вкладка (renderEcoflow() у порталі, assets/www/
// index.html) - Connection + Overview-таблиця + картки пристроїв, "1:1 як на
// порталі" (запит користувача цієї сесії), не kv/badge-переказ, що був тут
// раніше. Той самий набір/порядок колонок і полів, що й портал.

function ecoBrokerText(data) { return data.brokerHost ? esc(data.brokerHost) + ':' + dash(data.brokerPort) : '-'; }
function ecoTransportText(data) { return data.viaProxy ? 'proxy (plain MQTT)' : 'direct (TLS)'; }
// heapLargestBlockBytes - лише в mqttStatusJson()/portalStatusJson()
// (WebEcoflowModule.cpp), не в кожній тестовій фікстурі - null дає "-", а не
// "NaN KB" (assertNoLeakedPlaceholders у test/render.test.js).
function ecoHeapText(data) {
  if (data.heapFreeBytes == null) return '-';
  const largest = data.heapLargestBlockBytes != null ? Math.round(data.heapLargestBlockBytes / 1024) + ' KB' : '-';
  return Math.round(data.heapFreeBytes / 1024) + ' KB free · ' + largest + ' largest block';
}

// Рядок Overview-таблиці - той самий перелік колонок, що #ecoflow-table
// порталу: #, Serial, Name, Status, Charge, Grid, Power/AC (лише широкий
// екран, .ecoflow-wide), Left, Age. .opt/.opt2/.ecoflow-wide - ті самі класи
// й пороги (560/900/1300px), що в порталі (CSS нижче в index.html).
function ecoTableRow(d, index) {
  return `<tr>
    <td class="opt2">${index}</td>
    <td class="opt2">${esc(d.serialNumber)}</td>
    <td>${esc(d.name || d.serialNumber)}</td>
    <td>${ecoPresence(d.presence)}</td>
    <td class="num">${ecoChargeRounded(d)}</td>
    <td>${ecoGrid(d.grid, d.gridInferred)}</td>
    <td class="ecoflow-wide">${ecoWatts(d.inputWatts, d.outputWatts)}</td>
    <td class="ecoflow-wide num">${ecoAcText(d)}</td>
    <td class="opt num">${ecoRemain(d.remainTimeMinutes)}</td>
    <td class="opt2 num">${d.gridForMs != null ? ecoDuration(d.gridForMs) : '-'}</td>
  </tr>`;
}

// Ключі вже нормалізовані на пристрої (EcoflowDeviceRegistry::normalizeKey) -
// esc() лишається обов'язковим: список формується з того, що прийшло
// мережею, довіряти йому не можна навіть у власному акаунті (буквальний порт
// ecoParamsTable(), assets/www/index.html).
function ecoParamsTable(params) {
  const keys = Object.keys(params).sort();
  if (keys.length === 0) return '<p class="muted">No raw parameters captured yet.</p>';
  const rows = keys.map((k) => `<tr><td>${esc(k)}</td><td>${params[k]}</td></tr>`).join('');
  return `<table><thead><tr><th>Key</th><th>Value</th></tr></thead><tbody>${rows}</tbody></table>`;
}

// Розділ "Raw parameters" картки - на відміну від порталу (params завжди в
// /api/ecoflow/status), MQTT-агрегат ecoflow-status (mqttStatusJson(),
// WebEcoflowModule.cpp) свідомо їх не несе (розмір payload, "Провайдер ≠
// форматер", docs/mqtt-web-handoff.md). Замість мовчазної відсутності -
// окремий per-device drill-down топік (docs/mqtt-topics.md,
// devices/<client-id>/api/ecoflow-params/<sn>): d.params з'являється лише
// ПІСЛЯ того, як index.html злив відповідь цього топіка в кешований
// пристрій (mergeEcoflowDeviceParams()) - render.js сам нічого не запитує
// (без DOM/мережі, той самий принцип, що й решта файлу). open - чи розгорнутий
// <details> ПЕРЕД цією перемальовкою (index.html сканує DOM, той самий
// патерн, що ecoSysOpenDetails/ecoflowSystemDevicesHtml вище).
function ecoParamsSection(d, open) {
  const sn = esc(d.serialNumber);
  if (d.params) {
    const count = Object.keys(d.params).length;
    const dropped = d.droppedParams > 0 ? `, ${d.droppedParams} dropped (per-device limit)` : '';
    // Та сама кнопка/клас, що на "не завантажено" нижче (.eco-params-btn) -
    // index.html делегує клік по класу, тому один слухач обслуговує і перше
    // завантаження, і повторний рефреш; без цієї кнопки d.params, щойно
    // з'явившись, лишалось б застарілим НАЗАВЖДИ (params на пристрої
    // змінюються, а MQTT-агрегат ecoflow-status їх свідомо не оновлює -
    // розділ вище).
    return `<details class="eco-params"${open ? ' open' : ''} data-sn="${sn}">
      <summary>Raw parameters (${count}${dropped}, capture: ${d.captureAll ? 'all' : 'important only'})</summary>
      <div class="eco-params-body">
        <button class="act ghost eco-params-btn" type="button" data-sn="${sn}" style="margin-bottom:8px">Refresh</button>
        ${ecoParamsTable(d.params)}
      </div>
    </details>`;
  }
  return `<details class="eco-params"${open ? ' open' : ''} data-sn="${sn}">
    <summary>Raw parameters (not loaded)</summary>
    <div class="eco-params-body">
      <p class="muted" style="margin:0 0 8px">Not sent with ecoflow-status (payload size) - fetched on demand.</p>
      <button class="act ghost eco-params-btn" type="button" data-sn="${sn}">Load raw parameters</button>
    </div>
  </details>`;
}

// Той самий словник, що ECO_DEVICE_IMAGE_SLUG у порталі (assets/www/
// index.html) - водяний знак фото за моделлю (data-model, CSS у sapi/
// index.html). Невідома модель - без data-model, картка без фото, а не з
// биткою заглушкою (той самий принцип, що й портал).
const ECO_DEVICE_IMAGE_SLUG = { 'DELTA 2': 'delta2', 'DELTA mini': 'delta-mini', 'DELTA Pro': 'delta-pro' };

// Картка пристрою - той самий перелік dt/dd, що ecoSysDeviceRow() (System),
// той самий ecoDeviceDetailRows() (DRY - CLAUDE.md), лише БЕЗ згорнутого
// стану верхнього рівня (тут завжди розгорнуто - повна вкладка, не
// компактний блок System). openParamsSerials - Set серійників із розгорнутим
// "Raw parameters" (index.html сканує DOM перед перемальовкою) - лише цей
// розділ картки має власний згорнутий стан.
function ecoDeviceCard(d, openParamsSerials) {
  const open = (openParamsSerials || new Set()).has(d.serialNumber);
  const imgSlug = ECO_DEVICE_IMAGE_SLUG[d.type] || '';
  const imgAttr = imgSlug ? ` data-model="${imgSlug}"` : '';
  return `<div class="card ecoflow-device" data-sn="${esc(d.serialNumber)}"${imgAttr}>
    <div class="row" style="justify-content:space-between">
      <b>${esc(d.name || d.serialNumber)}</b><span class="muted">${esc(d.serialNumber)} · ${esc(dash(d.type))}</span>
    </div>
    <dl>${ecoDeviceDetailRows(d)}</dl>
    ${ecoParamsSection(d, open)}
  </div>`;
}

// --- MQTT: буквальний порт portal (assets/www/index.html: hintIcon/
// mqttDeniedText/mqttCommandsRejectedText/mqttDroppedText/mqttDroppedRows/
// mqttLwtRows/mqttHeartbeatText/mqttHeartbeatRows/mqttConnectionRows/
// mqttConsoleMirrorRows/renderMqtt/renderMqttSystemSummary) - "1:1 як на
// порталі" (запит користувача цієї сесії). Портал-мапер dt/dd сам екранує
// текстові значення (третій елемент пари - "це вже розмітка, не екранувати
// вдруге") - dlRows()/kv() тут такого розрізнення не роблять (dd лишається
// на відповідальність викликача, як і в решті файлу), тому нижче esc()
// викликається явно на кожному текстовому полі, без третього елемента пари.

function hintIcon() { return '<span class="hint-mark">ⓘ</span>'; }

// subscribeDeniedCount > 0 - брокер відхилив підписку (ACL), клієнт лишається
// "connected", але мовчить (docs/tech_debt.md, "EcoFlow: відкликані ключі").
function mqttDeniedText(count) {
  if (!count) return '0';
  return `<span style="color:var(--err)">${count} - check ACL (docs/ecoflow_mqtt_proxy_setup.md)</span>`;
}
function mqttCommandsRejectedText(count) {
  if (!count) return '0';
  return `<span style="color:var(--err)">${count} - command queue was full</span>`;
}
// Компактний однорядковий варіант (System-картка - місця на два окремі рядки
// нема) - розбивка out/in у хінті (title).
function mqttDroppedText(out, incoming) {
  const total = (out || 0) + (incoming || 0);
  const hint = [
    'Not enough memory (heap) to send or receive a message,',
    'or the queue was full.',
    `Dropped out: ${out} message(s)`,
    `Dropped in: ${incoming} message(s)`,
  ].join('\n');
  const color = total > 0 ? ' style="color:var(--err)"' : '';
  return `<span title="${esc(hint)}"${color}>${total}${hintIcon()}</span>`;
}
// Два окремі рядки - для повної вкладки MQTT.
function mqttDroppedRows(out, incoming) {
  const text = (n) => n > 0 ? `<span style="color:var(--err)">${n} message(s)</span>` : '0 message(s)';
  return [
    ['Dropped out', text(out)],
    ['Dropped in', text(incoming)],
  ];
}
// Три окремі рядки - і для повної вкладки MQTT, і для System-картки, той
// самий формат в обох місцях.
function mqttLwtRows(lwt) {
  if (!lwt) return [['LWT', 'not configured']];
  return [
    ['LWT topic', esc(lwt.topic)],
    ['LWT online', esc(lwt.onlineMessage || '(none)')],
    ['LWT offline', esc(lwt.offlineMessage || '(none)')],
  ];
}
const kHeartbeatHint = 'Periodic keep-alive message published to the LWT topic between ' +
  'connects/disconnects, so a silent device is caught even without a network drop.';
// Компактний варіант - для System (LWT там теж три рядки, Heartbeat лишається згорнутим).
function mqttHeartbeatText(hb) {
  if (!hb || !hb.message) return `<span title="${esc(kHeartbeatHint)}">not configured${hintIcon()}</span>`;
  return `<span title="${esc(kHeartbeatHint)}">"${esc(hb.message)}" every ` +
    `${hb.intervalMs / 1000} s${hintIcon()}</span>`;
}
function mqttHeartbeatRows(hb) {
  if (!hb || !hb.message) return [['Heartbeat', 'not configured']];
  return [
    ['Heartbeat message', esc(hb.message)],
    ['Heartbeat interval', (hb.intervalMs / 1000) + ' s'],
  ];
}
// Спільний перелік dt/dd - і повна вкладка MQTT, і System-картка. LWT сюди
// навмисно не входить - додається окремо через mqttLwtRows() в обох місцях.
// droppedRows - параметр (вкладка/System показують Dropped по-різному), а не
// власний виклик усередині.
function mqttConnectionRows(data, droppedRows) {
  return [
    ['Connection', data.connected ? 'connected' : 'disconnected'],
    ['Broker', esc(dash(data.host)) + ':' + dash(data.port)],
    ['Security', data.security === 'tls' ? 'TLS' : 'plain'],
    ['Client ID', esc(dash(data.clientId))],
    ['Login', data.login == null ? '(anonymous)' : esc(data.login)],
    ['Topic prefix', esc(dash(data.topicPrefix))],
    ['Published / received', dash(data.publishedCount) + ' / ' + dash(data.receivedCount)],
    ...droppedRows,
    ['Subscribe denied', mqttDeniedText(data.subscribeDeniedCount)],
    ['Commands rejected', mqttCommandsRejectedText(data.commandsRejectedCount)],
  ];
}
function mqttConsoleMirrorRows(cm) {
  if (!cm || !cm.available) return [['Console mirror', 'not built into this firmware']];
  const rules = cm.allowRules.length + cm.denyRules.length;
  const rulesText = rules === 0 ? 'none (everything goes out)' :
    [...cm.allowRules.map((t) => 'allow ' + t), ...cm.denyRules.map((t) => 'deny ' + t)].join(', ');
  return [
    ['Mirror', cm.active ? 'on' : 'off'],
    ['Topic', esc(cm.topic)],
    ['Published', dash(cm.publishedCount)],
    ['Dropped (rate limit)', dash(cm.droppedByRateLimitCount)],
    ['Tag rules', esc(rulesText)],
  ];
}

// Повна вкладка MQTT (renderMqtt() у порталі) - Connection/.../Commands
// rejected + LWT (3 рядки) + Heartbeat (2 рядки, розгорнутий) окремим блоком,
// Console mirror окремим блоком нижче.
function renderMqttStatus(data) {
  const droppedRows = mqttDroppedRows(data.droppedOutgoingCount, data.droppedIncomingCount);
  const rows = mqttConnectionRows(data, droppedRows).concat(mqttLwtRows(data.lwt), mqttHeartbeatRows(data.heartbeat));
  return kv(rows) + '<div class="section-group"><h3>Console mirror</h3>' + kv(mqttConsoleMirrorRows(data.consoleMirror)) + '</div>';
}

// System-картка (renderMqttSystemSummary() у порталі) - той самий перелік,
// Dropped згорнутий в один рядок (mqttDroppedText), Console mirror - лише
// коли зібраний у прошивку (available), одним компактним рядком on/off.
function mqttSystemRows(data) {
  const droppedRow = [['Messages dropped', mqttDroppedText(data.droppedOutgoingCount, data.droppedIncomingCount)]];
  const rows = mqttConnectionRows(data, droppedRow).concat(mqttLwtRows(data.lwt));
  rows.push(['Heartbeat', mqttHeartbeatText(data.heartbeat)]);
  if (data.consoleMirror && data.consoleMirror.available) {
    rows.push(['Console mirror', data.consoleMirror.active ? 'on' : 'off']);
  }
  return rows;
}

// Той самий каркас карток, що System-вкладка порталу (assets/www/index.html,
// #sys-cards/.card): Device/Network/EcoFlow/MQTT/Memory/LittleFS/SD card/
// NVS/Modules + таблиця розділів під картками, той самий порядок карток.
//
// `extra.wifi`/`extra.ecoflow` - остання відома відповідь wifi-status/
// ecoflow-status (той самий формат, що й renderWifiStatus()/
// renderEcoflowStatus() нижче отримують), яку index.html передає сюди після
// БУДЬ-ЯКОЇ з трьох відповідей (система/wifi/ecoflow) - той самий принцип,
// що statusRows() у порталі малює і вкладку Wi-Fi, і System-картку з ОДНОГО
// запиту. Немає ще жодної відповіді на wifi-status/ecoflow-status в цій
// сесії SAPI - картка лишається mockDl() (форма готова, дані ще не
// приїхали, а не "нуль"), той самий контракт, що вже несе `data.portal`
// нижче для полів, яких system-info взагалі ще не носить.
//
// `extra.ecoflowOpenSerials` - Set серійників, чий <details> зараз
// розгорнутий (index.html сканує DOM ПЕРЕД перемальовкою - render.js сам
// без DOM); EcoFlow-картка тепер 1:1 з порталом - той самий підсумок
// (ecoflowSystemRows) + той самий компактний розгортний список пристроїв
// (ecoflowSystemDevicesHtml/ecoSysDeviceRow), не лише мок.
//
// `extra.mqtt` - остання відома відповідь mqtt-status (той самий принцип,
// що wifi/ecoflow вище) - MQTT-картка тепер теж 1:1 з порталом
// (mqttSystemRows), мок лише поки жодної відповіді ще не було.

// Бар використання heap (картка Memory) - НЕ порт порталу: портал сьогодні
// не має жодного bar/progress-віджета (лише текстові dl), це нова
// візуалізація за проханням користувача цієї сесії, специфічна для SAPI.
//
// Ширина бара = h.totalBytes. Зліва - Used (суцільний колір). Одразу за ним
// - Largest free block (яскравіший відтінок "вільного"): показує, скільки з
// вільного лежить ОДНИМ безперервним шматком. Решта смуги лишається фоном
// бара (var(--line), той самий "порожній трек" колір) - це вільне, побите
// на дрібніші шматки. Розрив між "Largest" і рештою смуги показує
// фрагментацію напряму, без окремого числа поруч (fragmentationPercent
// лишається в dl нижче як точне число - бар лише дає оком оцінити масштаб).
//
// Min free ever - НЕ окремий сегмент (другий бар плутав би, яка межа до
// чого - саме той сумнів, який user описав), а вертикальна позначка
// (маркер) на тій самій смузі: де проходила межа used/free в НАЙГІРШИЙ
// момент з часу старту пристрою. Відповідає на "наскільки близько
// підходили до вичерпання" одним поглядом, а не ще одним числом.
//
// Hover-tooltip на кожному розмірі (used/free/largest/min) окремо, НЕ один
// загальний title на весь бар - користувач прямо зазначив, що загальний
// незрозуміло як показувати (чотири числа в одному title нечитабельні).
// Тому "вільний фрагментований залишок" (сьогодні - просто фон бара) тут
// стає окремим <div> (heap-bar-free) - інакше йому нема на чому висіти
// власним title. Total - свідомо БЕЗ tooltip (сам користувач це виключив).
//
// Відкрите питання (навмисно не вирішене цієї сесії, за словами
// користувача): графік вільної пам'яті в часі - вимагає циклічного
// опитування (SAPI сам зберігає історію точок, а не пристрій) - записано в
// docs/mqtt-web-handoff.md, не реалізовано.
function heapBarHtml(h) {
  const total = h.totalBytes;
  if (!total) return ''; // ще нема даних (мок-стан) - бар без чисел не малюємо
  const used = Math.max(0, total - (h.freeBytes || 0));
  const usedPct = Math.min(100, (used / total) * 100);
  const largestBytes = Math.min(h.largestFreeBlockBytes || 0, total - used);
  const largestPct = Math.min(100 - usedPct, (largestBytes / total) * 100);
  const freeBytesTotal = Math.max(0, total - used);
  const fragmentedBytes = Math.max(0, freeBytesTotal - largestBytes);
  const fragmentedPct = Math.max(0, 100 - usedPct - largestPct);

  let markerHtml = '';
  if (h.minFreeEverBytes != null) {
    const markerPct = Math.min(100, Math.max(0, ((total - h.minFreeEverBytes) / total) * 100));
    markerHtml = `<div class="heap-bar-marker" style="left:${markerPct.toFixed(2)}%" ` +
      `title="Min free ever: ${esc(fmtBytes(h.minFreeEverBytes))} (most memory ever used at once since boot)"></div>`;
  }

  let freeHtml = '';
  if (fragmentedPct > 0) {
    freeHtml = `<div class="heap-bar-free" style="left:${(usedPct + largestPct).toFixed(2)}%;width:${fragmentedPct.toFixed(2)}%" ` +
      `title="Free: ${esc(fmtBytes(fragmentedBytes))} in smaller fragments (${esc(fmtBytes(freeBytesTotal))} free in total)"></div>`;
  }

  return `<div class="heap-bar">` +
      `<div class="heap-bar-used" style="width:${usedPct.toFixed(2)}%" ` +
        `title="Used: ${esc(fmtBytes(used))} of ${esc(fmtBytes(total))}"></div>` +
      `<div class="heap-bar-largest" style="left:${usedPct.toFixed(2)}%;width:${largestPct.toFixed(2)}%" ` +
        `title="Largest free block: ${esc(fmtBytes(largestBytes))} (largest single contiguous chunk)"></div>` +
      freeHtml +
      markerHtml +
    `</div>` +
    `<div class="heap-bar-legend">` +
      `<span><i class="sw sw-used"></i>Used</span>` +
      `<span><i class="sw sw-largest"></i>Largest free block</span>` +
      `<span><i class="sw sw-free"></i>Free (fragmented)</span>` +
      (h.minFreeEverBytes != null ? `<span><i class="sw sw-marker"></i>Min free ever</span>` : '') +
    `</div>`;
}

function renderSystemInfo(data, extra) {
  extra = extra || {};
  const c = data.chip || {}, h = data.heap || {}, f = data.flash || {}, n = data.nvs || {};
  // portal - той самий об'єкт, що й /api/status (WebPortal::statusJson(),
  // src/main.cpp: jsonApiSystemInfoExecute()) - властивості ПРИСТРОЮ, не
  // HTTP-каналу, тому реальні одразу, без прив'язки до окремої SAPI-команди.
  const portal = data.portal || null;

  const deviceTop = portal ? dlRows([
    ['Firmware env', esc(dash(portal.env))],
    ['Revision', esc(dash(portal.revision))],
    ['Uptime', fmtUptime(portal.uptimeMs)],
  ]) : mockDl(['Firmware env', 'Revision', 'Uptime']);
  const deviceHeap = dlRows([['Free heap', fmtBytes(h.freeBytes)]]);
  const deviceRest = portal ? dlRows([
    ['Portal auth', boolBadge(portal.auth, 'enabled', 'disabled')],
    ['Pending jobs', dash(portal.pendingJobs)],
  ]) : mockDl(['Portal auth', 'Pending jobs']);
  const deviceFlash = dlRows([
    ['Chip model', esc(dash(c.model)) + (c.revision != null ? ` (rev ${c.revision})` : '')],
    ['CPU', `${dash(c.cores)} core${c.cores === 1 ? '' : 's'} @ ${c.cpuFreqMHz ?? '-'} MHz`],
    ['PSRAM', c.psramFound ? fmtBytes(c.psramBytes) : 'not present'],
    ['Flash size', fmtBytes(f.sizeBytes)],
    ['Flash speed', f.speedHz ? (f.speedHz / 1e6).toFixed(0) + ' MHz' : '-'],
  ]);
  let html = '<div class="cards">';
  // Один <dl>, не два (портал розводить #sys-device/#sys-device-flash - але
  // там причина в РІЗНІЙ частоті оновлення: перший - на кожен тік, другий -
  // лише за Refresh). Тут обидва шматки приїжджають ОДНИМ system-info-запитом
  // і рендеряться одним викликом - двох незалежних CSS-grid (кожен dl рахує
  // свою колонку max-content окремо) без причини лишало враження "двох різних
  // блоків", а не однієї картки.
  html += `<div class="card"><h2>Device</h2><dl>${deviceTop}${deviceHeap}${deviceRest}${deviceFlash}</dl></div>`;

  html += '<div class="card"><h2>Network</h2><dl>' + (extra.wifi
    ? dlRows(wifiSystemRows(extra.wifi))
    : mockDl(['State', 'SSID', 'IP address', 'Gateway', 'Signal', 'Connection type', 'MAC', 'Auto reconnect', 'Hotspot'])
  ) + '</dl></div>';

  {
    const eco = extra.ecoflow;
    const devices = eco && Array.isArray(eco.devices) ? eco.devices : [];
    const countLabel = eco ? `<span class="muted" style="font-size:.75em; font-weight:400">${devices.length} device(s)</span>` : '';
    html += `<div class="card"><h2>EcoFlow ${countLabel}</h2><dl>` + (eco
      ? dlRows(ecoflowSystemRows(eco))
      : mockDl(['Connection', 'Broker', 'Connection type', 'Devices', 'Messages received'])
    ) + '</dl>' + (eco
      ? `<div class="sys-eco-devices">${ecoflowSystemDevicesHtml(devices, extra.ecoflowOpenSerials)}</div>`
      : '') + '</div>';
  }

  html += '<div class="card"><h2>MQTT</h2><dl>' + (extra.mqtt
    ? dlRows(mqttSystemRows(extra.mqtt))
    : mockDl(['Connection', 'Broker', 'Security', 'Client ID', 'Login', 'Topic prefix',
        'Published / received', 'Messages dropped', 'Subscribe denied', 'Commands rejected',
        'LWT topic', 'LWT online', 'LWT offline', 'Heartbeat', 'Console mirror'])
  ) + '</dl></div>';

  const memUnit = fsUnit(h.totalBytes || h.freeBytes || 1);
  const freePct = h.totalBytes > 0 ? Math.round((h.freeBytes / h.totalBytes) * 100) : null;
  html += '<div class="card"><h2>Memory</h2><dl>' + dlRows([
    ['Total', h.totalBytes ? fsSizeIn(h.totalBytes, memUnit) : '-'],
    ['Free', fsSizeIn(h.freeBytes, memUnit) + (freePct != null ? ` (${freePct}%)` : '')],
    ['Largest free block', fsSizeIn(h.largestFreeBlockBytes, memUnit)],
    ['Fragmentation', (h.fragmentationPercent ?? '-') + '%'],
    ['Min free ever', h.minFreeEverBytes ? fsSizeIn(h.minFreeEverBytes, memUnit) : '-'],
  ]) + '</dl>' + heapBarHtml(h) + '</div>';

  html += `<div class="card"><h2>LittleFS</h2><dl>${mockDl(['Total', 'Used', 'Free'])}</dl></div>`;
  html += `<div class="card"><h2>SD card</h2><dl>${mockDl(['Type', 'Total', 'Used', 'Free'])}</dl></div>`;

  html += '<div class="card"><h2>NVS</h2><dl>' + (n.available ? dlRows((() => {
    const pct = n.totalEntries > 0 ? Math.round((n.usedEntries / n.totalEntries) * 100) : 0;
    return [
      ['Total', n.totalEntries.toLocaleString()],
      ['Used', `${n.usedEntries.toLocaleString()} (${pct}%)`],
      ['Free', `${n.freeEntries.toLocaleString()} (${100 - pct}%)`],
      ['Namespaces', dash(n.namespaceCount)],
    ];
  })()) : '<dt>NVS</dt><dd class="muted">not available</dd>') + '</dl></div>';

  html += '<div class="card"><h2>Modules</h2>' + (portal
    ? (portal.modules && portal.modules.length
        ? `<p>${portal.modules.map(esc).join(', ')}</p>`
        : '<p class="muted">-</p>')
    : '<p class="muted">***</p>'
  ) + '</div>';
  html += '</div>'; // .cards

  // Звичайний <h2> (як card-заголовки вище), не .section-group h3 - портал
  // теж дає "FLASH" той самий великий uppercase-кегль, що й Device/Memory/...,
  // а не приглушений дрібний підзаголовок.
  html += '<h2>Flash partitions</h2>';
  if (Array.isArray(data.partitions) && data.partitions.length) {
    const hexLen = Math.max(0, ...data.partitions.flatMap((p) => [p.offset, p.size].map((x) => (x >>> 0).toString(16).length)));
    const hex = (x) => '0x' + (x >>> 0).toString(16).padStart(hexLen, '0');
    html += '<table class="zebra"><thead><tr>'
      + '<th>Label</th><th class="num">Size</th><th>Type</th><th class="opt">Subtype</th>'
      + '<th class="num opt mono">Offset</th><th class="num opt mono">Length</th></tr></thead><tbody>'
      + data.partitions.map((p) => `<tr><td>${esc(p.label)}</td><td class="num">${fmtBytes(p.size)}</td>`
        + `<td>${esc(p.type)}</td><td class="opt">${esc(p.subtype)}</td>`
        + `<td class="num opt mono">${hex(p.offset)}</td><td class="num opt mono">${hex(p.size)}</td></tr>`).join('')
      + '</tbody></table>';
  } else {
    html += '<p class="muted">No partition table.</p>';
  }
  return html;
}

// Рядок таблиці "Saved profiles" - буквальний порт renderProfiles() з
// порталу (assets/www/index.html): та сама таблиця, ті самі
// badge/бейджі-текст (open/static/connected), лише БЕЗ останньої колонки
// (Connect + ▾-меню Edit/Forget), лише сама кнопка "Connect" - і та
// заблокована (disabled): SAPI поки не має жодної команди, що міняє стан
// пристрою (докладніше - sapi/README.md, розділ "Пре-альфа"). Без хоч якоїсь
// кнопки остання колонка лишалась би геть порожньою, і без цього "якоря"
// праворуч таблиця (без .num на Signal/State) розповзається на всю ширину
// .col порожніми проміжками - саме те, що user описав як "стисла" таблиця.
// title - та сама причина, що показана в тексті: коли з'явиться мутуючий
// канал на MQTT, кнопка й розблокується (не інша розмітка).
const kWifiConnectDisabledHint = 'Not implemented yet - SAPI is read-only (mutations over MQTT are planned)';
function wifiProfileRow(c) {
  return `<tr class="${c.active ? 'active' : ''}">
    <td>${ssidLabel(c.ssid)}${c.hasPassword ? '' : ' <span class="muted">(open)</span>'}
        ${c.staticIp ? ' <span class="muted">static</span>' : ''}
        ${c.active ? ' <span style="color:var(--ok)">connected</span>' : ''}</td>
    <td class="num opt">${dash(c.priority)}</td>
    <td class="opt">${c.rssi ? signal(c.rssi, c.quality) : '<span class="muted">not in range</span>'}</td>
    <td>${c.enabled ? 'enabled' : '<span class="muted">disabled</span>'}</td>
    <td class="num"><button class="act ghost" disabled title="${esc(kWifiConnectDisabledHint)}">Connect</button></td>
  </tr>`;
}

// connections === null/undefined - відповідь 'wifi-connections' ще не
// приходила цієї сесії (той самий "форма готова, дані ще не приїхали", що
// mockDl() в renderSystemInfo), а не порожній масив (= пристрій справді без
// збережених профілів, "No saved profiles." порталу).
function wifiProfilesTable(connections) {
  const header = '<thead><tr><th>SSID</th><th class="opt">Priority</th>'
    + '<th class="opt">Signal</th><th>State</th><th></th></tr></thead>';
  if (!Array.isArray(connections)) {
    return `<table>${header}<tbody><tr><td colspan="5" class="muted">Not loaded yet.</td></tr></tbody></table>`;
  }
  if (connections.length === 0) {
    return `<table>${header}<tbody><tr><td colspan="5" class="muted">No saved profiles.</td></tr></tbody></table>`;
  }
  return `<table>${header}<tbody>${connections.map(wifiProfileRow).join('')}</tbody></table>`;
}

// Повна вкладка Wi-Fi (renderStatus()+renderProfiles() у порталі) - "1:1 як
// на порталі" (запит користувача цієї сесії), лише контент-частина: без
// Actions (Scan/Reconnect/Hotspot) і без форми нового профілю - усе це
// мутації, яких SAPI поки не робить (sapi/README.md, "Пре-альфа"), тому й
// "Networks in range" тут немає - той список наповнює лише сама дія Scan.
// STATUS - той самий wifiSystemRows(), що вже малює System-картку Network
// (DRY, CLAUDE.md): портал теж малює обидва місця з ОДНОГО /api/wifi/status
// (коментар statusRows() у порталі), тому другий словник рядків тут не
// заводимо.
//
// extra.connections - остання відома відповідь 'wifi-connections' (окремий
// запит, той самий принцип, що extra.journalData в renderEcoflowStatus) -
// index.html передає її сюди після кожної відповіді wifi-connections;
// undefined, поки жодної ще не було в цій сесії.
//
// .cols/.col - буквальний порт двоколонкового каркасу вкладки Wi-Fi з
// порталу (assets/www/index.html: Status зліва, Saved profiles справа, CSS -
// sapi/index.html): без цього таблиця профілів розтягувалась на всю ширину
// .out (портал показує її лише в правій половині) - звідси й "стисла" на
// вигляд таблиця з величезними проміжками між Priority/Signal/State
// (жодна з колонок, крім Priority, не має width:1%, і порожньому місцю
// нема на чому зупинитись).
function renderWifiStatus(data, extra) {
  extra = extra || {};
  const statusHtml = '<h2>Status</h2><dl>' + dlRows(wifiSystemRows(data)) + '</dl>';
  const profilesHtml = '<h2>Saved profiles</h2>' + wifiProfilesTable(extra.connections);
  return '<div class="cols"><div class="col">' + statusHtml + '</div>'
    + '<div class="col">' + profilesHtml + '</div></div>';
}

// Рядок таблиці Journal (окремий запит 'ecoflow-journal', НЕ розширення
// ecoflow-status - той самий провайдер, що на порталі, EcoflowJournalView.hpp).
// Finish = Start + Age - похідний момент (сервер рахує лише "скільки
// тривало", ageSec, не "коли завершилось" - EcoflowJournalView.hpp). Дата
// збігається зі Start - лише час (дата вже видна в сусідній колонці); не
// збігається - час плюс "(+XXd)", різниця КАЛЕНДАРНИХ дат (місцева північ,
// Date.UTC(рік,місяць,день) для обох міток - не ділення різниці секунд на
// 86400, яке з переходом через північ дало б хибне число). toLocaleTimeString(),
// не свій формат (портал: ecoFinishText(), assets/www/index.html) - той
// самий вибір, що вже в fmtEpoch() вище.
function ecoFinishText(startEpoch, ageSec) {
  const start = new Date(startEpoch * 1000);
  const finish = new Date((startEpoch + ageSec) * 1000);
  const time = finish.toLocaleTimeString();
  const startDay = Date.UTC(start.getFullYear(), start.getMonth(), start.getDate());
  const finishDay = Date.UTC(finish.getFullYear(), finish.getMonth(), finish.getDate());
  if (startDay === finishDay) return time;
  return `${time} (+${Math.round((finishDay - startDay) / 86400000)}d)`;
}

// row.mark - '>'/'<'/'' - той самий сенс, що в serial-команді
// 'ecoflow-journal show': смуга зліва (CSS) замість односимвольної колонки,
// плюс текстова приписка - колір сам собою непомітний для дальтоніків.
// Колір смуги йде за row.grid (ecoGridColorVar) - поточний стан на grid
// зелений, поза grid жовтий; попередній - той самий колір приглушений.
//
// selectedDevice - обраний кліком пристрій (index.html: scan #eco-journal-
// -wrap ПЕРЕД перемальовкою, той самий патерн, що journalTarget/journalOpen
// нижче) - на відміну від порталу (там клас .selected/.muted додає DOM-
// -мутація після рендеру), тут увесь панель будується заново з data щоразу,
// тому клас одразу в розмітці рядка, а не окремим проходом по DOM.
function ecoJournalRow(row, selectedDevice) {
  const stripe = row.mark === '>' ? ecoGridColorVar(row.grid, false)
    : row.mark === '<' ? ecoGridColorVar(row.grid, true) : '';
  const hint = row.mark === '>' ? '<span class="muted"> (ongoing)</span>'
    : row.mark === '<' ? '<span class="muted"> (closed)</span>' : '';
  const style = stripe ? ` style="--eco-stripe:${stripe}"` : '';
  const cls = !selectedDevice ? '' : row.device === selectedDevice ? ' class="selected"' : ' class="muted"';
  return `<tr${style}${cls} data-eco-device="${esc(row.device)}">
    <td>${esc(fmtEpoch(row.atEpoch))}</td>
    <td>${esc(ecoFinishText(row.atEpoch, row.ageSec))}</td>
    <td>${esc(row.device)}</td>
    <td>${ecoGrid(row.grid, false)}${hint}</td>
    <td class="num">${ecoDuration(row.ageSec * 1000)}</td>
  </tr>`;
}

// devices - для опцій "Device" (той самий знімок ecoflow-status, свого
// запиту під список не заводимо, DRY). journalData - остання відповідь
// 'ecoflow-journal' (index.html: lastEcoflowJournal, той самий принцип, що
// lastEcoflowStatus) - null, поки жодної ще не приходило. target/auto/open/
// newest - зі сканування DOM ПЕРЕД перемальовкою (index.html:
// renderEcoflowPanel()), той самий патерн, що openParamsSerials/devicesOpen
// вище. Сервер віддає rows хронологічно (найстаріший спочатку,
// EcoflowJournalView.cpp) - newest лише реверсить копію масиву перед map():
// row.mark лишається властивістю самого рядка (не позиції), reverse() тут
// нічого не ламає.
function ecoJournalSection(devices, journalData, target, auto, open, newest, selectedDevice) {
  const options = `<option value="all"${!target || target === 'all' ? ' selected' : ''}>All devices</option>` +
    devices.map((d) => `<option value="${esc(d.serialNumber)}"${d.serialNumber === target ? ' selected' : ''}>`
      + `${esc(d.name || d.serialNumber)}</option>`).join('');
  const rows = journalData && Array.isArray(journalData.rows)
    ? (newest ? [...journalData.rows].reverse() : journalData.rows)
    : null;
  const tbody = rows === null
    ? '<tr><td colspan="5" class="muted">Expand to load…</td></tr>'
    : rows.length === 0
      ? '<tr><td colspan="5" class="muted">No transitions recorded yet.</td></tr>'
      : rows.map((row) => ecoJournalRow(row, selectedDevice)).join('');
  const count = rows === null ? '' : rows.length + ' row(s)';
  // data-eco-selected - той самий атрибут, що читає index.html (renderEcoflowPanel():
  // скан DOM ПЕРЕД innerHTML=) ПЕРЕД записом сюди: без цього рядка кожна
  // перемальовка (ecoflow-status/ecoflow-journal тик, auto чи ручний) повертала
  // б wrap БЕЗ атрибута, і наступний скан бачив би "нічого не обрано" -
  // виділення губилось би вже на другому рендері після кліку.
  const selectedAttr = selectedDevice ? ` data-eco-selected="${esc(selectedDevice)}"` : '';
  return `<details id="eco-journal-wrap"${open ? ' open' : ''}${selectedAttr}>`
    + '<summary>Journal '
    + `<span class="muted" style="font-size:.75em; font-weight:400">${count}</span></summary>`
    + '<div id="eco-journal-block">'
    + '<div class="row" style="justify-content:space-between">'
    + `<label class="row">Device: <select id="eco-journal-target">${options}</select></label>`
    + '<span class="row">'
    + `<label class="row"><input type="checkbox" id="eco-journal-auto"${auto ? ' checked' : ''}> auto</label>`
    + `<label class="row"><input type="checkbox" id="eco-journal-newest-first"${newest ? ' checked' : ''}> newest first</label>`
    + '<button class="act ghost" id="eco-journal-refresh" type="button" title="Refresh journal">Refresh</button>'
    + '</span></div>'
    + '<table class="zebra" id="eco-journal-table"><thead><tr>'
    + '<th>Start</th><th>Finish</th><th>Device</th><th>Grid</th><th class="num">Age</th>'
    + `</tr></thead><tbody>${tbody}</tbody></table></div></details>`;
}

// extra.openParamsSerials - Set серійників із розгорнутим "Raw parameters"
// (index.html сканує DOM ПЕРЕД перемальовкою, той самий патерн, що
// extra.ecoflowOpenSerials у renderSystemInfo()) - той самий необов'язковий
// другий аргумент, що вже несуть RENDERERS['system-info']/['wifi-status'].
function renderEcoflowStatus(data, extra) {
  const devices = Array.isArray(data.devices) ? data.devices : [];
  const openParamsSerials = (extra && extra.openParamsSerials) || new Set();
  const devicesOpen = !extra || extra.devicesOpen !== false;
  const journalData = (extra && extra.journalData) || null;
  const journalTarget = (extra && extra.journalTarget) || 'all';
  const journalAuto = !!(extra && extra.journalAuto);
  const journalOpen = !!(extra && extra.journalOpen);
  // Дефолт - true (як і log-newest-first): щойно розгорнутий блок одразу
  // показує найновіший перехід зверху, без гортання вниз по історії.
  const journalNewest = !extra || extra.journalNewest !== false;
  const journalSelectedDevice = (extra && extra.journalSelectedDevice) || null;

  let html = kv([
    ['Connected', data.connected ? 'yes' : 'no'],
    ['MQTT session', data.running ? 'running' : 'stopped'],
    ['Channel', esc(dash(data.channel))],
    ['Broker', ecoBrokerText(data)],
    ['Connection type', ecoTransportText(data)],
    ['Account', esc(data.account || '-')],
    ['Messages received', dash(data.messageCount)],
    ['Last topic', esc(data.lastTopic || '-')],
    ['Last error', esc(data.lastError || '-')],
    ['Heap', ecoHeapText(data)],
    ['Net task stack headroom', dash(data.netStackHeadroomBytes) + ' B'],
  ]);

  // Звичайний <h2>, не .section-group h3 - той самий портал (assets/www/
  // index.html: <h2>Overview</h2>/<summary>Devices...), не приглушений
  // дрібний підзаголовок (той самий принцип, що Flash partitions вище).
  html += '<h2>Overview</h2><table class="zebra ecoflow-table"><thead><tr>'
    + '<th class="opt2">#</th><th class="opt2">Serial</th><th>Name</th><th>Status</th>'
    + '<th class="num">Charge</th><th>Grid</th>'
    + '<th class="ecoflow-wide">Power</th><th class="ecoflow-wide num">AC</th>'
    + '<th class="opt num">Left</th><th class="opt2 num">Age</th>'
    + '</tr></thead><tbody>'
    + (devices.length === 0
        ? '<tr><td colspan="10" class="muted">No devices configured.</td></tr>'
        : devices.map(ecoTableRow).join(''))
    + '</tbody></table>';

  // <details>/<summary> - той самий трикутник розкриття, що портал
  // (#ecoflow-devices-wrap, assets/www/index.html), не голий <h2>: заголовок
  // сам є перемикачем згорнути/розгорнути картки. "open" - зі скану DOM
  // ПЕРЕД цією перемальовкою (index.html: renderEcoflowPanel()), той самий
  // патерн, що openParamsSerials вище.
  html += `<details id="ecoflow-devices-wrap"${devicesOpen ? ' open' : ''}>`
    + '<summary>Devices '
    + `<span class="muted" style="font-size:.75em; font-weight:400">${devices.length} device(s)</span></summary>`
    + (devices.length === 0
        ? '<p class="muted">No devices configured.</p>'
        : '<div class="cards">' + devices.map((d) => ecoDeviceCard(d, openParamsSerials)).join('') + '</div>')
    + '</details>';

  // Окремий запит (НЕ розширення ecoflow-status) - той самий принцип, що на
  // порталі: журнал переходів рідше потрібен одразу, тому свій <details>,
  // згорнутий за замовчуванням.
  html += ecoJournalSection(devices, journalData, journalTarget, journalAuto, journalOpen, journalNewest,
    journalSelectedDevice);
  return html;
}

const RENDERERS = {
  'system-info': renderSystemInfo,
  'wifi-status': renderWifiStatus,
  'ecoflow-status': renderEcoflowStatus,
  'mqtt-status': renderMqttStatus,
};

// UMD-подібний хвіст: у браузері (класичний <script>, без type="module")
// module не існує - гілка пропускається, і всі function-декларації вище
// лишаються глобальними для наступного <script>, як і раніше. У Node
// (require() з test/render.test.js) - експортуємо явно.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    esc, badge, boolBadge, signal, kv, dlRows, mockDl, dash, fmtBytes, fmtUptime, fmtEpoch, fsUnit, fsSizeIn,
    ssidLabel, wifiSystemRows, wifiProfileRow, wifiProfilesTable,
    ecoflowSystemRows, ecoflowSystemDevicesHtml, ecoSysDeviceRow, ecoDeviceDetailRows,
    ecoBrokerText, ecoTransportText, ecoTableRow, ecoDeviceCard, ecoParamsTable,
    ecoJournalRow, ecoJournalSection, ecoFinishText,
    mqttSystemRows, mqttConnectionRows, mqttLwtRows, mqttConsoleMirrorRows, heapBarHtml,
    renderSystemInfo, renderWifiStatus, renderEcoflowStatus, renderMqttStatus, RENDERERS,
  };
}
