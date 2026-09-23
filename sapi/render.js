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
function dash(v) { return v === null || v === undefined ? '-' : v; }
function fmtBytes(n) {
  if (n === null || n === undefined) return '-';
  if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(2) + ' MB';
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KB';
  return n + ' B';
}
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

function renderSystemInfo(data) {
  const c = data.chip || {}, h = data.heap || {}, f = data.flash || {}, n = data.nvs || {};
  let html = '<div class="section-group"><h3>Chip</h3>' + kv([
    ['model', esc(dash(c.model))],
    ['revision', dash(c.revision)],
    ['cores', dash(c.cores)],
    ['cpu freq', (c.cpuFreqMHz ?? '-') + ' MHz'],
    ['PSRAM', c.psramFound ? fmtBytes(c.psramBytes) : boolBadge(false)],
  ]) + '</div>';
  html += '<div class="section-group"><h3>Heap</h3>' + kv([
    ['total', fmtBytes(h.totalBytes)],
    ['free', fmtBytes(h.freeBytes)],
    ['largest free block', fmtBytes(h.largestFreeBlockBytes)],
    ['min free ever', fmtBytes(h.minFreeEverBytes)],
    ['fragmentation', (h.fragmentationPercent ?? '-') + ' %'],
  ]) + '</div>';
  html += '<div class="section-group"><h3>Flash</h3>' + kv([
    ['size', fmtBytes(f.sizeBytes)],
    ['speed', f.speedHz ? (f.speedHz / 1e6).toFixed(0) + ' MHz' : '-'],
  ]) + '</div>';
  html += '<div class="section-group"><h3>NVS</h3>' + (n.available ? kv([
    ['used entries', dash(n.usedEntries)],
    ['free entries', dash(n.freeEntries)],
    ['total entries', dash(n.totalEntries)],
    ['namespaces', dash(n.namespaceCount)],
  ]) : boolBadge(false, 'available', 'unavailable')) + '</div>';
  if (Array.isArray(data.partitions) && data.partitions.length) {
    html += '<div class="section-group"><h3>Partitions</h3><table class="data"><thead><tr>'
      + '<th>label</th><th>type</th><th>subtype</th><th>offset</th><th>size</th><th>enc</th></tr></thead><tbody>'
      + data.partitions.map((p) => `<tr><td>${esc(p.label)}</td><td>${esc(p.type)}</td><td>${esc(p.subtype)}</td>`
        + `<td>0x${(p.offset >>> 0).toString(16)}</td><td>${fmtBytes(p.size)}</td><td>${p.encrypted ? '✓' : ''}</td></tr>`).join('')
      + '</tbody></table></div>';
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
    esc, badge, boolBadge, kv, dash, fmtBytes, fmtMs, fmtEpoch,
    renderSystemInfo, renderWifiStatus, renderEcoflowStatus, RENDERERS,
  };
}
