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
const { renderSystemInfo, renderWifiStatus, renderEcoflowStatus } = require('../render.js');

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
