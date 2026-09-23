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
function fmtMs(ms) {
  if (ms === null || ms === undefined) return '-';
  if (ms < 1000) return ms + ' ms';
  const s = ms / 1000;
  if (s < 60) return s.toFixed(1) + ' s';
  const m = s / 60;
  if (m < 60) return m.toFixed(1) + ' min';
  return (m / 60).toFixed(1) + ' h';
}
function fmtEpoch(sec) { return sec ? new Date(sec * 1000).toLocaleString() : '-'; }

// Той самий каркас карток, що System-вкладка порталу (assets/www/index.html,
// #sys-cards/.card): Device/Network/EcoFlow/MQTT/Memory/LittleFS/SD card/
// NVS/Modules + таблиця розділів під картками, той самий порядок карток.
// system-info (kJsonApiSystemInfo, src/main.cpp) сьогодні несе лише
// chip/heap/flash/nvs/partitions - решта портальних карток (env/uptime/
// мережа/EcoFlow/MQTT/LittleFS/SD/modules) цим каналом ще не їдуть, тому
// mockDl()/"***" замість того, щоб тихо пропустити картку чи рядок: форма
// вже готова під майбутнє поле, видно, що саме ще не підключено, а не що
// воно нульове.
//
// Network/EcoFlow дублюють окремі SAPI-команди (wifi-status/ecoflow-status,
// свої вкладки) - навмисно мок і тут: об'єднання System-картки з даними
// сусідньої команди (один HTTP-подібний запит на вкладку, як у порталі,
// а не один MQTT-запит на команду) - відкрите архітектурне питання, ще не
// вирішене (сесія 2026-09-24, обговорення "як генерувати SAPI-контент з
// різних повідомлень"). MQTT-картка (стан ВЛАСНОГО MQTT-клієнта плати,
// docs/mqtt-topics.md) - мок повністю, для неї ще немає жодної SAPI-команди
// взагалі.
function renderSystemInfo(data) {
  const c = data.chip || {}, h = data.heap || {}, f = data.flash || {}, n = data.nvs || {};

  const deviceTop = mockDl(['Firmware env', 'Revision', 'Uptime']);
  const deviceHeap = dlRows([['Free heap', fmtBytes(h.freeBytes)]]);
  const deviceRest = mockDl(['Portal auth', 'Pending jobs']);
  const deviceFlash = dlRows([
    ['Chip model', esc(dash(c.model)) + (c.revision != null ? ` (rev ${c.revision})` : '')],
    ['CPU', `${dash(c.cores)} core${c.cores === 1 ? '' : 's'} @ ${c.cpuFreqMHz ?? '-'} MHz`],
    ['PSRAM', c.psramFound ? fmtBytes(c.psramBytes) : 'not present'],
    ['Flash size', fmtBytes(f.sizeBytes)],
    ['Flash speed', f.speedHz ? (f.speedHz / 1e6).toFixed(0) + ' MHz' : '-'],
  ]);
  let html = '<div class="cards">';
  html += `<div class="card"><h2>Device</h2><dl>${deviceTop}${deviceHeap}${deviceRest}</dl><dl>${deviceFlash}</dl></div>`;

  html += '<div class="card"><h2>Network</h2><dl>' + mockDl([
    'State', 'SSID', 'IP address', 'Gateway', 'Signal', 'Connection type', 'MAC', 'Auto reconnect', 'Hotspot',
  ]) + '</dl></div>';

  html += '<div class="card"><h2>EcoFlow</h2><dl>' + mockDl([
    'Connection', 'Broker', 'Connection type', 'Devices', 'Messages received',
  ]) + '</dl></div>';

  html += '<div class="card"><h2>MQTT</h2><dl>' + mockDl([
    'Connection', 'Broker', 'Security', 'Client ID', 'Login', 'Topic prefix',
    'Published / received', 'Messages dropped', 'Subscribe denied', 'Commands rejected',
    'LWT topic', 'LWT online', 'LWT offline', 'Heartbeat', 'Console mirror',
  ]) + '</dl></div>';

  const memUnit = fsUnit(h.totalBytes || h.freeBytes || 1);
  const freePct = h.totalBytes > 0 ? Math.round((h.freeBytes / h.totalBytes) * 100) : null;
  html += '<div class="card"><h2>Memory</h2><dl>' + dlRows([
    ['Total', h.totalBytes ? fsSizeIn(h.totalBytes, memUnit) : '-'],
    ['Free', fsSizeIn(h.freeBytes, memUnit) + (freePct != null ? ` (${freePct}%)` : '')],
    ['Largest free block', fsSizeIn(h.largestFreeBlockBytes, memUnit)],
    ['Fragmentation', (h.fragmentationPercent ?? '-') + '%'],
    ['Min free ever', h.minFreeEverBytes ? fsSizeIn(h.minFreeEverBytes, memUnit) : '-'],
  ]) + '</dl></div>';

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

  html += '<div class="card"><h2>Modules</h2><p class="muted">***</p></div>';
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

function renderWifiStatus(data) {
  const stateCls = data.connected ? 'ok'
    : ['connecting', 'reconnecting', 'scanning', 'wps'].includes(data.state) ? 'warn' : 'err';
  let html = kv([
    ['state', badge(dash(data.state), stateCls)],
    ['ssid', esc(dash(data.ssid))],
    ['ip', esc(dash(data.ip))],
    ['gateway', esc(dash(data.gateway))],
    ['rssi', data.rssi != null ? data.rssi + ' dBm' : '-'],
    ['quality', data.quality != null ? data.quality + ' %' : '-'],
    ['phy mode', esc(dash(data.phyMode))],
    ['mac', esc(dash(data.mac))],
    ['auto-reconnect', boolBadge(data.autoReconnect)],
  ]);
  if (data.ap) {
    html += '<div class="section-group"><h3>Access point</h3>' + kv([
      ['active', boolBadge(data.ap.active)],
      ['ssid', esc(dash(data.ap.ssid))],
      ['ip', esc(dash(data.ap.ip))],
      ['clients', dash(data.ap.clients)],
      ['security', esc(dash(data.ap.security))],
    ]) + '</div>';
  }
  return html;
}

function renderEcoflowStatus(data) {
  let html = kv([
    ['connected', boolBadge(data.connected)],
    ['running', boolBadge(data.running)],
    ['channel', esc(dash(data.channel))],
    ['account', esc(dash(data.account))],
    ['broker', esc(dash(data.brokerHost)) + ':' + esc(dash(data.brokerPort)) + (data.viaProxy ? ' (proxy)' : '')],
    ['messages', dash(data.messageCount)],
    ['last topic', esc(dash(data.lastTopic))],
    ['last error', data.lastError ? badge(data.lastError, 'err') : badge('none', 'muted')],
    ['heap free', fmtBytes(data.heapFreeBytes)],
  ]);
  const devices = Array.isArray(data.devices) ? data.devices : [];
  if (!devices.length) {
    html += '<p class="muted" style="margin:10px 0 0">no devices</p>';
    return html;
  }
  html += '<div class="section-group"><h3>Devices</h3>';
  for (const d of devices) {
    const presenceCls = d.presence === 'online' ? 'ok' : d.presence === 'offline' ? 'err' : 'muted';
    const gridCls = d.grid === 'on-grid' ? 'ok' : d.grid === 'off-grid' ? 'warn' : 'muted';
    html += `<div style="margin-bottom:10px"><strong>${esc(d.name || d.serialNumber)}</strong> `
      + `<span class="muted">(${esc(dash(d.type))})</span><br>` + kv([
        ['presence', badge(dash(d.presence), presenceCls)],
        ['grid', badge(dash(d.grid), gridCls) + (d.gridInferred ? ' <span class="muted">inferred</span>' : '')],
        ['soc', d.socPrecise != null ? d.socPrecise + ' %' : d.socPercent != null ? d.socPercent + ' %' : '-'],
        ['input / output', `${dash(d.inputWatts)} W / ${dash(d.outputWatts)} W`],
        ['remain time', d.remainTimeMinutes != null ? d.remainTimeMinutes + ' min' : '-'],
        ['last message', fmtEpoch(d.lastMessageEpoch) + (d.ageMs != null ? ` <span class="muted">(${fmtMs(d.ageMs)} ago)</span>` : '')],
        ['grid for', fmtMs(d.gridForMs)],
      ]) + '</div>';
  }
  html += '</div>';
  return html;
}

const RENDERERS = {
  'system-info': renderSystemInfo,
  'wifi-status': renderWifiStatus,
  'ecoflow-status': renderEcoflowStatus,
};

// UMD-подібний хвіст: у браузері (класичний <script>, без type="module")
// module не існує - гілка пропускається, і всі function-декларації вище
// лишаються глобальними для наступного <script>, як і раніше. У Node
// (require() з test/render.test.js) - експортуємо явно.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    esc, badge, boolBadge, kv, dlRows, mockDl, dash, fmtBytes, fmtMs, fmtEpoch, fsUnit, fsSizeIn,
    renderSystemInfo, renderWifiStatus, renderEcoflowStatus, RENDERERS,
  };
}
