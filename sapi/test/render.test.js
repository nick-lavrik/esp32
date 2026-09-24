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
        ecoflowSystemDevicesHtml, mqttSystemRows, heapBarHtml } = require('../render.js');

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
  assert.match(html, /1\.0 h/); // Uptime: fmtMs(3725000)
  assert.match(html, /Pending jobs<\/dt><dd>2/);
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

// --- wifi-status ---

test('wifi-status: підключено, AP вимкнений', () => {
  const html = renderWifiStatus({
    state: 'connected', connected: true, ssid: 'STARLINK', ip: '192.168.1.25', gateway: '192.168.1.1',
    rssi: -68, quality: 64, phyMode: '802.11n (HT20)', mac: '9C:CC:01:7C:FD:90', autoReconnect: true,
    ap: { active: false, ssid: 'ESP-esp32-c3', ip: '192.168.4.1', clients: 0, security: 'open' },
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /STARLINK/);
  assert.match(html, /badge ok">connected/);
  assert.match(html, /class="sig">/); // Signal - смужки (signal()), connected=true
  assert.match(html, /Access point/);
});

test('wifi-status: не підключено, без блоку AP взагалі', () => {
  const html = renderWifiStatus({
    state: 'idle', connected: false, ssid: '', ip: '', gateway: '',
    rssi: null, quality: null, phyMode: '', mac: '9C:CC:01:7C:FD:90', autoReconnect: false,
    ap: null,
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /badge err">idle/);
  assert.doesNotMatch(html, /class="sig">/); // connected=false - прочерк, не смужки
  assert.doesNotMatch(html, /Access point/);
});

test('wifi-status: XSS у ssid - HTML екрановано, тег не пролазить', () => {
  const html = renderWifiStatus({
    state: 'connected', connected: true, ssid: '<img src=x onerror=alert(1)>', ip: '1.2.3.4',
    gateway: '', rssi: -50, quality: 80, phyMode: '', mac: '', autoReconnect: true, ap: null,
  });
  assert.doesNotMatch(html, /<img/);
  assert.match(html, /&lt;img/);
});

// --- ecoflow-status ---

test('ecoflow-status: онлайн-пристрій on-grid (реальні дані DELTA 2)', () => {
  const html = renderEcoflowStatus({
    connected: true, running: true, channel: 'app (private API)', account: 'app-d60c0ab5',
    brokerHost: '192.168.1.22', brokerPort: 1883, viaProxy: true,
    messageCount: 67113, lastTopic: '/app/device/property/R331ZEB4ZEBW0026', lastError: '',
    heapFreeBytes: 66048,
    devices: [{
      serialNumber: 'R331ZEB4ZEBW0026', name: 'DELTA 2', type: 'DELTA 2',
      presence: 'online', online: true, messageCount: 67113, ageMs: 1200,
      lastMessageEpoch: 1758649843, socPercent: 100, socPrecise: 99.7,
      grid: 'on-grid', gridInferred: false, gridForMs: 123456789, gridChangeCount: 3,
      acInputMilliVolts: 230000, acInputFrequency: 50, inputWatts: 70, outputWatts: 57,
      remainTimeMinutes: 5938, snapshotAvailable: true, captureAll: false, droppedParams: 0,
    }],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /DELTA 2/);
  assert.match(html, /badge ok">online/);
  assert.match(html, /badge ok">on-grid/);
  assert.match(html, /99\.7 %/);
  assert.match(html, /70 W \/ 57 W/);
});

test('ecoflow-status: без пристроїв - "no devices", без винятку', () => {
  const html = renderEcoflowStatus({
    connected: false, running: false, channel: '', account: '', brokerHost: '', brokerPort: null,
    viaProxy: false, messageCount: 0, lastTopic: '', lastError: 'auth denied', heapFreeBytes: null,
    devices: [],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /no devices/);
  assert.match(html, /badge err">auth denied/);
});

test('ecoflow-status: невідома presence/grid, null-параметри пристрою', () => {
  const html = renderEcoflowStatus({
    connected: true, running: true, channel: 'app', account: 'acc', brokerHost: 'h', brokerPort: 1883,
    viaProxy: false, messageCount: 1, lastTopic: 't', lastError: '', heapFreeBytes: 1024,
    devices: [{
      serialNumber: 'SN1', name: '', type: 'DELTA 2', presence: 'unknown', online: false,
      messageCount: 0, ageMs: null, lastMessageEpoch: 0, socPercent: null, socPrecise: null,
      grid: 'unknown', gridInferred: true, gridForMs: null, gridChangeCount: 0,
      acInputMilliVolts: null, acInputFrequency: null, inputWatts: null, outputWatts: null,
      remainTimeMinutes: null, snapshotAvailable: false, captureAll: false, droppedParams: 0,
    }],
  });
  assertNoLeakedPlaceholders(html);
  assert.match(html, /SN1/); // без name - fallback на serialNumber
  assert.match(html, /badge muted">unknown/);
  assert.match(html, /inferred/);
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
  assert.match(html, /connected/);
  assert.match(html, /192\.168\.1\.22:1883/);
  assert.match(html, /esp32-c3/); // login
  assert.match(html, /devices\/mqtt-esp32-c3\/status/); // LWT topic
  assert.match(html, /Heartbeat message/); // розгорнутий (2 рядки), не компактний
  assert.match(html, /Console mirror/);
  assert.match(html, /on<\/dd>/); // Mirror: on
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
