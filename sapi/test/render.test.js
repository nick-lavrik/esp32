// Headless regression-тест для sapi/render.js - жодного браузера/DOM, лише
// текстовий JS. Мета: зловити регресію в render*() при зміні коду або зміні
// форми JSON на боці плати (src/main.cpp kJsonApi*), не відкриваючи Chrome.
//
// Дані нижче - реальна форма з живого тесту в браузері (esp32-c3, rpi5,
// docs/mqtt-web-handoff.md) плюс крайові випадки (відсутній ap, порожні
// devices, unavailable nvs, null-поля). Без залежностей (без jest/mocha) -
// docs/tech_debt.md, "Автотести" - у проєкті ще немає test-раннера, тому
// найдешевший старт - голий Node + assert.
//
// Запуск: node sapi/test/render.test.js

'use strict';
const assert = require('node:assert/strict');
const { renderSystemInfo, renderWifiStatus, renderEcoflowStatus, renderMqttStatus,
        ecoflowSystemDevicesHtml, ecoTableRow, mqttSystemRows, heapBarHtml, ecoParamsTable,
        ecoJournalRow, ecoJournalSection, ecoFinishText, ssidLabel, wifiProfilesTable,
        usageBarHtml, renderFsList,
        renderNvsList, nvsTableParts, hexToBytes, nvsAsJson,
        fsPreviewFromBytes, hexDump, b64ToBytes, concatBytes, fsIsTextual } = require('../render.js');

const CHIP_FIXTURE = {
  chip: { model: 'ESP32-C3', revision: 4, cores: 1, cpuFreqMHz: 160, psramFound: false, psramBytes: 0 },
  heap: { totalBytes: 278272, freeBytes: 86400, largestFreeBlockBytes: 45056, minFreeEverBytes: 15360, fragmentationPercent: 32 },
  flash: { sizeBytes: 4194304, speedHz: 80000000 },
  nvs: { available: true, usedEntries: 12, freeEntries: 988, totalEntries: 1000, namespaceCount: 3 },
  partitions: [],
};

const cases = [];
function test(name, fn) { cases.push({ name, fn }); }

function assertNoLeakedPlaceholders(html) {
  assert.doesNotMatch(html, /undefined/, 'вивід містить "undefined" - десь не підставлене поле');
  assert.doesNotMatch(html, /\bNaN\b/, 'вивід містить NaN - зламана числова формула');
}

// --- system-info ---

test('system-info: щасливий шлях (esp32-c3, з партиціями)', () => {
  const html = renderSystemInfo({
    chip: { model: 'ESP32-C3', revision: 4, cores: 1, cpuFreqMHz: 160, psramFound: false, psramBytes: 0 },
    heap: { totalBytes: 278272, freeBytes: 86400, largestFreeBlockBytes: 45056, minFreeEverBytes: 15360, fragmentationPercent: 32 },
    flash: { sizeBytes: 4194304, speedHz: 80000000 },
    nvs: { available: true, usedEntries: 12, freeEntries: 988, totalEntries: 1000, namespaceCount: 3 },
    partitions: [
      { label: 'nvs', type: 'data', subtype: 'nvs', offset: 36864, size: 24576, encrypted: false },
      { label: 'app', type: 'app', subtype: 'factory', offset: 65536, size: 2097152, encrypted: false },
    ],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /ESP32-C3/);
  assert.match(html, /84\.4 KB/); // free heap: 86400 / 1024
  assert.match(html, /4\.00 MB/); // flash size: 4194304 / 1024 / 1024
  assert.match(html, /<table class="zebra">/);
  assert.match(html, /0x009000/); // 36864 у hex, з ведучими нулями до довжини найбільшого офсету/розміру в таблиці
  assert.match(html, /class="muted">\*\*\*/); // поля, яких ще немає в system-info (Firmware env, LittleFS, ...)
});

test('system-info: PSRAM є, NVS недоступний, без партицій', () => {
  const html = renderSystemInfo({
    chip: { model: 'ESP32-S3', revision: 0, cores: 2, cpuFreqMHz: 240, psramFound: true, psramBytes: 8388608 },
    heap: { totalBytes: 320000, freeBytes: 200000, largestFreeBlockBytes: 180000, minFreeEverBytes: 90000, fragmentationPercent: 5 },
    flash: { sizeBytes: 8388608, speedHz: 80000000 },
    nvs: { available: false },
    partitions: [],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /8\.00 MB/); // PSRAM bytes
  assert.match(html, /not available/); // NVS.available=false
  assert.doesNotMatch(html, /<table class="zebra">/); // порожній масив - таблиці нема, лише "No partition table."
  assert.match(html, /No partition table\./);
});

test('system-info: portal-поле присутнє - Device/Modules без "***"', () => {
  const html = renderSystemInfo({
    ...CHIP_FIXTURE,
    portal: { env: 'esp32-c3', revision: 'a1b2c3d', uptimeMs: 3725000, freeHeap: 86400,
              auth: true, pendingJobs: 2, modules: ['wifi', 'console', 'system'] },
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /esp32-c3/);
  assert.match(html, /a1b2c3d/);
  assert.match(html, /badge ok">enabled/); // Portal auth
  assert.match(html, /wifi, console, system/); // Modules
  assert.match(html, /Uptime<\/dt><dd>01:02:05/); // fmtUptime(3725000) - < доби, без днів
  assert.match(html, /Pending jobs<\/dt><dd>2/);
});

test('system-info: uptime > доби - "XXd hh:mm:ss"', () => {
  const html = renderSystemInfo({
    ...CHIP_FIXTURE,
    portal: { env: 'esp32-c3', revision: 'a1b2c3d', uptimeMs: (2 * 86400 + 3 * 3600 + 4 * 60 + 5) * 1000,
              freeHeap: 86400, auth: true, pendingJobs: 0, modules: ['wifi'] },
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /Uptime<\/dt><dd>2d 03:04:05/);
});

const ECO_DEVICE_FIXTURE = {
  serialNumber: 'R331ZEB4ZEBW0026', name: 'DELTA 2', type: 'DELTA 2',
  presence: 'online', online: true, messageCount: 67113, ageMs: 1200,
  lastMessageEpoch: 1758649843, socPercent: 100, socPrecise: 99.7,
  grid: 'on-grid', gridInferred: false, gridForMs: 123456789, gridChangeCount: 3,
  acInputMilliVolts: 230000, acInputFrequency: 50, inputWatts: 70, outputWatts: 57,
  remainTimeMinutes: 5938, snapshotAvailable: true, captureAll: false, droppedParams: 0,
};

test('system-info: extra.wifi/extra.ecoflow - Network/EcoFlow без моку', () => {
  const html = renderSystemInfo(CHIP_FIXTURE, {
    wifi: { state: 'connected', connected: true, ssid: 'STARLINK', ip: '192.168.1.25',
            gateway: '192.168.1.1', rssi: -68, quality: 64, phyMode: '802.11n (HT20)',
            mac: '9C:CC:01:7C:FD:90', autoReconnect: true, ap: { active: false } },
    ecoflow: { connected: true, brokerHost: '192.168.1.22', brokerPort: 1883, viaProxy: true,
               messageCount: 67113,
               devices: [ECO_DEVICE_FIXTURE, { ...ECO_DEVICE_FIXTURE, serialNumber: 'SN2', presence: 'offline' }] },
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /STARLINK/);
  assert.match(html, /class="sig">/); // Signal - смужки (signal()), не текст
  assert.match(html, /-68 <span class="u">dBm/);
  assert.match(html, /class="q">64%/);
  // Роздільник " · " - CSS ::after (.eco-counts span:not(:last-child)), не текст у розмітці.
  assert.match(html, /<span class="eco-counts"><span>1 online<\/span><span>1 offline<\/span><\/span>/);
  assert.match(html, /192\.168\.1\.22:1883/);
  // Компактний розгортний список пристроїв - 1:1 з порталом (ecoSysDeviceRow)
  assert.match(html, /class="sys-eco-devices"/);
  assert.match(html, /class="sys-eco-device"[^>]*data-sn="R331ZEB4ZEBW0026"/);
  assert.match(html, /class="eco-caret">▸/);
  assert.match(html, /class="eco-name">DELTA 2/);
  assert.match(html, /class="eco-charge">100%/);
});

test('system-info: extra.ecoflow без пристроїв - "No devices configured."', () => {
  const html = renderSystemInfo(CHIP_FIXTURE, {
    ecoflow: { connected: false, brokerHost: '', brokerPort: null, viaProxy: false, messageCount: 0, devices: [] },
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /No devices configured\./);
  assert.doesNotMatch(html, /class="sys-eco-device"/);
});

test('ecoflowSystemDevicesHtml: openSerials позначає лише свій пристрій "open"', () => {
  const html = ecoflowSystemDevicesHtml(
    [ECO_DEVICE_FIXTURE, { ...ECO_DEVICE_FIXTURE, serialNumber: 'SN2' }],
    new Set(['SN2']));
  assert.match(html, /class="sys-eco-device" data-sn="R331ZEB4ZEBW0026"/); // не open
  assert.match(html, /class="sys-eco-device" open data-sn="SN2"/); // open
});

// --- wifi-status: повна вкладка (renderWifiStatus, "1:1 як на порталі" -
// Status dl (wifiSystemRows(), той самий, що System > Network) + Saved
// profiles table (wifiProfilesTable()), БЕЗ Actions/Networks in range/форми
// профілю - ці мутують стан пристрою, а SAPI поки лише читає) ---

const WIFI_STATUS_FIXTURE = {
  state: 'connected', connected: true, ssid: 'STARLINK', ip: '192.168.1.25', gateway: '192.168.1.1',
  rssi: -68, quality: 64, phyMode: '802.11n (HT20)', mac: '9C:CC:01:7C:FD:90', autoReconnect: true,
  ap: { active: false, ssid: 'ESP-esp32-c3', ip: '192.168.4.1', clients: 0, security: 'open' },
};

test('wifi-status: підключено, extra.connections ще не приходили - "Not loaded yet."', () => {
  const html = renderWifiStatus(WIFI_STATUS_FIXTURE);
  assertNoLeakedPlaceholders(html);
  assert.match(html, /STARLINK/);
  assert.match(html, /class="sig">/); // Signal - смужки (signal()), connected=true
  assert.match(html, /Saved profiles/);
  assert.match(html, /Not loaded yet\./);
  // Двоколонковий каркас (.cols/.col) - Status зліва, Saved profiles справа
  // (буквальний порт .cols/.col порталу, CSS sapi/index.html) - без нього
  // таблиця профілів лягає на всю ширину .out і виглядає "розтягнутою".
  assert.match(html, /^<div class="cols"><div class="col"><h2>Status<\/h2>/);
  assert.match(html, /<div class="col"><h2>Saved profiles<\/h2>/);
});

test('wifi-status: extra.connections=[] - "No saved profiles." (не "Not loaded yet.")', () => {
  const html = renderWifiStatus(WIFI_STATUS_FIXTURE, { connections: [] });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /No saved profiles\./);
  assert.doesNotMatch(html, /Not loaded yet\./);
});

test('wifi-status: не підключено, ap відсутній - Hotspot "off", без смужок сигналу', () => {
  const html = renderWifiStatus({
    state: 'idle', connected: false, ssid: '', ip: '', gateway: '',
    rssi: null, quality: null, phyMode: '', mac: '9C:CC:01:7C:FD:90', autoReconnect: false,
    ap: null,
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /Hotspot<\/dt><dd>off/);
  assert.doesNotMatch(html, /class="sig">/); // connected=false - прочерк, не смужки
});

test('wifi-status: extra.connections - профіль active/open/static у таблиці, буквальний рядок порталу', () => {
  const html = renderWifiStatus(WIFI_STATUS_FIXTURE, {
    connections: [
      { id: 1, ssid: 'STARLINK', hasPassword: true, priority: 10, enabled: true, maxRetries: -1,
        rssi: -74, quality: 52, active: true, staticIp: false },
      { id: 2, ssid: 'Guest', hasPassword: false, priority: 0, enabled: false, maxRetries: 3,
        rssi: 0, quality: 0, active: false, staticIp: true },
    ],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /class="active"/); // рядок підключеного профілю
  assert.match(html, /color:var\(--ok\)">connected/);
  assert.match(html, /muted">\(open\)/); // Guest - без пароля
  assert.match(html, /muted">static/); // Guest - staticIp=true
  assert.match(html, /muted">disabled/); // Guest - enabled=false
  assert.match(html, /muted">not in range/); // Guest - rssi=0
  // Заблокована кнопка "Connect" - не порожня остання колонка (розділ
  // "Пре-альфа": SAPI поки не мутує стан пристрою) і не активна кнопка.
  assert.match(html, /<button class="act ghost" disabled title="[^"]+">Connect<\/button>/);
});

test('wifi-status: XSS у ssid профілю - HTML екрановано, тег не пролазить', () => {
  const html = renderWifiStatus(WIFI_STATUS_FIXTURE, {
    connections: [{ id: 1, ssid: '<img src=x onerror=alert(1)>', hasPassword: true, priority: 0,
      enabled: true, maxRetries: -1, rssi: -60, quality: 70, active: false, staticIp: false }],
  });
  assert.doesNotMatch(html, /<img/);
  assert.match(html, /&lt;img/);
});

test('ssidLabel: пробіли на краю - позначка .ws на кожен, не trim', () => {
  assert.equal(ssidLabel(' Asus '), '<span class="ws" title="space"></span>Asus<span class="ws" title="space"></span>');
  assert.equal(ssidLabel('Asus'), 'Asus');
});

test('wifiProfilesTable: null/undefined -> "Not loaded yet.", не порожня таблиця', () => {
  assert.match(wifiProfilesTable(null), /Not loaded yet\./);
  assert.match(wifiProfilesTable(undefined), /Not loaded yet\./);
});

// --- ecoflow-status: повна вкладка (renderEcoflowStatus, "1:1 як на
// порталі" - Connection dl + Overview-таблиця + картки пристроїв) ---

test('ecoflow-status: онлайн-пристрій on-grid (реальні дані DELTA 2)', () => {
  const html = renderEcoflowStatus({
    connected: true, running: true, channel: 'app (private API)', account: 'app-d60c0ab5',
    brokerHost: '192.168.1.22', brokerPort: 1883, viaProxy: true,
    messageCount: 67113, lastTopic: '/app/device/property/R331ZEB4ZEBW0026', lastError: '',
    heapFreeBytes: 66048, heapLargestBlockBytes: 45056, netStackHeadroomBytes: 2048,
    devices: [ECO_DEVICE_FIXTURE],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /<table class="zebra ecoflow-table">/); // Overview
  assert.match(html, /DELTA 2/);
  assert.match(html, /R331ZEB4ZEBW0026/); // Serial - і в таблиці, і в картці
  assert.match(html, /192\.168\.1\.22:1883/); // Broker
  assert.match(html, /proxy \(plain MQTT\)/); // Connection type
  assert.match(html, /65 KB free · 44 KB largest block/); // Heap: 66048/1024=64.5 -> round(64.5)=65
  assert.match(html, /color:var\(--ok\)">online/); // ecoPresence (не badge - той самий колір, що портал)
  assert.match(html, /color:var\(--ok\)">on-grid/);
  assert.match(html, /99\.7%/); // Charge (картка - ecoCharge, з дробовою частиною)
  assert.match(html, /in 70 W · out 57 W/);
  assert.match(html, /class="cards">/); // картки пристроїв
  assert.match(html, /<div class="card ecoflow-device" data-sn="R331ZEB4ZEBW0026" data-model="delta2">/); // фото - за type
  assert.match(html, /Raw parameters \(not loaded\)/); // params не приходять цим каналом, окремий drill-down
  assert.match(html, /Load raw parameters/);
  assert.doesNotMatch(html, /<details class="eco-params" open/); // openParamsSerials не переданий - згорнуто
});

test('ecoflow-status: params довантажені (drill-down) - таблиця + кнопка "Refresh", details розгорнутий', () => {
  const html = renderEcoflowStatus({
    connected: true, running: true, channel: 'app (private API)', account: 'app-d60c0ab5',
    brokerHost: '192.168.1.22', brokerPort: 1883, viaProxy: true,
    messageCount: 67113, lastTopic: '/app/device/property/R331ZEB4ZEBW0026', lastError: '',
    heapFreeBytes: 66048, heapLargestBlockBytes: 45056, netStackHeadroomBytes: 2048,
    devices: [{ ...ECO_DEVICE_FIXTURE, params: { pd_soc: 99.7, bms_bmsStatus_soc: 100 }, droppedParams: 3 }],
  }, { openParamsSerials: new Set(['R331ZEB4ZEBW0026']) });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /<details class="eco-params" open data-sn="R331ZEB4ZEBW0026">/);
  assert.match(html, /Raw parameters \(2, 3 dropped \(per-device limit\), capture: important only\)/);
  assert.match(html, /<td>bms_bmsStatus_soc<\/td><td>100<\/td>/); // ecoParamsTable - ключі відсортовані
  assert.doesNotMatch(html, /Load raw parameters/); // початковий напис зникає, коли params уже є
  // Але кнопка ЛИШАЄТЬСЯ (той самий клас .eco-params-btn, той самий делегований
  // клік у index.html) - інакше params, щойно довантажені, лишались би
  // застарілими назавжди (пристрій далі оновлює значення, MQTT-агрегат їх не несе).
  assert.match(html, /<button class="act ghost eco-params-btn"[^>]*data-sn="R331ZEB4ZEBW0026"[^>]*>Refresh<\/button>/);
});

// --- ecoJournalSection/ecoJournalRow: журнал переходів grid, окремий запит
// 'ecoflow-journal' (не ecoflow-status) - EcoflowJournalView.hpp на боці плати ---

test('ecoJournalRow: mark ">" + on-grid - смуга повного --ok + приписка "(ongoing)"', () => {
  const html = ecoJournalRow({ atEpoch: 1732900000, device: 'DELTA 2 (xama)', grid: 'on-grid', ageSec: 3661, mark: '>' });
  assert.match(html, /<tr style="--eco-stripe:var\(--ok\)" data-eco-device="DELTA 2 \(xama\)">/);
  assert.match(html, /\(ongoing\)/);
  assert.match(html, /DELTA 2 \(xama\)/);
  assert.match(html, /color:var\(--ok\)">on-grid/); // ecoGrid(grid, false) - без "(inferred)"
});

test('ecoJournalRow: mark ">" + off-grid - смуга повного --warn (не зелена)', () => {
  const html = ecoJournalRow({ atEpoch: 1732900000, device: 'DELTA 2', grid: 'off-grid', ageSec: 60, mark: '>' });
  assert.match(html, /<tr style="--eco-stripe:var\(--warn\)" data-eco-device="DELTA 2">/);
});

test('ecoJournalRow: mark "<" - той самий колір, що й grid, але приглушений (--*-dim)', () => {
  const html = ecoJournalRow({ atEpoch: 1732900000, device: 'DELTA 2', grid: 'off-grid', ageSec: 90, mark: '<' });
  assert.match(html, /<tr style="--eco-stripe:var\(--warn-dim\)" data-eco-device="DELTA 2">/);
  assert.match(html, /\(closed\)/);
});

test('ecoJournalRow: mark "" - звичайний рядок, без смуги й без приписки', () => {
  const html = ecoJournalRow({ atEpoch: 1732900000, device: 'DELTA 2', grid: 'on-grid', ageSec: 5, mark: '' });
  assert.match(html, /<tr data-eco-device="DELTA 2">/);
  assert.doesNotMatch(html, /\(ongoing\)|\(closed\)/);
});

test('ecoJournalRow: selectedDevice - обраний пристрій ".selected", інший ".muted"', () => {
  const own = ecoJournalRow({ atEpoch: 1732900000, device: 'DELTA 2', grid: 'on-grid', ageSec: 5, mark: '' }, 'DELTA 2');
  const other = ecoJournalRow(
    { atEpoch: 1732900000, device: 'DELTA Pro', grid: 'on-grid', ageSec: 5, mark: '' }, 'DELTA 2');
  assert.match(own, /<tr class="selected" data-eco-device="DELTA 2">/);
  assert.match(other, /<tr class="muted" data-eco-device="DELTA Pro">/);
});

test('ecoJournalRow: колонка Finish - другий <td>, одразу після Start', () => {
  const html = ecoJournalRow({ atEpoch: 1732900000, device: 'DELTA 2', grid: 'on-grid', ageSec: 3600, mark: '' });
  const cells = html.match(/<td[^>]*>.*?<\/td>/gs);
  assert.equal(cells.length, 5); // Start, Finish, Device, Grid, Age
});

test('ecoFinishText: Finish у ту саму календарну дату - лише час, без "(+Xd)"', () => {
  const startEpoch = Math.floor(Date.UTC(2026, 0, 15, 12, 0, 0) / 1000); // 2026-01-15 12:00 UTC
  const text = ecoFinishText(startEpoch, 3600); // +1 год - та сама доба практично в будь-якому TZ
  assert.doesNotMatch(text, /\(\+/);
  assert.match(text, /:/); // виглядає як час (toLocaleTimeString())
});

test('ecoFinishText: Finish у іншу календарну дату - час + "(+Nd)"', () => {
  const startEpoch = Math.floor(Date.UTC(2026, 0, 15, 12, 0, 0) / 1000);
  const text = ecoFinishText(startEpoch, 2 * 86400); // +2 доби - різниця дат стійка до зсуву TZ
  assert.match(text, /\(\+2d\)/);
});

test('ecoJournalSection: journalData=null - "Expand to load…", опції з devices', () => {
  const html = ecoJournalSection([ECO_DEVICE_FIXTURE], null, 'all', false, false);
  assertNoLeakedPlaceholders(html);
  assert.match(html, /<details id="eco-journal-wrap">/); // journalOpen=false - без "open"
  assert.match(html, /Expand to load…/);
  assert.match(html, /<option value="all" selected>All devices<\/option>/);
  assert.match(html, /<option value="R331ZEB4ZEBW0026">DELTA 2<\/option>/);
  assert.doesNotMatch(html, /checked/); // journalAuto=false
});

test('ecoJournalSection: rows=[] - "No transitions recorded yet.", open+auto+обраний пристрій', () => {
  const html = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows: [] }, 'R331ZEB4ZEBW0026', true, true);
  assert.match(html, /<details id="eco-journal-wrap" open>/);
  assert.match(html, /No transitions recorded yet\./);
  assert.match(html, /<option value="R331ZEB4ZEBW0026" selected>DELTA 2<\/option>/);
  assert.match(html, /id="eco-journal-auto" checked/);
  assert.match(html, /0 row\(s\)/);
});

test('ecoJournalSection: рядки журналу рендеряться через ecoJournalRow, лічильник - довжина rows', () => {
  const rows = [
    { atEpoch: 1732800000, device: 'DELTA 2', serialNumber: 'R331ZEB4ZEBW0026', grid: 'off-grid', ageSec: 7200, mark: '' },
    { atEpoch: 1732900000, device: 'DELTA 2', serialNumber: 'R331ZEB4ZEBW0026', grid: 'on-grid', ageSec: 60, mark: '>' },
  ];
  const html = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows }, 'all', false, true);
  assertNoLeakedPlaceholders(html);
  assert.match(html, /2 row\(s\)/);
  assert.match(html, /<tr style="--eco-stripe:var\(--ok\)" data-eco-device="DELTA 2">/);
  assert.match(html, /<tr data-eco-device="DELTA 2">/);
});

test('ecoJournalSection: selectedDevice - лише рядки іншого пристрою отримують .muted', () => {
  const rows = [
    { atEpoch: 1732800000, device: 'DELTA 2', grid: 'off-grid', ageSec: 7200, mark: '' },
    { atEpoch: 1732900000, device: 'DELTA Pro', grid: 'on-grid', ageSec: 60, mark: '>' },
  ];
  const html = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows }, 'all', false, true, true, 'DELTA 2');
  assert.match(html, /<tr class="selected" data-eco-device="DELTA 2">/);
  assert.match(html, /<tr style="--eco-stripe:var\(--ok\)" class="muted" data-eco-device="DELTA Pro">/);
});

test('ecoJournalSection: selectedDevice повертається назад у data-eco-selected на wrap - '
  + 'інакше наступна перемальовка (index.html: скан DOM перед innerHTML=) губить вибір', () => {
  const withSelection = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows: [] }, 'all', false, true, true, 'DELTA 2');
  assert.match(withSelection, /<details id="eco-journal-wrap" open data-eco-selected="DELTA 2">/);
  const withoutSelection = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows: [] }, 'all', false, true, true, null);
  assert.doesNotMatch(withoutSelection, /data-eco-selected/);
});

test('ecoJournalSection: другий клік по вже обраному (index.html: wrap.dataset.ecoSelected === device -> '
  + 'delete) повертає journalSelectedDevice=null - жодних .selected/.muted, звичайний вигляд журналу', () => {
  const rows = [
    { atEpoch: 1732800000, device: 'DELTA 2', grid: 'off-grid', ageSec: 7200, mark: '' },
    { atEpoch: 1732900000, device: 'DELTA Pro', grid: 'on-grid', ageSec: 60, mark: '>' },
  ];
  const deselected = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows }, 'all', false, true, true, null);
  assert.doesNotMatch(deselected, /data-eco-selected/);
  // tr-рівень, не будь-який .muted на сторінці (лічильник "row(s)"/"(ongoing)" теж носить цей клас).
  assert.doesNotMatch(deselected, /<tr class="selected"|<tr class="muted"/);
});

test('ecoJournalSection: newest=true - рядки в зворотному порядку (найновіший перший), чекбокс checked', () => {
  const rows = [
    { atEpoch: 1732800000, device: 'DELTA 2', serialNumber: 'R331ZEB4ZEBW0026', grid: 'off-grid', ageSec: 7200, mark: '<' },
    { atEpoch: 1732900000, device: 'DELTA 2', serialNumber: 'R331ZEB4ZEBW0026', grid: 'on-grid', ageSec: 60, mark: '>' },
  ];
  const html = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows }, 'all', false, true, true);
  const tbody = html.match(/<tbody>(.*?)<\/tbody>/s)[1];
  // Найновіший (mark '>', --ok) - перший рядок tbody, а не другий.
  assert.match(tbody, /^<tr style="--eco-stripe:var\(--ok\)" data-eco-device="DELTA 2">/);
  assert.match(html, /id="eco-journal-newest-first" checked/);
});

test('ecoJournalSection: newest не передано - хронологічний порядок (як прийшов з сервера), чекбокс без checked', () => {
  const rows = [
    { atEpoch: 1732800000, device: 'DELTA 2', serialNumber: 'R331ZEB4ZEBW0026', grid: 'off-grid', ageSec: 7200, mark: '<' },
    { atEpoch: 1732900000, device: 'DELTA 2', serialNumber: 'R331ZEB4ZEBW0026', grid: 'on-grid', ageSec: 60, mark: '>' },
  ];
  const html = ecoJournalSection([ECO_DEVICE_FIXTURE], { rows }, 'all', false, true);
  const tbody = html.match(/<tbody>(.*?)<\/tbody>/s)[1];
  assert.match(tbody, /^<tr style="--eco-stripe:var\(--warn-dim\)" data-eco-device="DELTA 2">/);
  assert.doesNotMatch(html, /id="eco-journal-newest-first" checked/);
});

test('ecoParamsTable: сортує ключі, escape на значеннях/ключах, порожній набір - "not captured yet"', () => {
  assert.match(ecoParamsTable({ b: 1, a: 2 }), /<td>a<\/td><td>2<\/td>.*<td>b<\/td><td>1<\/td>/s);
  assert.match(ecoParamsTable({}), /No raw parameters captured yet\./);
});

test('ecoflow-status: без пристроїв - "No devices configured." у таблиці й картках, без винятку', () => {
  const html = renderEcoflowStatus({
    connected: false, running: false, channel: '', account: '', brokerHost: '', brokerPort: null,
    viaProxy: false, messageCount: 0, lastTopic: '', lastError: 'auth denied', heapFreeBytes: null,
    devices: [],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /0 device\(s\)/);
  assert.match(html, /No devices configured\./); // і в <tr colspan>, і під картками
  assert.match(html, /Last error<\/dt><dd>auth denied/);
  assert.match(html, /Heap<\/dt><dd>-/); // heapFreeBytes:null - "-", не "NaN KB"
});

test('ecoflow-status: невідома presence/grid, null-параметри пристрою - fallback на serialNumber', () => {
  const html = renderEcoflowStatus({
    connected: true, running: true, channel: 'app', account: 'acc', brokerHost: 'h', brokerPort: 1883,
    viaProxy: false, messageCount: 1, lastTopic: 't', lastError: '', heapFreeBytes: 1024,
    devices: [{
      serialNumber: 'SN1', name: '', type: 'DELTA 2 Max', presence: 'unknown', online: false,
      messageCount: 0, ageMs: null, lastMessageEpoch: 0, socPercent: null, socPrecise: null,
      grid: 'unknown', gridInferred: true, gridForMs: null, gridChangeCount: 0,
      acInputMilliVolts: null, acInputFrequency: null, inputWatts: null, outputWatts: null,
      remainTimeMinutes: null, snapshotAvailable: false, captureAll: false, droppedParams: 0,
    }],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /<b>SN1<\/b>/); // без name - fallback на serialNumber (картка)
  assert.match(html, /color:var\(--muted\)">unknown/); // presence "unknown" - muted, не badge
  assert.match(html, /inferred/);
  // Невідома модель (тут - "DELTA 2 Max", поза ECO_DEVICE_IMAGE_SLUG) - картка
  // без фото (без data-model), а не з биткою заглушкою (той самий портал).
  assert.doesNotMatch(html, /data-model=/);
});

test('ecoTableRow: рядок Overview-таблиці несе ті самі колонки, що портал', () => {
  const html = ecoTableRow(ECO_DEVICE_FIXTURE, 1);
  assertNoLeakedPlaceholders(html);
  assert.match(html, /<td class="opt2">1<\/td>/);
  assert.match(html, /<td class="opt2">R331ZEB4ZEBW0026<\/td>/);
  assert.match(html, /<td class="num">100%<\/td>/); // ecoChargeRounded - без дробової частини
  assert.match(html, /class="ecoflow-wide">in 70 W · out 57 W/);
  assert.match(html, /class="ecoflow-wide num">230\.0 V · 50 Hz/);
});

// --- mqtt-status ---

const MQTT_FIXTURE = {
  connected: true, host: '192.168.1.22', port: 1883, security: 'plain',
  clientId: 'mqtt-esp32-c3', login: 'esp32-c3', topicPrefix: 'mykola-lavryk',
  publishedCount: 4213, receivedCount: 189,
  subscribeDeniedCount: 0, commandsRejectedCount: 0,
  droppedOutgoingCount: 0, droppedIncomingCount: 0, netStackHeadroomBytes: 2048,
  lwt: { topic: 'devices/mqtt-esp32-c3/status', offlineMessage: 'offline', onlineMessage: 'online' },
  heartbeat: { message: 'alive', intervalMs: 60000 },
  consoleMirror: { available: true, active: true, topic: 'console/mqtt-esp32-c3',
                   publishedCount: 512, droppedByRateLimitCount: 3, allowRules: [], denyRules: [] },
};

test('mqtt-status: щасливий шлях - повна вкладка (renderMqttStatus)', () => {
  const html = renderMqttStatus(MQTT_FIXTURE);
  assertNoLeakedPlaceholders(html);
  assert.match(html, /<h2>Connection<\/h2>/);
  assert.match(html, /connected/);
  assert.match(html, /192\.168\.1\.22:1883/);
  assert.match(html, /esp32-c3/); // login
  assert.match(html, /devices\/mqtt-esp32-c3\/status/); // LWT topic
  assert.match(html, /Heartbeat message/); // розгорнутий (2 рядки), не компактний
  assert.match(html, /<h2>Console mirror<\/h2>/);
  assert.match(html, /console-mqtt/); // той самий p.muted, що на порталі
  assert.match(html, /on<\/dd>/); // Mirror: on
  assert.match(html, /class="legend muted"/);
  assert.match(html, /<dt>Subscribe denied<\/dt>/);
  assert.doesNotMatch(html, /class="kv"/); // голий dl, як портал, не .kv
  assert.doesNotMatch(html, /<h3>Console mirror<\/h3>/);
});

test('mqtt-status: анонімний логін, LWT не налаштовано, dropped/denied>0 - колір і хінт', () => {
  const html = renderMqttStatus({
    ...MQTT_FIXTURE, login: null, lwt: null, subscribeDeniedCount: 2, commandsRejectedCount: 1,
    droppedOutgoingCount: 3, droppedIncomingCount: 1,
    consoleMirror: { available: false },
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /\(anonymous\)/);
  assert.match(html, /LWT<\/dt><dd>not configured/);
  assert.match(html, /color:var\(--err\)">2 - check ACL/);
  assert.match(html, /color:var\(--err\)">1 - command queue was full/);
  assert.match(html, /color:var\(--err\)">3 message\(s\)/); // Dropped out
  assert.match(html, /not built into this firmware/);
});

test('mqttSystemRows: System-картка - Dropped згорнутий в один рядок, Console mirror компактний', () => {
  const rows = mqttSystemRows(MQTT_FIXTURE);
  const byLabel = Object.fromEntries(rows.map(([k, v]) => [k, v]));
  assert.ok('Messages dropped' in byLabel);
  assert.ok(!('Dropped out' in byLabel)); // не два окремі рядки, як на повній вкладці
  assert.match(byLabel['Heartbeat'], /class="hint-mark"/); // компактний, з hintIcon
  assert.equal(byLabel['Console mirror'], 'on');
});

test('mqttSystemRows: consoleMirror.available=false - рядок Console mirror не показується взагалі', () => {
  const rows = mqttSystemRows({ ...MQTT_FIXTURE, consoleMirror: { available: false } });
  const labels = rows.map(([k]) => k);
  assert.ok(!labels.includes('Console mirror'));
});

test('system-info: extra.mqtt - MQTT-картка без моку', () => {
  const html = renderSystemInfo(CHIP_FIXTURE, { mqtt: MQTT_FIXTURE });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /mqtt-esp32-c3/);
  assert.doesNotMatch(html, /<h2>MQTT<\/h2><dl><dt>Connection<\/dt><dd class="muted">\*\*\*/);
});

// --- LittleFS: картка, бар, вкладка Files ---

test('System: картка LittleFS - рядки Total/Used/Free і бар used/free', () => {
  const html = renderSystemInfo({ ...CHIP_FIXTURE, littlefs: { available: true, usedBytes: 262144, totalBytes: 1048576 } });
  assert.match(html, /<dt>Total<\/dt><dd>1\.00 MB<\/dd><dt>Used<\/dt><dd>0\.25 MB \(25%\)<\/dd>/);
  assert.match(html, /\(25%\)/);
  assert.match(html, /\(75%\)/);
  assert.match(html, /Used: 256\.0 KB of 1\.00 MB/);
});

test('System: LittleFS недоступна - not available; без поля - мок', () => {
  assert.match(renderSystemInfo({ ...CHIP_FIXTURE, littlefs: { available: false } }), /LittleFS<\/dt><dd class="muted">not available/);
  assert.match(renderSystemInfo(CHIP_FIXTURE), /<h2>LittleFS<\/h2><dl class="dl-bar">.*\*\*\*/s);
});

test('System: NVS і SD - бар; плата без SD - картки нема; без поля sd - мок', () => {
  const sd = { available: true, present: true, type: 'SDHC', sizeBytes: 4e9, usedBytes: 1e9 };
  const html = renderSystemInfo({ ...CHIP_FIXTURE, sd });
  assert.match(html, /<h2>SD card<\/h2>.*SDHC.*Used: 953\.67 MB of 3\.73 GB/s);
  assert.match(html, /Used: 12 entries of 1,000 entries|Used: 12 entries of 1\D000 entries/);
  assert.doesNotMatch(renderSystemInfo({ ...CHIP_FIXTURE, sd: { available: false } }), /SD card/);
  assert.match(renderSystemInfo({ ...CHIP_FIXTURE, sd: { available: true, present: false } }), /not detected/);
  assert.match(renderSystemInfo(CHIP_FIXTURE), /<h2>SD card<\/h2><dl class="dl-bar">.*\*\*\*/s);
});

test('usageBarHtml: total=0 - порожньо; used>total затискається до 100%', () => {
  assert.equal(usageBarHtml(0, 0), '');
  assert.match(usageBarHtml(2000, 1000), /width:100\.00%/);
});

test('renderFsList: каталоги вгорі, крихти, Open лише на каталозі, View заблокований', () => {
  const html = renderFsList({ ok: true, path: '/www', label: 'LittleFS', truncated: false, used: 100, total: 1000,
    entries: [{ name: 'b.txt', dir: false, size: 5 }, { name: 'sub', dir: true, size: 0 }, { name: 'a.txt', dir: false, size: 7 }] });
  assert.ok(html.indexOf('>sub<') < html.indexOf('>a.txt<'));
  assert.ok(html.indexOf('>a.txt<') < html.indexOf('>b.txt<'));
  assert.match(html, /data-fs-cd="\/www"/);
  assert.match(html, /data-fs-open="\/www\/sub"/);
  assert.equal((html.match(/disabled title/g) || []).length, 2 + 4); // New folder/file + форма Editor
  assert.match(html, /<tr data-fs-view="\/www\/a\.txt">/);
  assert.match(html, /<button class="act ghost" data-fs-view="\/www\/b\.txt">View<\/button>/);
  assert.ok(html.indexOf('fs-table') < html.indexOf('heap-bar'), 'бар під списком файлів');
  assert.match(html, /<h2>Editor[\s\S]*<h2>Preview/);
  assert.match(html, /3 item\(s\)/);
});

test('fsPreview: текст, порожній файл, бінарник (hex), картинка, обрізаний файл', () => {
  const enc = (s) => new TextEncoder().encode(s);
  assert.deepEqual(fsPreviewFromBytes('/a.txt', enc('héllo'), 6), { meta: '/a.txt · 6 B', text: 'héllo' });
  assert.equal(fsPreviewFromBytes('/e.txt', enc(''), 0).text, '(empty)');
  const bin = fsPreviewFromBytes('/b.bin', new Uint8Array([0, 0x41, 255]), 3);
  assert.match(bin.meta, /binary$/);
  assert.equal(bin.text, '00000000  00 41 ff                                          .A.');
  assert.equal(fsPreviewFromBytes('/x/p.JPG', new Uint8Array(4), 4).imageMime, 'image/jpeg');
  assert.match(fsPreviewFromBytes('/big.txt', enc('abc'), 5000).meta, /first 3 B of 4\.9 KB/);
});

test('b64ToBytes/concatBytes: розкодування шматків і склейка', () => {
  const a = b64ToBytes(Buffer.from('ab').toString('base64'));
  const b = b64ToBytes(Buffer.from('cd').toString('base64'));
  assert.equal(new TextDecoder().decode(concatBytes([a, b])), 'abcd');
  assert.equal(fsIsTextual(new Uint8Array([10, 65])), true);
  assert.equal(fsIsTextual(new Uint8Array([0])), false);
});

const NVS_FIXTURE = { ok: true, namespace: 'esp32', writable: true, truncated: false, namespaces: ['nvs.net80211', 'esp32'],
  entries: [{ key: 'zeta', type: 'string', value: '<b>x</b>', editable: true },
            { key: 'alpha', type: 'i32', value: '5', editable: true },
            { key: 'cal', type: 'blob', value: '(4 bytes)', editable: false }] };

test('renderNvsList: ключі за абеткою, свій namespace перший, форма disabled, View на кожному рядку', () => {
  const html = renderNvsList(NVS_FIXTURE, '');
  assert.ok(html.indexOf('>alpha<') < html.indexOf('>cal<') && html.indexOf('>cal<') < html.indexOf('>zeta<'));
  assert.ok(html.indexOf('<option value="esp32" selected>') < html.indexOf('<option value="nvs.net80211"'));
  assert.equal((html.match(/data-nvs-view=/g) || []).length, 6); // tr + кнопка x3
  assert.equal((html.match(/disabled title/g) || []).length, 5); // key, type, Save, Clear, textarea
  assert.match(html, /Add or update<\/h2><form/);
  assert.match(html, /<h2>Value /);
  assert.doesNotMatch(html, /<b>x<\/b>/); // екранування
});

test('renderNvsList: чужий namespace - без форми, з позначкою read-only; фільтр; ok:false', () => {
  const ro = renderNvsList({ ...NVS_FIXTURE, writable: false }, '');
  assert.match(ro, /read-only namespace/);
  assert.doesNotMatch(ro, /<form/);
  assert.match(nvsTableParts(NVS_FIXTURE, 'ALP').count, /^1 of 3/);
  assert.match(nvsTableParts(NVS_FIXTURE, 'qqq').rows, /Nothing matches/);
  assert.match(nvsTableParts({ ...NVS_FIXTURE, entries: [] }, '').rows, /Namespace is empty/);
  assert.match(renderNvsList({ ok: false, message: 'Bad' }), /Bad/);
  assert.match(renderNvsList(null), /Not loaded yet/);
});

test('hexToBytes/nvsAsJson', () => {
  assert.deepEqual([...hexToBytes('00ff41')], [0, 255, 65]);
  assert.deepEqual(nvsAsJson(' {"a":1} '), { a: 1 });
  assert.equal(nvsAsJson('42'), null);
  assert.equal(nvsAsJson('{bad'), null);
});

test('renderFsList: ok:false, порожній і незавантажений стани; ім\'я екранується', () => {
  assert.match(renderFsList({ ok: false, message: 'No such directory' }), /No such directory/);
  assert.match(renderFsList(null), /Not loaded yet/);
  assert.match(renderFsList({ ok: true, path: '/', entries: [] }), /Directory is empty/);
  assert.doesNotMatch(renderFsList({ ok: true, path: '/', entries: [{ name: '<b>', dir: false, size: 1 }] }), /<b>/);
});

// --- heapBarHtml (bar для Memory) ---

test('heapBarHtml: щасливий шлях - used/largest ширини й marker рахуються коректно', () => {
  // total=1000, free=400 -> used=600 (60%); largest=150 (15%, лежить одразу
  // за used); minFreeEver=100 -> marker на (1000-100)/1000=90%.
  const html = heapBarHtml({ totalBytes: 1000, freeBytes: 400, largestFreeBlockBytes: 150,
                             fragmentationPercent: 62, minFreeEverBytes: 100 });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /class="heap-bar-used" style="width:60\.00%"/);
  assert.match(html, /class="heap-bar-largest" style="left:60\.00%;width:15\.00%"/);
  assert.match(html, /class="heap-bar-marker" style="left:90\.00%"/);
  assert.match(html, /Min free ever/); // легенда показує рядок маркера
  // Кожен розмір - власний title (used/free/largest/min), без загального на
  // весь бар (за проханням користувача).
  assert.doesNotMatch(html, /<div class="heap-bar" title=/);
  assert.match(html, /class="heap-bar-used" style="width:60\.00%" title="Used: 600 B of 1000 B"/);
  assert.match(html, /class="heap-bar-largest"[^>]*title="Largest free block: 150 B/);
  assert.match(html, /class="heap-bar-free"[^>]*title="Free: 250 B in smaller fragments \(400 B free in total\)"/);
  assert.match(html, /class="heap-bar-marker"[^>]*title="Min free ever: 100 B/);
});

test('heapBarHtml: totalBytes=0/відсутній - порожній рядок, без ділення на нуль', () => {
  assert.equal(heapBarHtml({ totalBytes: 0, freeBytes: 0 }), '');
  assert.equal(heapBarHtml({}), '');
});

test('heapBarHtml: minFreeEverBytes відсутній - без marker і без рядка легенди', () => {
  const html = heapBarHtml({ totalBytes: 1000, freeBytes: 400, largestFreeBlockBytes: 150 });
  assertNoLeakedPlaceholders(html);
  assert.doesNotMatch(html, /heap-bar-marker/);
  assert.doesNotMatch(html, /Min free ever/);
});

test('heapBarHtml: largestFreeBlockBytes > freeBytes (неможливо фізично, але захист) - клемпиться', () => {
  const html = heapBarHtml({ totalBytes: 1000, freeBytes: 100, largestFreeBlockBytes: 5000 });
  assertNoLeakedPlaceholders(html);
  // used=900 (90%), largest не може перевищити залишок смуги (10%)
  assert.match(html, /class="heap-bar-used" style="width:90\.00%"/);
  assert.match(html, /class="heap-bar-largest" style="left:90\.00%;width:10\.00%"/);
  // Largest поглинає ввесь free без залишку - фрагментованого сегмента нема,
  // heap-bar-free не рендериться взагалі (нема на чому висіти title).
  assert.doesNotMatch(html, /heap-bar-free/);
});

// --- запуск ---

let failed = 0;
for (const { name, fn } of cases) {
  try {
    fn();
    console.log(`ok   - ${name}`);
  } catch (e) {
    failed++;
    console.error(`FAIL - ${name}`);
    console.error(`       ${e.message}`);
  }
}

console.log(`\n${cases.length - failed}/${cases.length} passed`);
process.exit(failed ? 1 : 0);
