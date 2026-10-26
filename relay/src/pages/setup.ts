// /setup: everything you need to connect Deskling, Claude Code and your other tools to
// this relay, plus buttons that send test events to the screen.
export const SETUP_HTML = /* html */ `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Deskling setup</title>
<style>
:root{--ground:#FBF6EF;--card:#fff;--ink:#2A2433;--soft:#3A3346;--muted:#6A6274;--lilac:#CFC4F2;--purple:#5B4FA8;--good:#2F8A63;--goodbg:#D3EEDF;--alert:#C8553D;--alertbg:#FDE8E1;--line:#E4DCCF;--butter:#FBF0D6}
*{box-sizing:border-box}
body{margin:0;background:var(--ground);color:var(--ink);font:600 16px/1.5 ui-rounded,"Nunito","SF Pro Rounded",system-ui,sans-serif}
main{max-width:820px;margin:0 auto;padding:28px 16px 64px}
h1{font-size:38px;margin:0;font-weight:900}
h2{font-size:14px;letter-spacing:1.2px;text-transform:uppercase;color:var(--muted);margin:0 0 10px}
h3{margin:14px 0 6px}
section{background:var(--card);border-radius:26px;padding:22px;margin:16px 0}
.muted{color:var(--muted);font-weight:600}
input{width:100%;font:inherit;padding:12px 14px;border-radius:14px;border:2px solid var(--line);background:var(--ground)}
button{font:inherit;font-weight:800;border:3px solid var(--ink);border-radius:16px;background:var(--ink);color:var(--ground);padding:10px 18px;cursor:pointer;min-height:44px}
button.ghost{background:var(--card);color:var(--ink)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.copy{display:flex;gap:8px;align-items:stretch;margin:6px 0}
.copy code{flex:1;display:block;background:var(--ground);border-radius:12px;padding:11px 14px;font:500 14px/1.4 ui-monospace,Menlo,monospace;overflow-x:auto;white-space:pre}
.copy button{padding:6px 14px;min-height:0}
.pill{display:inline-block;border-radius:999px;padding:4px 14px;font-size:14px;font-weight:800;background:var(--line)}
.pill.ok{background:var(--goodbg);color:var(--good)}.pill.bad{background:var(--alertbg);color:var(--alert)}
.msg{font-weight:800;margin-top:8px}.ok{color:var(--good)}.bad{color:var(--alert)}
.steps{counter-reset:s;list-style:none;padding:0;margin:0}
.steps>li{counter-increment:s;position:relative;padding-left:44px;margin:14px 0}
.steps>li:before{content:counter(s);position:absolute;left:0;top:0;width:30px;height:30px;border-radius:50%;background:var(--lilac);display:flex;align-items:center;justify-content:center;font-weight:900}
a{color:var(--purple)}
</style>
</head>
<body>
<main>
  <h1>Set up Deskling</h1>
  <p class="muted">This is your relay: the little server your agents and your screen both talk to. It's yours, on your Cloudflare account.</p>

  <section id="gate">
    <h2>Your relay token</h2>
    <p class="muted">The DESKLING_TOKEN you chose when you deployed. It's only kept in this browser tab.</p>
    <div class="row"><input id="token" type="password" placeholder="Paste your token" style="flex:1"><button id="unlock">Continue</button></div>
    <div id="gatemsg" class="msg bad"></div>
  </section>

  <div id="app" hidden>
    <section>
      <h2>Status</h2>
      <div class="row"><span id="screens" class="pill">…</span><span id="agents" class="pill">…</span><span class="muted">Live preview: <a href="/sim" target="_blank">the browser simulator</a></span></div>
    </section>

    <section>
      <h2>Your details</h2>
      <h3>Relay URL</h3>
      <div class="copy"><code id="url"></code><button class="ghost" data-copy="url">Copy</button></div>
      <h3>Token</h3>
      <div class="copy"><code id="tok">••••••••</code><button class="ghost" id="reveal">Show</button><button class="ghost" data-copy="tokraw">Copy</button></div>
      <span id="tokraw" hidden></span>
    </section>

    <section>
      <h2>The screen</h2>
      <ol class="steps">
        <li><b>Install the firmware</b> once over USB: <a href="/flash">open the installer</a> in Chrome or Edge.</li>
        <li><b>Pick your Wi-Fi</b> on Deskling's screen.</li>
        <li><b>Open the address it shows</b> (usually <code>http://deskling.local</code>) on this computer, type the 4-digit code from the screen, and paste the relay URL and token from above.</li>
      </ol>
      <p class="muted">After that it updates itself over Wi-Fi; you'll never need the cable again.</p>
    </section>

    <section>
      <h2>Claude Code (Mac, Windows, desktop app)</h2>
      <p>In Claude Code, run these three commands. The last one asks for the relay URL and token from above.</p>
      <div class="copy"><code id="mk">/plugin marketplace add moderniselife/deskling</code><button class="ghost" data-copy="mk">Copy</button></div>
      <div class="copy"><code id="plugin">/plugin install deskling@deskling</code><button class="ghost" data-copy="plugin">Copy</button></div>
      <div class="copy"><code id="pconf">/plugin configure deskling@deskling</code><button class="ghost" data-copy="pconf">Copy</button></div>
      <p class="muted">Prefer the terminal? This sets both in one go (the token is included when you copy it):</p>
      <div class="copy"><code id="cliconf"></code><button class="ghost" data-copy="cliconfraw">Copy</button></div>
      <span id="cliconfraw" hidden></span>
      <p class="muted">Restart Claude Code afterwards. The desktop app uses the same plugins. To send messages to sessions that are sitting idle, start Claude with <code>claude --channels plugin:deskling@deskling</code>.</p>
    </section>

    <section>
      <h2>Claude app, Cowork and cloud sessions</h2>
      <p>Sessions in the Claude app (Cowork) and in Anthropic's cloud never see this computer, so they can't read your settings. Upload your own copy of the plugin to your Claude account once and it follows you everywhere: Cowork on your computer and in the cloud, and Claude Code.</p>
      <ol class="steps">
        <li><button id="dlplugin">Download my plugin</button> <span class="muted" id="dlmsg"></span></li>
        <li>In the Claude app, open <b>Customize &rarr; Plugins</b>, choose <b>Add</b> &rarr; <b>Upload plugin</b> and pick <code>deskling-personal.zip</code>.</li>
        <li>Start a new session. Cloud sessions show up as <b>Cloud</b>, Cowork ones as <b>Cowork</b>.</li>
      </ol>
      <p class="muted">The zip has your relay token in it, so keep it to yourself. Already installed Deskling from the marketplace too? That's fine: the relay ignores the doubled-up events. For Claude Code on the web, if your cloud environment's network access is <b>Trusted</b>, add <code id="relayhost"></code> to its allowed domains.</p>
    </section>

    <section>
      <h2>The config file: Claude desktop app, Cursor, scripts</h2>
      <p>Save this as <code>~/.deskling/config</code> (Windows: <code>%USERPROFILE%\\.deskling\\config</code>). Everything reads it: the Claude Code plugin (handy when your Claude app has no place to enter plugin settings), the Cursor hooks and the <code>deskling</code> command. Restart Claude afterwards.</p>
      <div class="copy"><code id="conf"></code><button class="ghost" data-copy="confraw">Copy</button></div>
      <span id="confraw" hidden></span>
      <p class="muted">Cursor: see <a href="https://github.com/moderniselife/deskling/tree/main/hooks#cursor" target="_blank">hooks/README</a>.</p>
    </section>

    <section>
      <h2>Try it</h2>
      <p class="muted">Each button sends something to every connected screen (and the simulator).</p>
      <div class="row">
        <button id="t-notify">Send a notification</button>
        <button id="t-remind">Remind me in 1 minute</button>
        <button id="t-agent">Add a pretend session</button>
        <button class="ghost" id="t-clear">Remove it</button>
      </div>
      <div class="row" style="margin-top:10px">
        <button id="t-perm">Ask for permission</button>
        <button id="t-question">Ask a question</button>
      </div>
      <div id="tmsg" class="msg"></div>
    </section>
  </div>
</main>

<script>
const $ = (id) => document.getElementById(id);
let token = '';
try { token = sessionStorage.getItem('deskling-token') || ''; } catch {}
const origin = location.origin;

async function call(path, opts = {}) {
  const r = await fetch(path, { ...opts, headers: { authorization: 'Bearer ' + token, 'content-type': 'application/json', ...(opts.headers || {}) } });
  if (r.status === 401) throw new Error('That token doesn\\u2019t match this relay.');
  const t = await r.text();
  try { return t ? JSON.parse(t) : {}; } catch { return { raw: t }; }
}
const event = (body) => call('/api/event', { method: 'POST', body: JSON.stringify(body) });
const say = (m, ok = true) => { $('tmsg').textContent = m; $('tmsg').className = 'msg ' + (ok ? 'ok' : 'bad'); };

async function unlock() {
  token = $('token').value.trim() || token;
  try { await call('/api/health'); } catch (e) { $('gatemsg').textContent = e.message; return; }
  try { sessionStorage.setItem('deskling-token', token); } catch {}
  $('gate').hidden = true; $('app').hidden = false;
  $('url').textContent = origin; $('tokraw').textContent = token;
  const conf = 'DESKLING_URL="' + origin + '"\\nDESKLING_TOKEN="' + token + '"';
  $('confraw').textContent = conf; $('conf').textContent = conf.replace(token, '\\u2022'.repeat(8));
  const json = JSON.stringify({ relay_url: origin, token });
  const cli = "echo '" + json + "' | claude plugin configure deskling@deskling --values-stdin";
  $('cliconfraw').textContent = cli; $('cliconf').textContent = cli.replace(token, '\\u2022'.repeat(8));
  poll(); setInterval(poll, 3000);
}
$('unlock').onclick = unlock;
$('token').addEventListener('keydown', (e) => { if (e.key === 'Enter') unlock(); });
$('relayhost').textContent = location.host;
$('dlplugin').onclick = async () => {
  $('dlmsg').textContent = 'Building\u2026';
  try {
    const r = await fetch('/plugin.zip', { headers: { authorization: 'Bearer ' + token } });
    if (!r.ok) throw new Error(r.status === 401 ? 'That token doesn\u2019t match this relay.' : 'Couldn\u2019t build it (' + r.status + ').');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(await r.blob());
    a.download = 'deskling-personal.zip';
    document.body.appendChild(a); a.click(); a.remove();
    $('dlmsg').textContent = 'Downloaded deskling-personal.zip';
  } catch (e) { $('dlmsg').textContent = e.message; }
};
$('reveal').onclick = () => { const shown = $('tok').textContent === token; $('tok').textContent = shown ? '\\u2022'.repeat(8) : token; $('reveal').textContent = shown ? 'Show' : 'Hide'; };
document.querySelectorAll('[data-copy]').forEach((b) => b.onclick = async () => { await navigator.clipboard.writeText($(b.dataset.copy).textContent); b.textContent = 'Copied!'; setTimeout(() => b.textContent = 'Copy', 1200); });

async function poll() {
  try {
    const h = await call('/api/health');
    $('screens').textContent = h.screens === 1 ? '1 screen connected' : h.screens + ' screens connected';
    $('screens').className = 'pill ' + (h.screens ? 'ok' : 'bad');
    $('agents').textContent = h.agents === 1 ? '1 session' : h.agents + ' sessions';
  } catch {}
}

$('t-notify').onclick = () => event({ kind: 'notify', title: 'Hello from setup!', body: 'If you can read this, the relay works.', level: 'good' }).then(() => say('Sent! Look at the screen.')).catch((e) => say(e.message, false));
$('t-remind').onclick = () => event({ kind: 'reminder', title: 'Stand up and stretch', in_minutes: 1, from: 'setup page' }).then(() => say('Reminder set for a minute from now.')).catch((e) => say(e.message, false));
$('t-agent').onclick = () => event({ kind: 'status', id: 'setup-demo', name: 'demo-project', host: 'This browser', state: 'working', task: 'Pretending to refactor the login flow', source: 'custom' }).then(() => say('A pretend session is "working" now.')).catch((e) => say(e.message, false));
$('t-clear').onclick = () => event({ kind: 'status', id: 'setup-demo', remove: true }).then(() => say('Removed.')).catch((e) => say(e.message, false));

async function fakeHook(body, label) {
  say(label + ' Waiting up to 60 s for you to answer on the screen\\u2026');
  const r = await call('/hook?wait=60', { method: 'POST', headers: { 'x-deskling-host': 'This browser', 'x-deskling-os': 'web' }, body: JSON.stringify({ session_id: 'setup-demo-hook', cwd: '/demo/setup-test', ...body }) });
  const out = r.hookSpecificOutput;
  if (!out) return say('No answer (it timed out, or you picked "On my computer").', false);
  if (out.decision) return say('You tapped ' + (out.decision.behavior === 'allow' ? 'Allow' : 'Deny') + ' \\u2714');
  if (out.permissionDecisionReason) return say('Answer received: ' + out.permissionDecisionReason.split('\\n')[1]);
}
$('t-perm').onclick = () => fakeHook({ hook_event_name: 'PermissionRequest', tool_name: 'Bash', tool_input: { command: 'npm run deploy -- --prod', description: 'Test request from the setup page' } }, 'Sent!').catch((e) => say(e.message, false));
$('t-question').onclick = () => fakeHook({ hook_event_name: 'PreToolUse', tool_name: 'AskUserQuestion', tool_input: { questions: [{ question: 'Which colour should Deskling be?', header: 'Colour', multiSelect: false, options: [{ label: 'Lilac', description: 'The classic' }, { label: 'Peach', description: 'Warm and cosy' }, { label: 'Mint', description: 'Fresh' }] }] } }, 'Sent!').catch((e) => say(e.message, false));

if (token) { $('token').value = token; unlock(); }
</script>
</body>
</html>`;
