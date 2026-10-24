// Single-page configuration + status UI. Served from PROGMEM; all dynamic data
// comes from /api/config and /api/state.
#pragma once

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"=====(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Uptime Monitor</title>
<style>
  :root {
    --bg: #0e1116; --card: #161b22; --card2: #1c2330; --line: #263041;
    --fg: #e6edf3; --muted: #8b98a9; --accent: #3fb950; --danger: #f85149;
    --warn: #d29922; --blue: #58a6ff;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--bg); color: var(--fg);
    font: 15px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  }
  header {
    display: flex; align-items: baseline; justify-content: space-between; gap: 16px;
    padding: 18px 22px; border-bottom: 1px solid var(--line); flex-wrap: wrap;
  }
  .brand { font-weight: 700; font-size: 18px; letter-spacing: .2px; }
  .brand span { color: var(--accent); }
  .dev { color: var(--muted); font-size: 12.5px; font-variant-numeric: tabular-nums; }
  main { max-width: 1000px; margin: 0 auto; padding: 22px; display: grid; gap: 18px; }
  .card { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 18px; }
  h2 { margin: 0; font-size: 16px; }
  h3 { margin: 18px 0 8px; font-size: 13px; text-transform: uppercase; letter-spacing: .6px; color: var(--muted); }
  .row { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 12px; }
  button {
    font: inherit; font-size: 13.5px; color: var(--fg); background: var(--card2);
    border: 1px solid var(--line); border-radius: 7px; padding: 7px 12px; cursor: pointer;
  }
  button:hover { border-color: #3a4a63; }
  button.primary { background: #238636; border-color: #2ea043; color: #fff; }
  button.primary:hover { background: #2ea043; }
  button.danger { color: var(--danger); border-color: #5a2a2a; }
  .mon { border: 1px solid var(--line); border-radius: 9px; padding: 12px 14px; margin-bottom: 10px; background: var(--card2); }
  .monHead { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  .monName { font-weight: 600; }
  .monType { color: var(--muted); font-size: 12px; }
  .monHead .edit { margin-left: auto; }
  .monMeta { color: var(--muted); font-size: 12.5px; margin-top: 4px; word-break: break-all; }
  .monMeta code { color: #c9d1d9; font-size: 12.5px; }
  .err { color: var(--danger); }
  .badge {
    font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: .5px;
    padding: 2px 8px; border-radius: 999px; border: 1px solid transparent;
  }
  .badge.up { color: #3fb950; background: rgba(63,185,80,.12); border-color: rgba(63,185,80,.35); }
  .badge.down { color: #f85149; background: rgba(248,81,73,.12); border-color: rgba(248,81,73,.35); }
  .badge.pending { color: #d29922; background: rgba(210,153,34,.12); border-color: rgba(210,153,34,.35); }
  .badge.disabled { color: var(--muted); background: rgba(139,152,169,.1); border-color: var(--line); }
  .empty { color: var(--muted); margin: 8px 0 0; }
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 12px; }
  label { display: flex; flex-direction: column; gap: 5px; font-size: 12.5px; color: var(--muted); }
  label.check { flex-direction: row; align-items: center; gap: 8px; color: var(--fg); font-size: 13.5px; }
  input, select {
    font: inherit; font-size: 14px; color: var(--fg); background: #0d1117;
    border: 1px solid var(--line); border-radius: 7px; padding: 8px 10px; width: 100%;
  }
  input:focus, select:focus { outline: none; border-color: var(--blue); }
  input[type=checkbox] { width: auto; }
  .hint { color: var(--muted); font-size: 12px; margin: 10px 0 0; }
  .actions { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; margin-top: 16px; }
  .msg { font-size: 12.5px; color: var(--accent); }
  .msg.bad { color: var(--danger); }
  .modal { position: fixed; inset: 0; background: rgba(0,0,0,.65); display: flex; align-items: flex-start; justify-content: center; padding: 40px 16px; overflow: auto; z-index: 20; }
  .modal.hidden { display: none; }
  .sheet { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 20px; width: 100%; max-width: 680px; }
  .flags { display: flex; gap: 18px; flex-wrap: wrap; margin-top: 14px; }
  .hidden-field { display: none !important; }
</style>
</head>
<body>
<header>
  <div class="brand">uptime<span>.</span>monitor</div>
  <div class="dev" id="dev">connecting&hellip;</div>
</header>
<main>
  <section class="card">
    <div class="row">
      <h2>Monitors</h2>
      <button id="add" class="primary">+ Add monitor</button>
    </div>
    <div id="monitors"><p class="empty">loading&hellip;</p></div>
  </section>

  <section class="card">
    <h2>Settings</h2>
    <div class="grid">
      <label>Device name <input id="deviceName" maxlength="31"></label>
      <label>Probe timeout (ms, 500-30000) <input id="timeoutMs" type="number" min="500" max="30000"></label>
      <label>Webhook URL <input id="webhookUrl" placeholder="https://example.com/hook"></label>
      <label>Webhook bearer token (optional) <input id="webhookToken" placeholder="secret"></label>
      <label>Webhook format
        <select id="webhookFormat">
          <option value="generic">Generic JSON</option>
          <option value="discord">Discord</option>
        </select>
      </label>
    </div>
    <div class="flags">
      <label class="check"><input type="checkbox" id="notifyRecovery"> Send webhook when a monitor recovers</label>
    </div>
    <h3>Wi-Fi</h3>
    <div class="grid">
      <label>SSID <input id="wifiSsid" maxlength="32"></label>
      <label>Password <input id="wifiPass" type="password" maxlength="64"></label>
    </div>
    <h3>Web UI access</h3>
    <div class="grid">
      <label>Username <input id="uiUser" maxlength="31"></label>
      <label>Password <input id="uiPass" type="password" maxlength="64"></label>
    </div>
    <p class="hint">A blank UI password disables authentication. Changing Wi-Fi credentials reboots the device. Secrets are shown as ******** and kept unless you retype them.</p>
    <div class="actions">
      <button id="save" class="primary">Save configuration</button>
      <button id="testHook">Test webhook</button>
      <button id="reboot" class="danger">Reboot</button>
      <span class="msg" id="saveMsg"></span>
    </div>
  </section>
</main>

<div class="modal hidden" id="modal">
  <form class="sheet" id="form">
    <h2 id="formTitle">Add monitor</h2>
    <div class="grid" style="margin-top:14px">
      <label>Monitor type
        <select id="fType">
          <option value="http">HTTP(s)</option>
          <option value="tcp">TCP Port</option>
          <option value="ping">Ping</option>
        </select>
      </label>
      <label>Friendly name <input id="fName" placeholder="web server"></label>
      <label class="httpOnly">URL <input id="fUrl" placeholder="https://example.com/health"></label>
      <label class="notHttp">Hostname / IP <input id="fHost" placeholder="192.0.2.10"></label>
      <label class="tcpOnly">Port <input id="fPort" type="number" min="1" max="65535"></label>
      <label class="httpOnly">Keyword must appear in body (optional) <input id="fKeyword" maxlength="63" placeholder="application is healthy"></label>
      <label class="httpOnly">Accepted status codes <input id="fStatus" maxlength="31" value="200-299" placeholder="200-299"></label>
      <label class="httpOnly">Max redirects <input id="fRedirects" type="number" min="0" max="10" value="5"></label>
      <label>Request timeout override, ms (0 = global) <input id="fTimeout" type="number" min="0" max="60000" value="0"></label>
      <label>Heartbeat interval, seconds <input id="fInterval" type="number" min="1" max="86400" value="60"></label>
      <label>Retries <input id="fRetries" type="number" min="0" max="20" value="3"></label>
      <label>Heartbeat retry interval, seconds <input id="fRetry" type="number" min="1" max="86400" value="60"></label>
      <label>Resend if down X times (0 = off) <input id="fResend" type="number" min="0" max="1000" value="0"></label>
      <label>Description <input id="fDesc" maxlength="95"></label>
    </div>
    <div class="flags">
      <label class="check"><input type="checkbox" id="fEnabled" checked> Enabled</label>
      <label class="check"><input type="checkbox" id="fUpside"> Upside down mode</label>
      <label class="check"><input type="checkbox" id="fNotify" checked> Send webhook</label>
    </div>
    <p class="hint" id="semantics"></p>
    <div class="actions">
      <button type="submit" class="primary">Save monitor</button>
      <button type="button" id="cancel">Cancel</button>
      <button type="button" id="delete" class="danger">Delete</button>
      <span class="msg" id="formMsg"></span>
    </div>
  </form>
</div>

<script>
(function () {
  var cfg = null;
  var state = null;
  var editingId = null;
  var $ = function (id) { return document.getElementById(id); };

  function esc(v) {
    return String(v === undefined || v === null ? '' : v).replace(/[&<>"']/g, function (c) {
      return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];
    });
  }

  function typeLabel(t, m) {
    if (t === 'http' && m && m.keyword) return 'HTTP(s) keyword';
    return ({http: 'HTTP(s)', tcp: 'TCP Port', ping: 'Ping'})[t] || t;
  }

  function statusText(m) {
    if (!m.enabled) return 'disabled';
    var s = state && state.monitors ? state.monitors.find(function (x) { return x.id === m.id; }) : null;
    return s ? s.state : 'pending';
  }

  function statusMeta(m) {
    var s = state && state.monitors ? state.monitors.find(function (x) { return x.id === m.id; }) : null;
    if (!s) return '';
    var bits = [];
    if (s.latency_ms) bits.push(s.latency_ms + ' ms');
    if (s.last_check_s !== undefined && state) {
      var age = state.uptime_s - s.last_check_s;
      bits.push(age <= 1 ? 'just checked' : 'checked ' + age + 's ago');
    }
    if (s.last_error) bits.push('<span class="err">' + esc(s.last_error) + '</span>');
    return bits.join(' · ');
  }

  function targetOf(m) {
    if (m.type === 'http') {
      if (m.url) return m.url;
      var scheme = m.https ? 'https' : 'http';
      var port = (m.https && m.port === 443) || (!m.https && m.port === 80) ? '' : ':' + m.port;
      return scheme + '://' + m.host + port + (m.path || '/');
    }
    if (m.type === 'tcp') return m.host + ':' + m.port;
    return m.host;
  }

  function renderMonitors() {
    var host = $('monitors');
    if (!cfg || !cfg.monitors) { host.innerHTML = '<p class="empty">loading&hellip;</p>'; return; }
    if (!cfg.monitors.length) {
      host.innerHTML = '<p class="empty">No monitors yet. Add one to start watching.</p>';
      return;
    }
    host.innerHTML = cfg.monitors.map(function (m) {
      var st = statusText(m);
      var meta = statusMeta(m);
      return '<div class="mon">' +
        '<div class="monHead">' +
          '<span class="badge ' + esc(st) + '">' + esc(st) + '</span>' +
          '<span class="monName">' + esc(m.name) + '</span>' +
          '<span class="monType">' + esc(typeLabel(m.type, m)) + '</span>' +
          '<button class="edit" data-id="' + m.id + '">Edit</button>' +
        '</div>' +
        '<div class="monMeta"><code>' + esc(targetOf(m)) + '</code></div>' +
        '<div class="monMeta">every ' + m.interval + 's · retries ' + m.retries +
          ' @ ' + m.retry_interval + 's' +
          (m.resend_every ? ' · resend every ' + m.resend_every + ' downs' : '') +
          (m.upside_down ? ' · upside down' : '') +
        '</div>' +
        (meta ? '<div class="monMeta">' + meta + '</div>' : '') +
      '</div>';
    }).join('');
    Array.prototype.forEach.call(host.querySelectorAll('.edit'), function (b) {
      b.onclick = function () { openForm(Number(b.getAttribute('data-id'))); };
    });
  }

  function renderSettings() {
    $('deviceName').value = cfg.device_name || '';
    $('timeoutMs').value = cfg.timeout_ms || 5000;
    $('webhookUrl').value = (cfg.webhook && cfg.webhook.url) || '';
    $('webhookToken').value = (cfg.webhook && cfg.webhook.token) || '';
    $('webhookFormat').value = (cfg.webhook && cfg.webhook.format) || 'generic';
    $('notifyRecovery').checked = !cfg.webhook || cfg.webhook.notify_on_recovery !== false;
    $('wifiSsid').value = (cfg.wifi && cfg.wifi.ssid) || '';
    $('wifiPass').value = (cfg.wifi && cfg.wifi.pass) || '';
    $('uiUser').value = (cfg.ui && cfg.ui.user) || '';
    $('uiPass').value = (cfg.ui && cfg.ui.pass) || '';
  }

  function renderDevice() {
    if (!state) return;
    var bits = [state.device, state.ip || 'no address'];
    if (state.ap && !state.wifi) bits.push('setup AP');
    if (state.wifi) bits.push('rssi ' + state.rssi + ' dBm');
    bits.push('heap ' + Math.round(state.heap / 1024) + ' KB');
    bits.push('up ' + fmtUptime(state.uptime_s));
    $('dev').textContent = bits.join(' · ');
  }

  function fmtUptime(s) {
    s = Math.max(0, Math.floor(s || 0));
    var d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600),
        m = Math.floor((s % 3600) / 60), sec = s % 60;
    if (d) return d + 'd ' + h + 'h';
    if (h) return h + 'h ' + m + 'm';
    if (m) return m + 'm ' + sec + 's';
    return sec + 's';
  }

  function syncForm() {
    var t = $('fType').value;
    Array.prototype.forEach.call(document.querySelectorAll('.httpOnly'), function (e) {
      e.classList.toggle('hidden-field', t !== 'http');
    });
    Array.prototype.forEach.call(document.querySelectorAll('.notHttp'), function (e) {
      e.classList.toggle('hidden-field', t === 'http');
    });
    Array.prototype.forEach.call(document.querySelectorAll('.tcpOnly'), function (e) {
      e.classList.toggle('hidden-field', t !== 'tcp');
    });
    $('semantics').textContent = t === 'ping'
      ? 'Ping sends one ICMP echo request. A reply within the timeout is UP.'
      : t === 'tcp'
        ? 'TCP Port opens a connection to host:port. It is UP when the handshake succeeds.'
        : 'HTTP(s) performs a GET and is UP on an accepted status code. HTTPS certificates are not validated in this build. If a keyword is set, the response body must contain it.';
  }

  function openForm(id) {
    editingId = id || null;
    var m = null;
    if (editingId) {
      m = cfg.monitors.find(function (x) { return x.id === editingId; });
    }
    $('formTitle').textContent = m ? 'Edit monitor' : 'Add monitor';
    $('delete').style.display = m ? '' : 'none';
    $('fType').value = m ? m.type : 'tcp';
    $('fName').value = m ? m.name : '';
    $('fUrl').value = m && m.type === 'http' ? targetOf(m) : '';
    $('fHost').value = m ? m.host : '';
    $('fPort').value = m ? (m.port || '') : '';
    $('fKeyword').value = m ? (m.keyword || '') : '';
    $('fStatus').value = m ? (m.accepted_status || '200-299') : '200-299';
    $('fRedirects').value = m && m.max_redirects !== undefined ? m.max_redirects : 5;
    $('fTimeout').value = m ? (m.timeout_ms || 0) : 0;
    $('fInterval').value = m ? m.interval : 60;
    $('fRetries').value = m ? m.retries : 3;
    $('fRetry').value = m ? m.retry_interval : 60;
    $('fResend').value = m ? m.resend_every : 0;
    $('fDesc').value = m ? (m.description || '') : '';
    $('fEnabled').checked = m ? m.enabled : true;
    $('fUpside').checked = m ? m.upside_down : false;
    $('fNotify').checked = m ? m.notify : true;
    syncForm();
    formFlash('');
    $('modal').classList.remove('hidden');
  }

  function closeForm() {
    $('modal').classList.add('hidden');
    editingId = null;
  }

  function flash(text, bad) {
    var el = $('saveMsg');
    el.textContent = text;
    el.classList.toggle('bad', !!bad);
    clearTimeout(flash._t);
    flash._t = setTimeout(function () { el.textContent = ''; }, 4000);
  }

  function formFlash(text, bad) {
    var el = $('formMsg');
    el.textContent = text || '';
    el.classList.toggle('bad', !!bad);
  }

  function getJSON(url, opts) {
    return fetch(url, opts).then(function (r) {
      return r.text().then(function (text) {
        var data;
        try { data = JSON.parse(text); } catch (e) { data = {error: text}; }
        if (!r.ok) throw new Error(data.error || ('HTTP ' + r.status));
        return data;
      });
    });
  }

  function collectSettings() {
    cfg.device_name = $('deviceName').value.trim() || 'uptime-monitor';
    cfg.timeout_ms = Number($('timeoutMs').value) || 5000;
    cfg.webhook = cfg.webhook || {};
    cfg.webhook.url = $('webhookUrl').value.trim();
    cfg.webhook.token = $('webhookToken').value;
    cfg.webhook.format = $('webhookFormat').value;
    cfg.webhook.notify_on_recovery = $('notifyRecovery').checked;
    cfg.wifi = cfg.wifi || {};
    cfg.wifi.ssid = $('wifiSsid').value.trim();
    cfg.wifi.pass = $('wifiPass').value;
    cfg.ui = cfg.ui || {};
    cfg.ui.user = $('uiUser').value.trim();
    cfg.ui.pass = $('uiPass').value;
  }

  function saveAll(notify) {
    var say = notify || flash;
    collectSettings();
    return getJSON('/api/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(cfg)
    }).then(function (r) {
      say(r.reboot ? 'saved - rebooting' : 'saved');
      return loadConfig().then(function () {
        if (r.reboot) $('dev').textContent = 'rebooting…';
        return true;
      });
    }).catch(function (e) {
      say('error: ' + e.message, true);
      return false;
    });
  }

  function loadConfig() {
    return getJSON('/api/config').then(function (data) {
      cfg = data;
      if (!cfg.monitors) cfg.monitors = [];
      renderSettings();
      renderMonitors();
    });
  }

  function poll() {
    getJSON('/api/state').then(function (data) {
      state = data;
      renderDevice();
      renderMonitors();
    }).catch(function (e) {
      $('dev').textContent = 'offline: ' + e.message;
    });
  }

  $('add').onclick = function () { openForm(null); };
  $('cancel').onclick = closeForm;
  $('fType').onchange = syncForm;

  $('form').onsubmit = function (ev) {
    ev.preventDefault();
    var type = $('fType').value;
    var item = {
      id: editingId || 0,
      type: type,
      name: $('fName').value.trim(),
      enabled: $('fEnabled').checked,
      upside_down: $('fUpside').checked,
      notify: $('fNotify').checked,
      interval: Number($('fInterval').value),
      retries: Number($('fRetries').value),
      retry_interval: Number($('fRetry').value),
      resend_every: Number($('fResend').value),
      description: $('fDesc').value,
      keyword: type === 'http' ? $('fKeyword').value.trim() : '',
      accepted_status: type === 'http' ? ($('fStatus').value.trim() || '200-299') : '200-299',
      max_redirects: type === 'http' ? Number($('fRedirects').value) : 0,
      timeout_ms: Number($('fTimeout').value) || 0,
      host: '',
      port: 0,
      https: false,
      path: '/'
    };
    if (type === 'http') {
      item.url = $('fUrl').value.trim();
    } else {
      item.host = $('fHost').value.trim();
      if (type === 'tcp') item.port = Number($('fPort').value);
    }

    if (type === 'http' && !item.url) { formFlash('URL is required', true); return; }
    if (type !== 'http' && !item.host) { formFlash('Hostname is required', true); return; }
    if (type === 'tcp' && !(item.port > 0)) { formFlash('Port is required', true); return; }

    var snapshot = cfg.monitors.map(function (x) { return Object.assign({}, x); });
    var idx = cfg.monitors.findIndex(function (x) { return x.id === editingId; });
    if (editingId && idx >= 0) cfg.monitors[idx] = item;
    else cfg.monitors.push(item);

    formFlash('saving…');
    saveAll(formFlash).then(function (ok) {
      if (ok) {
        closeForm();
      } else {
        cfg.monitors = snapshot;
        renderMonitors();
      }
    });
  };

  $('delete').onclick = function () {
    if (!confirm('Delete this monitor?')) return;
    var snapshot = cfg.monitors.map(function (x) { return Object.assign({}, x); });
    cfg.monitors = cfg.monitors.filter(function (x) { return x.id !== editingId; });
    formFlash('deleting…');
    saveAll(formFlash).then(function (ok) {
      if (ok) {
        closeForm();
      } else {
        cfg.monitors = snapshot;
        renderMonitors();
      }
    });
  };

  $('save').onclick = function () { saveAll(); };
  $('reboot').onclick = function () {
    if (!confirm('Reboot the device?')) return;
    getJSON('/api/reboot', {method: 'POST'}).catch(function () {});
    flash('rebooting…');
  };
  $('testHook').onclick = function () {
    flash('queueing test webhook…');
    getJSON('/api/test-webhook', {method: 'POST'}).then(function () {
      flash('test webhook queued…');
      var tries = 0;
      var timer = setInterval(function () {
        tries++;
        getJSON('/api/state').then(function (s) {
          if (s.test_webhook === 'sent') {
            clearInterval(timer);
            flash('webhook delivered');
          } else if (s.test_webhook === 'failed') {
            clearInterval(timer);
            flash('webhook failed: ' + (s.test_webhook_msg || 'error'), true);
          } else if (tries >= 15) {
            clearInterval(timer);
            flash('webhook still pending; check the device log', true);
          }
        }).catch(function () {});
      }, 1000);
    }).catch(function (e) {
      flash('webhook failed: ' + e.message, true);
    });
  };

  loadConfig().catch(function (e) { $('dev').textContent = 'error: ' + e.message; });
  poll();
  setInterval(poll, 3000);
})();
</script>
</body>
</html>
)=====";
