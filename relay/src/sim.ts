// A browser stand-in for the Deskling screen. It speaks the same WebSocket
// protocol as the firmware, so you can test hooks before the board is ready.
export const SIM_HTML = /* html */ `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Deskling simulator</title>
<style>
:root{--ground:#FBF6EF;--card:#fff;--ink:#2A2433;--muted:#6A6274;--accent:#CFC4F2;--alert:#FDE8E1;--alertInk:#C8553D;--good:#D3EEDF;--goodInk:#2F8A63}
*{box-sizing:border-box}
body{margin:0;background:var(--ground);color:var(--ink);font:600 16px/1.4 Nunito,system-ui,sans-serif}
main{max-width:1280px;margin:0 auto;padding:24px;display:grid;grid-template-columns:380px minmax(0,1fr);gap:20px}
@media (max-width:800px){main{grid-template-columns:1fr}}
section{background:var(--card);border-radius:28px;padding:20px 22px}
h2{margin:0 0 10px;font-size:14px;letter-spacing:1.2px;text-transform:uppercase;color:var(--muted)}
.hero{background:var(--accent)}
.hero h1{margin:6px 0;font-size:30px}
.row{display:flex;align-items:center;gap:12px;padding:8px 0;border-bottom:2px solid #F3EDE4}
.row:last-child{border-bottom:none}
.dot{width:12px;height:12px;border-radius:50%;flex:none}
.pill{border-radius:999px;padding:3px 12px;font-size:13px;font-weight:800;margin-left:auto;white-space:nowrap}
.muted{color:var(--muted);font-weight:600}
.alert{background:var(--alert);grid-column:1/-1}
.alert code{display:block;background:#fff;border:3px solid var(--ink);border-radius:18px;padding:14px 18px;font-size:18px;margin:10px 0;word-break:break-all}
button{font:inherit;font-weight:800;border-radius:18px;border:3px solid var(--ink);background:#fff;color:var(--ink);padding:12px 20px;cursor:pointer;min-height:44px}
button.primary{background:var(--ink);color:var(--ground)}
.check{width:40px;height:40px;border-radius:50%;padding:0;flex:none}
.check.done{background:var(--goodInk);border-color:var(--goodInk)}
.bar{grid-column:1/-1;display:flex;gap:12px;flex-wrap:wrap;align-items:center}
.bar input{font:inherit;padding:10px 14px;border-radius:14px;border:2px solid #ddd;min-width:240px}
#status{margin-left:auto}
.face{font-size:56px;line-height:1}
</style>
</head>
<body>
<main>
  <div class="bar">
    <strong>deskling simulator</strong>
    <label>Token <input id="token" type="password" autocomplete="off"></label>
    <button id="connect">Connect</button>
    <span id="status" class="muted">not connected</span>
  </div>
  <div id="pending"></div>
  <section class="hero" id="hero"><h2>Deskling</h2><div class="face" aria-hidden="true">(•‿•)</div><h1>Nothing yet</h1><div class="muted">Connect, then start Claude Code.</div></section>
  <div style="display:grid;gap:20px">
    <section><h2>Agents</h2><div id="agents" class="muted">None yet</div></section>
    <section id="chat"></section>
    <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:20px">
      <section><h2>Reminders</h2><div id="reminders" class="muted">None</div></section>
      <section><h2>Feed</h2><div id="feed" class="muted">Quiet</div></section>
    </div>
    <div class="bar">
      <button class="primary" data-action="focus">Focus mode</button>
      <button data-action="snooze_all">Snooze all 15 min</button>
      <button data-action="ping_when_done">Ping me when done</button>
    </div>
  </div>
</main>
<script>
const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, ...kids) => { const n = Object.assign(document.createElement(tag), props); n.append(...kids); return n; };
const COLORS = { idle: ['#E7E2FA', '#5B4FA8', 'Idle'], working: ['#E7E2FA', '#5B4FA8', 'Working'], waiting: ['#FDE9DC', '#B8860B', 'Waiting'], needs_you: ['#FAD9CF', '#C8553D', 'Needs you'], done: ['#D3EEDF', '#2F8A63', 'Done'] };
const FACES = { working: '(•‿•)…', needs_you: '(°□°;)', done: '(^▽^)', waiting: '(•ᴗ•)?', idle: '(-‿-) z' };
let ws, state, selected = null;
try { $('token').value = localStorage.getItem('deskling-token') || ''; } catch {}
const send = (m) => ws && ws.readyState === 1 && ws.send(JSON.stringify(m));
const ago = (ts) => { const s = Math.max(0, (state.now - ts) / 1000); return s < 60 ? 'now' : s < 3600 ? Math.floor(s / 60) + 'm' : Math.floor(s / 3600) + 'h'; };
function connect() {
  const token = $('token').value.trim();
  try { localStorage.setItem('deskling-token', token); } catch {}
  if (ws) ws.close();
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(proto + '://' + location.host + '/ws?token=' + encodeURIComponent(token));
  $('status').textContent = 'connecting…';
  ws.onopen = () => { $('status').textContent = 'connected'; };
  ws.onclose = () => { $('status').textContent = 'disconnected'; };
  ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.type === 'snapshot') { state = m; render(); } };
}
function render() {
  const agents = state.agents;
  const lead = agents.find((a) => a.state === 'needs_you') || agents.find((a) => a.state === 'working') || agents[0];
  const hero = $('hero');
  hero.replaceChildren(el('h2', { textContent: state.focus ? 'Deskling · focus' : 'Deskling' }),
    el('div', { className: 'face', textContent: FACES[lead ? lead.state : 'idle'] }),
    el('h1', { textContent: lead ? ({ working: 'Claude is busy', needs_you: 'Claude needs you', done: 'All done!', waiting: 'Your turn', idle: 'Ready when you are' })[lead.state] : 'All quiet' }),
    el('div', { className: 'muted', textContent: lead ? lead.task + ' · ' + lead.name + ' · ' + lead.host : '' }));
  $('agents').replaceChildren(...(agents.length ? agents.map((a) => { const c = COLORS[a.state]; return el('div', { className: 'row', style: 'cursor:pointer', onclick: () => { selected = a.id; render(); } }, el('span', { className: 'dot', style: 'background:' + c[1] }), el('span', {}, el('strong', { textContent: a.name }), el('span', { className: 'muted', textContent: ' · ' + a.host })), el('span', { className: 'muted', textContent: ' ' + a.source }), el('span', { className: 'pill', style: 'background:' + c[0], textContent: c[2] })); }) : [document.createTextNode('None yet')]));
  $('reminders').replaceChildren(...(state.reminders.length ? state.reminders.map((r) => el('div', { className: 'row' }, el('button', { className: 'check' + (r.done ? ' done' : ''), ariaLabel: 'Toggle ' + r.title, onclick: () => send({ type: 'reminder_done', id: r.id, done: !r.done }) }), el('div', { style: r.done ? 'opacity:.55;text-decoration:line-through' : '' }, el('div', { textContent: r.title }), el('div', { className: 'muted', textContent: (r.due ? new Date(r.due).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' }) : 'Any time') + ' · from ' + r.from })))) : [document.createTextNode('None')]));
  $('feed').replaceChildren(...(state.feed.length ? state.feed.slice(0, 6).map((f) => el('div', { className: 'row' }, el('span', { className: 'dot', style: 'background:' + ({ alert: '#C8553D', good: '#2F8A63', reminder: '#B8860B' }[f.level] || '#5B4FA8') }), el('div', {}, el('div', { textContent: f.title }), el('div', { className: 'muted', textContent: f.body })), el('span', { className: 'pill', textContent: ago(f.ts) }))) : [document.createTextNode('Quiet')]));
  const p = state.pending[0];
  let card = [];
  if (p && p.kind === 'question') {
    const picks = {};
    const qs = p.questions.map((q) => el('div', {}, el('h1', { textContent: q.question }), el('div', { className: 'bar' }, ...q.options.map((o) => { const b = el('button', { textContent: o.label, title: o.description }); b.onclick = () => { if (q.multiSelect) { const cur = new Set(picks[q.question] || []); cur.has(o.label) ? cur.delete(o.label) : cur.add(o.label); picks[q.question] = [...cur]; b.classList.toggle('primary'); } else { picks[q.question] = o.label; if (p.questions.length === 1) send({ type: 'answer', id: p.id, answers: picks }); else b.classList.add('primary'); } }; return b; }))));
    card = [el('section', { className: 'alert' }, el('h2', { textContent: p.name + ' · ' + p.host + ' · question' }), el('div', { className: 'face', textContent: '(•ᴗ•)?' }), ...qs, el('div', { className: 'bar' }, el('button', { className: 'primary', textContent: 'Send answers', onclick: () => send({ type: 'answer', id: p.id, answers: picks }) }), el('button', { textContent: 'On my computer', onclick: () => send({ type: 'decision', id: p.id, decision: 'computer' }) })))];
  } else if (p) {
    card = [el('section', { className: 'alert' }, el('h2', { textContent: p.name + ' · ' + p.host + ' · ' + p.tool }), el('div', { className: 'face', textContent: '(°□°;)' }), el('h1', { textContent: 'Claude wants to use ' + p.tool }), el('code', { textContent: p.summary || '(no details)' }), p.detail ? el('p', { className: 'muted', textContent: p.detail }) : '', el('div', { className: 'bar' }, el('button', { className: 'primary', textContent: 'Allow once', onclick: () => send({ type: 'decision', id: p.id, decision: 'allow' }) }), el('button', { textContent: 'Deny', onclick: () => send({ type: 'decision', id: p.id, decision: 'deny' }) }), el('button', { textContent: 'On my computer', onclick: () => send({ type: 'decision', id: p.id, decision: 'computer' }) })))];
  }
  $('pending').replaceChildren(...card);
  const a = agents.find((x) => x.id === selected) || null;
  $('chat').replaceChildren(...(a ? [el('h2', { textContent: a.name + ' · ' + a.host + ' · ' + a.source + (a.channel ? ' · live channel' : '') }), ...(a.chat || []).map((c) => el('div', { className: 'row' }, el('strong', { textContent: c.from === 'you' ? 'You' : 'Claude' }), el('span', { textContent: c.text }), el('span', { className: 'pill', textContent: c.status }))), a.canMessage ? el('form', { className: 'bar', onsubmit: (e) => { e.preventDefault(); const i = e.target.querySelector('input'); if (i.value.trim()) send({ type: 'message', agentId: a.id, text: i.value.trim() }); i.value = ''; } }, el('input', { placeholder: 'Message this session…' }), el('button', { className: 'primary', textContent: 'Send' }), ...['Continue', 'Run the tests', 'Wrap up and summarise'].map((t) => el('button', { type: 'button', textContent: t, onclick: () => send({ type: 'message', agentId: a.id, text: t }) }))) : el('p', { className: 'muted', textContent: 'This tool can’t take messages from Deskling.' })] : [el('p', { className: 'muted', textContent: 'Tap an agent to open its chat.' })]));
}
$('connect').onclick = connect;
document.querySelectorAll('[data-action]').forEach((b) => (b.onclick = () => send({ type: 'action', name: b.dataset.action })));
setInterval(() => { if (state) { state.now += 1000; } }, 1000);
if ($('token').value) connect();
</script>
</body>
</html>`;
