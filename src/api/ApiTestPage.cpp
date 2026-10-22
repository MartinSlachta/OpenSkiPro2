#include "ApiTestPage.h"

namespace ApiTestPage {

QByteArray html()
{
    return QByteArrayLiteral(R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenSkiPro2 API</title>
<style>
  :root { --bg:#f6f7f9; --fg:#1c2330; --muted:#6b7686; --panel:#fff; --line:#dde2ea; --accent:#1f6feb; }
  @media (prefers-color-scheme: dark) { :root { --bg:#14181f; --fg:#e4e8ef; --muted:#8b95a5; --panel:#1c222b; --line:#2c3440; --accent:#58a6ff; } }
  body { margin:0; font:14px/1.4 "Segoe UI", system-ui, sans-serif; background:var(--bg); color:var(--fg); }
  header { padding:12px 16px; border-bottom:1px solid var(--line); display:flex; gap:16px; align-items:baseline; flex-wrap:wrap; }
  h1 { font-size:16px; margin:0; }
  .muted { color:var(--muted); }
  main { display:grid; grid-template-columns:minmax(220px, 300px) 1fr; gap:12px; padding:12px 16px; }
  @media (max-width:800px) { main { grid-template-columns:1fr; } }
  section { background:var(--panel); border:1px solid var(--line); border-radius:6px; padding:10px; min-width:0; }
  h2 { font-size:13px; margin:0 0 8px; text-transform:uppercase; letter-spacing:.04em; color:var(--muted); }
  ul { list-style:none; margin:0; padding:0; }
  li a { color:var(--accent); text-decoration:none; cursor:pointer; }
  li { padding:2px 0; display:flex; justify-content:space-between; gap:8px; }
  pre { margin:0; white-space:pre-wrap; word-break:break-word; font:12px/1.35 Consolas, monospace; max-height:60vh; overflow:auto; }
  #log div { border-bottom:1px solid var(--line); padding:3px 0; font:12px Consolas, monospace; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; cursor:pointer; }
  #log div.open { white-space:pre-wrap; }
  .t { display:inline-block; min-width:90px; font-weight:600; }
  input, button { font:inherit; }
  .row { display:flex; gap:6px; margin-bottom:8px; flex-wrap:wrap; }
  #clock { font:600 18px Consolas, monospace; }
</style>
</head>
<body>
<header>
  <h1>OpenSkiPro2 API</h1>
  <span id="clock">--:--:--</span>
  <span id="ws" class="muted">WebSocket: connecting</span>
  <a href="/docs" style="color:var(--accent)">API docs (Swagger UI)</a>
  <a href="/api/v1/openapi.yaml" style="color:var(--accent)">OpenAPI description</a>
</header>
<main>
  <section>
    <h2>Resources</h2>
    <ul id="res"></ul>
  </section>
  <section>
    <h2 id="title">Resource</h2>
    <pre id="body"></pre>
  </section>
  <section style="grid-column:1/-1">
    <h2>WebSocket messages</h2>
    <div class="row">
      <input id="sub" placeholder="results, run/1/results?group=0" size="40">
      <button id="subBtn">Subscribe</button>
      <label><input type="checkbox" id="showClock"> clock</label>
      <button id="clear">Clear</button>
    </div>
    <div id="log"></div>
  </section>
</main>
<script>
const $ = id => document.getElementById(id);
const path = n => '/api/v1/' + n.replace(/^run\//, 'runs/');
let ws;
function fmt(ms) { if (ms == null) return '--:--:--'; const s = Math.floor(ms / 1000) % 86400;
  return [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60].map(v => String(v).padStart(2, '0')).join(':'); }
async function show(name) {
  $('title').textContent = name;
  const r = await fetch(path(name));
  $('body').textContent = r.status + '\n' + JSON.stringify(await r.json(), null, 2);
}
function listResources(status) {
  const ul = $('res'); ul.innerHTML = '';
  for (const [n, rev] of Object.entries(status.revs || {})) {
    const li = document.createElement('li');
    const a = document.createElement('a'); a.textContent = n; a.onclick = () => show(n);
    const s = document.createElement('span'); s.className = 'muted'; s.textContent = 'rev ' + rev;
    li.append(a, s); ul.append(li);
  }
}
function log(msg) {
  if (msg.type === 'clock') { $('clock').textContent = fmt(msg.data.dayMs) + (msg.data.synced ? '' : ' (PC)'); if (!$('showClock').checked) return; }
  if (msg.type === 'status') listResources(msg.data);
  const d = document.createElement('div');
  d.innerHTML = '<span class="t"></span>';
  d.firstChild.textContent = msg.seq + ' ' + msg.type;
  d.append(' ' + JSON.stringify(msg.data));
  d.onclick = () => { d.classList.toggle('open'); if (d.classList.contains('open')) d.lastChild.textContent = ' ' + JSON.stringify(msg.data, null, 2); };
  $('log').prepend(d);
  while ($('log').childElementCount > 300) $('log').lastChild.remove();
}
function connect() {
  ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
  ws.onopen = () => $('ws').textContent = 'WebSocket: connected';
  ws.onclose = () => { $('ws').textContent = 'WebSocket: disconnected, retrying'; setTimeout(connect, 2000); };
  ws.onmessage = e => log(JSON.parse(e.data));
}
$('subBtn').onclick = () => {
  const names = $('sub').value.split(',').map(s => s.trim()).filter(Boolean);
  if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: 'subscribe', data: { resources: names } }));
};
$('clear').onclick = () => $('log').innerHTML = '';
fetch('/api/v1/status').then(r => r.json()).then(listResources);
show('status');
connect();
</script>
</body>
</html>
)HTML");
}

QByteArray swaggerHtml()
{
    return QByteArrayLiteral(R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenSkiPro2 API docs</title>
<link rel="stylesheet" href="/docs/swagger-ui.css">
<style> body { margin:0; background:#fff; } </style>
</head>
<body>
<div id="swagger-ui"></div>
<script src="/docs/swagger-ui-bundle.js"></script>
<script>
window.ui = SwaggerUIBundle({
  url: '/api/v1/openapi.yaml',
  dom_id: '#swagger-ui',
  deepLinking: true,
  tryItOutEnabled: true,
  displayRequestDuration: true,
  docExpansion: 'list',
  defaultModelsExpandDepth: 0
});
</script>
</body>
</html>
)HTML");
}

} // namespace ApiTestPage
